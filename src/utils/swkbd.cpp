// utils/swkbd.cpp -- see swkbd.h.
//
// Mirrors GaryOderNichts' imgui_impl_wiiu swkbd flow, but standalone so we don't
// pull in that backend's touch/nav handling (input.cpp owns those). The game
// (TPHD) never uses nn::swkbd itself, so there's no Create() conflict.
#include "swkbd.h"

#include "imgui.h"
#include "imgui_internal.h"   // GetInputTextState / ImGuiInputTextState

#include <nn/swkbd.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <coreinit/filesystem.h>
#include <coreinit/debug.h>

#include <malloc.h>
#include <string.h>

namespace SwKbd {

static bool       s_ready   = false;
static void*      s_work    = nullptr;
static FSClient*  s_fsc     = nullptr;
static bool       s_wanted  = false;   // io.WantTextInput last frame (edge detect)
static bool       s_deactivatePending = false;  // drop ImGui focus after a dismiss
static bool       s_needRelease = false;        // ignore A/B carried over from the activation

// Latest stashed controller state (written by the read hooks).
static VPADStatus s_vpad      = {};
static bool       s_haveVpad  = false;
static KPADStatus s_kpad      = {};
static bool       s_haveKpad  = false;

bool Init()
{
    if (s_ready)
        return true;

    if (!s_work) {
        uint32_t sz = nn::swkbd::GetWorkMemorySize(0);
        s_work = memalign(0x40, sz);
        s_fsc  = (FSClient*)malloc(sizeof(FSClient));
        if (!s_work || !s_fsc)
            return false;
        FSAddClient(s_fsc, FS_ERROR_FLAG_NONE);
    }

    nn::swkbd::CreateArg ca;          // ctor zero-inits + sets defaults
    ca.workMemory = s_work;
    ca.regionType = nn::swkbd::RegionType::USA;   // TPHD US (AZAE)
    ca.fsClient   = s_fsc;
    if (nn::swkbd::Create(ca)) {
        s_ready = true;
        OSReport("[tphd_tools] swkbd ready\n");
    }
    return s_ready;
}

bool IsReady()  { return s_ready; }

bool IsVisible()
{
    return s_ready && nn::swkbd::GetStateInputForm() != nn::swkbd::State::Hidden;
}

void SetVpad(const VPADStatus* v)
{
    if (v) { s_vpad = *v; s_haveVpad = true; }
}

void SetKpad(const KPADStatus* k, int count)
{
    if (k && count > 0) { s_kpad = k[0]; s_haveKpad = true; }
    else                  s_haveKpad = false;
}

bool ProcessInput(ImGuiIO& io)
{
    if (!s_ready)
        return false;

    // After a dismiss, the InputText is still active so io.WantTextInput never
    // falls -> no rising edge to reopen. Once the keyboard has fully closed (and
    // the typed text has been applied to the field), clear ImGui's focus so that
    // clicking the field again produces a fresh edge.
    if (s_deactivatePending &&
        nn::swkbd::GetStateInputForm() == nn::swkbd::State::Hidden) {
        ImGui::ClearActiveID();
        s_deactivatePending = false;
        // Don't re-evaluate the appear edge this frame: io.WantTextInput is still
        // stale-true (the field deactivates on the next NewFrame). Leaving
        // s_wanted true keeps the edge from firing until WantTextInput falls and
        // the user re-focuses. (Fixes the keyboard popping up a second time.)
        return false;
    }

    // A text field just gained focus -> raise the keyboard, seeded from its
    // current contents.
    if (io.WantTextInput && !s_wanted) {
        nn::swkbd::AppearArg arg;
        ImGuiInputTextState* st = ImGui::GetInputTextState(ImGui::GetActiveID());
        if (st) {
            arg.inputFormArg.initialText          = (const char16_t*)st->TextW.Data;
            arg.inputFormArg.maxTextLength        = st->BufCapacityA;
            arg.inputFormArg.higlightInitialText  = !!(st->Flags & ImGuiInputTextFlags_AutoSelectAll);
        }
        arg.keyboardArg.configArg.languageType   = nn::swkbd::LanguageType::English;
        arg.keyboardArg.configArg.controllerType = nn::swkbd::ControllerType::DrcGamepad;
        if (nn::swkbd::GetStateInputForm() == nn::swkbd::State::Hidden) {
            nn::swkbd::AppearInputForm(arg);
            s_needRelease = true;   // the activation A/B is still held; wait for release
        }
    }
    s_wanted = io.WantTextInput;

    if (nn::swkbd::GetStateInputForm() == nn::swkbd::State::Hidden)
        return false;

    // Feed the keyboard fresh controller input (touch must be calibrated).
    VPADStatus* vpad  = s_haveVpad ? &s_vpad : nullptr;
    KPADStatus* kpad0 = s_haveKpad ? &s_kpad : nullptr;
    if (vpad)
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpad->tpNormal, &vpad->tpNormal);

    // The input that activated the field is still down the frame the keyboard
    // appears -- if we pass it through, swkbd treats it as a confirm/keypress/tap
    // and instantly closes. This covers BOTH the A/B that activated via the pad AND
    // the touch that activated via a tap: a tap lasts a few frames, and the
    // WantTextInput edge that raises the keyboard is itself a couple frames behind,
    // so the tap is usually still down on the appear frame (this was the "pops up
    // then immediately disappears, second time works" bug). Mask the carried input
    // until everything that activated the field is released.
    if (s_needRelease) {
        bool held = false;
        if (vpad && (vpad->hold & (VPAD_BUTTON_A | VPAD_BUTTON_B)))
            held = true;
        if (vpad && vpad->tpNormal.touched)   // the activating tap is still down
            held = true;
        if (kpad0) {
            if (kpad0->hold & (WPAD_BUTTON_A | WPAD_BUTTON_B)) held = true;
            if (kpad0->pro.hold & (WPAD_PRO_BUTTON_A | WPAD_PRO_BUTTON_B)) held = true;
            if (kpad0->classic.hold & (WPAD_CLASSIC_BUTTON_A | WPAD_CLASSIC_BUTTON_B)) held = true;
        }
        if (vpad) {
            vpad->hold &= ~(VPAD_BUTTON_A | VPAD_BUTTON_B);
            vpad->tpNormal.touched = 0;       // swallow the carried tap so it can't tap a key/cancel
        }
        if (kpad0) {
            kpad0->hold         &= ~(WPAD_BUTTON_A | WPAD_BUTTON_B);
            kpad0->pro.hold     &= ~(WPAD_PRO_BUTTON_A | WPAD_PRO_BUTTON_B);
            kpad0->classic.hold &= ~(WPAD_CLASSIC_BUTTON_A | WPAD_CLASSIC_BUTTON_B);
        }
        if (!held)
            s_needRelease = false;
    }

    nn::swkbd::ControllerInfo ci;
    ci.vpad    = vpad;
    ci.kpad[0] = kpad0;
    ci.kpad[1] = ci.kpad[2] = ci.kpad[3] = nullptr;
    nn::swkbd::Calc(ci);

    if (nn::swkbd::IsNeedCalcSubThreadFont())
        nn::swkbd::CalcSubThreadFont();
    if (nn::swkbd::IsNeedCalcSubThreadPredict())
        nn::swkbd::CalcSubThreadPredict();

    // Don't honor OK/Cancel while still waiting for the carried-over A/B release.
    if (!s_needRelease && nn::swkbd::IsDecideOkButton(nullptr)) {
        ImGuiInputTextState* st = ImGui::GetInputTextState(ImGui::GetActiveID());
        if (st)
            st->ClearText();
        for (const char16_t* s = nn::swkbd::GetInputFormString(); s && *s; ++s)
            io.AddInputCharacterUTF16(*s);
        nn::swkbd::DisappearInputForm();
        s_deactivatePending = true;   // drop focus once the text is applied + closed
    }
    if (!s_needRelease && nn::swkbd::IsDecideCancelButton(nullptr)) {
        nn::swkbd::DisappearInputForm();
        s_deactivatePending = true;
    }

    return true;
}

void DrawTV()
{
    if (IsVisible())
        nn::swkbd::DrawTV();
}

void DrawGamePad()
{
    if (IsVisible())
        nn::swkbd::DrawDRC();
}

} // namespace SwKbd
