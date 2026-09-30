#include "tools/coordinates.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "tools/flycam.h"
#include "ui/notifications.h"
#include "ui/ui_hotkey.h"

#include "d/actor/d_a_alink.h"
#include "d/d_camera.h"
#include "d/d_com_inf_game.h"
#include "d/d_stage.h"

#include "imgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace Coordinates {
struct Saved {
    char stage[dStage_NAME_LEN + 1];
    s8   room;
    s8   layer;
    s16  spawn;
    cXyz linkPos;
    s16  linkAngle;
    cXyz camAt;
    cXyz camEye;
};

static const char     kNotifyKey[] = "coords";
static const char     kTitle[] = "Coordinates";
static const unsigned kSlowLoadFrames = 900;
static const float    kMaxCoord = 1.0e7f;

static bool     s_enabled = false;
static bool     s_hasSaved = false;
static Saved    s_saved = {};
static bool     s_stampNextFrame = false;
static bool     s_roomLoading = false;
static bool     s_slowWarned = false;
static unsigned s_loadFrames = 0;
static char     s_status[96] = "";

static void setStatus(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static void notify(Notifications::Kind kind, const char* message)
{
    Notifications::ShowKeyed(kNotifyKey, kind, kTitle, message);
}

static bool fail(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
    notify(Notifications::Error, s_status);
    return false;
}

static daAlink_c* link()
{
    return (daAlink_c*)dComIfGp_getPlayer();
}

static bool sameStage(const char* a, const char* b)
{
    return strncmp(a, b, dStage_NAME_LEN) == 0;
}

static void copyStageName(char* out, const char* stage)
{
    memset(out, 0, dStage_NAME_LEN + 1);
    for (int i = 0; i < dStage_NAME_LEN && stage[i]; ++i)
        out[i] = stage[i];
}

static bool sceneAvailable(daAlink_c* l)
{
    return l && dCam_getCurrentXform() && dStage_getStageName()[0] && !dStage_warpPending();
}

static void pinLink(daAlink_c* l)
{
    daAlink_setPos(l, &s_saved.linkPos, s_saved.linkAngle, s_saved.room);
    daAlink_procWaitInit(l);
    daAlink_clearMomentum(l);
}

static bool applySaved()
{
    daAlink_c* l = link();
    if (!l || !dCam_getCurrentXform())
        return false;
    dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
    pinLink(l);
    return dCam_snapXform(&s_saved.camAt, &s_saved.camEye);
}

static void arrived()
{
    s_stampNextFrame = true;
    s_status[0] = '\0';
    notify(Notifications::Success, "Loaded");
}

static void stopRoomLoad()
{
    if (s_roomLoading)
        dStage_clearRoomRequest();
    s_roomLoading = false;
    s_slowWarned = false;
    s_loadFrames = 0;
}

static bool beginRoomLoad()
{
    const s8 stay = dStage_getStayRoomNo();
    if (s_saved.room < 0 || s_saved.room >= dStage_ROOM_COUNT ||
        stay < 0 || stay >= dStage_ROOM_COUNT) {
        Logger::LogWarn("[coords] room load rejected: saved room %d, stay room %d",
                        (int)s_saved.room, (int)stay);
        return fail("Cannot load that room from here.");
    }

    s_roomLoading = true;
    s_slowWarned = false;
    s_loadFrames = 0;

    dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
    dStage_requestRoom(s_saved.room);
    if (!applySaved()) {
        stopRoomLoad();
        return fail("Cannot load right now.");
    }

    setStatus("Loading room %d...", (int)s_saved.room);
    notify(Notifications::Info, "Loading the saved room");
    Logger::Log("[coords] room load started: stage %s, room %d to %d, spawn %d, layer %d",
                s_saved.stage, (int)stay, (int)s_saved.room, (int)s_saved.spawn,
                (int)s_saved.layer);
    return true;
}

static void tickRoomLoad()
{
    daAlink_c* l = link();
    const char* stage = dStage_getStageName();
    if (!l || !stage[0] || !sameStage(stage, s_saved.stage) || dStage_warpPending()) {
        stopRoomLoad();
        fail("Room load cancelled, the scene changed.");
        Logger::LogWarn("[coords] room load cancelled, scene changed");
        return;
    }

    ++s_loadFrames;
    dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
    dStage_requestRoom(s_saved.room);

    // Pinned until the room's collision exists, or the game's room check sends Link back.
    pinLink(l);
    dCam_snapXform(&s_saved.camAt, &s_saved.camEye);

    if (dStage_getStayRoomNo() == s_saved.room && dStage_isRoomBackgroundReady(s_saved.room)) {
        const unsigned frames = s_loadFrames;
        stopRoomLoad();
        if (!applySaved()) {
            Logger::LogWarn("[coords] room %d ready but Link could not be placed",
                            (int)s_saved.room);
            fail("Room loaded, but Link could not be placed.");
            return;
        }
        Logger::Log("[coords] room %d ready after %u frames", (int)s_saved.room, frames);
        arrived();
        return;
    }

    if (!s_slowWarned && s_loadFrames >= kSlowLoadFrames) {
        s_slowWarned = true;
        setStatus("Still loading room %d...", (int)s_saved.room);
        Logger::LogWarn("[coords] room load pending %u frames: stay %d, target %d, status %02X",
                        s_loadFrames, (int)dStage_getStayRoomNo(), (int)s_saved.room,
                        (unsigned)dStage_getRoomStatusFlags(s_saved.room));
    }
}

static bool save()
{
    if (s_roomLoading)
        return fail("Wait for the room to finish loading.");

    daAlink_c* l = link();
    if (!sceneAvailable(l))
        return fail("Cannot save right now.");

    const dCamera_xform_c* cam = dCam_getCurrentXform();
    memset(&s_saved, 0, sizeof(s_saved));
    copyStageName(s_saved.stage, dStage_getStageName());
    s_saved.room = dStage_getStayRoomNo();
    if (s_saved.room < 0 || s_saved.room >= dStage_ROOM_COUNT)
        s_saved.room = dStage_getRoomNo();
    s_saved.layer = dStage_getLayer();
    s_saved.spawn = dStage_getSpawn();
    s_saved.linkPos = l->current.pos;
    s_saved.linkAngle = l->shape_angle.y;
    s_saved.camAt = cam->mAt;
    s_saved.camEye = cam->mEye;
    s_hasSaved = true;
    s_status[0] = '\0';
    Config::MarkDirty();

    Logger::Log("[coords] saved: stage %s, room %d, spawn %d, layer %d, pos (%.1f, %.1f, %.1f)",
                s_saved.stage, (int)s_saved.room, (int)s_saved.spawn, (int)s_saved.layer,
                (double)s_saved.linkPos.x, (double)s_saved.linkPos.y,
                (double)s_saved.linkPos.z);
    notify(Notifications::Success, "Saved");
    return true;
}

static bool load()
{
    if (s_roomLoading)
        return fail("Wait for the room to finish loading.");
    if (!s_hasSaved)
        return fail("No coordinates saved yet.");

    daAlink_c* l = link();
    if (!sceneAvailable(l))
        return fail("Cannot load right now.");

    const char* stage = dStage_getStageName();
    if (!sameStage(stage, s_saved.stage)) {
        char current[dStage_NAME_LEN + 1];
        copyStageName(current, stage);
        return fail("Saved in %s, you are in %s.", s_saved.stage, current);
    }

    FlyCam::Stop();

    if (dStage_getStayRoomNo() != s_saved.room || !dStage_isRoomBackgroundReady(s_saved.room))
        return beginRoomLoad();

    if (!applySaved())
        return fail("Cannot load right now.");
    Logger::Log("[coords] loaded in place: stage %s, room %d", s_saved.stage,
                (int)s_saved.room);
    arrived();
    return true;
}

// The game can recompute Link's room from his old spot after a big in-place move.
static void restamp()
{
    s_stampNextFrame = false;
    dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
    dStage_setLinkRoom(s_saved.room);
    daAlink_c* l = link();
    if (l) {
        daAlink_procWaitInit(l);
        daAlink_clearMomentum(l);
    }
    dCam_snapXform(&s_saved.camAt, &s_saved.camEye);
}

void DrawMenuItem()
{
    ImGui::BeginDisabled(s_roomLoading);
    bool enabled = s_enabled;
    if (ImGui::Checkbox("Save/Load Coordinates", &enabled)) {
        s_enabled = enabled;
        if (!enabled)
            s_stampNextFrame = false;
        Config::MarkDirty();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Loading works only in the stage you saved in.");
    ImGui::EndDisabled();
    if (!s_enabled)
        return;

    ImGui::Indent();
    Ui::Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_SAVE_COORDINATES), nullptr, ": save   ",
                         true);
    ImGui::SameLine(0.0f, 0.0f);
    Ui::Hotkey::DrawText(Hotkeys::Get(Hotkeys::HOTKEY_LOAD_COORDINATES), nullptr, ": load",
                         true);

    ImGui::BeginDisabled(s_roomLoading);
    if (ImGui::Button("Save coordinates"))
        save();
    ImGui::SameLine();
    ImGui::BeginDisabled(!s_hasSaved);
    if (ImGui::Button("Load coordinates"))
        load();
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (s_hasSaved)
        ImGui::TextDisabled("Slot: %s  room %d  spawn %d  layer %d", s_saved.stage,
                            (int)s_saved.room, (int)s_saved.spawn, (int)s_saved.layer);
    else
        ImGui::TextDisabled("Slot: empty");
    if (s_status[0])
        ImGui::TextWrapped("%s", s_status);
    ImGui::Unindent();
}

bool WantsInputBlock(uint32_t held)
{
    return s_enabled && (Hotkeys::Matches(Hotkeys::HOTKEY_SAVE_COORDINATES, held) ||
                         Hotkeys::Matches(Hotkeys::HOTKEY_LOAD_COORDINATES, held));
}

void Tick(bool acceptInput)
{
    if (!s_enabled) {
        s_stampNextFrame = false;
        stopRoomLoad();
        return;
    }
    if (s_roomLoading) {
        tickRoomLoad();
        return;
    }
    if (s_stampNextFrame)
        restamp();

    if (!acceptInput)
        return;
    if (Hotkeys::Pressed(Hotkeys::HOTKEY_SAVE_COORDINATES)) {
        Input::DrainHeld();
        save();
    } else if (Hotkeys::Pressed(Hotkeys::HOTKEY_LOAD_COORDINATES)) {
        Input::DrainHeld();
        load();
    }
}

void OnApplicationStart()
{
    s_stampNextFrame = false;
    s_roomLoading = false;
    s_slowWarned = false;
    s_loadFrames = 0;
    s_status[0] = '\0';
}

void ResetToDefaults()
{
    s_enabled = false;
    s_stampNextFrame = false;
}

static bool readVec(const cJSON* root, const char* prefix, cXyz* out)
{
    char key[32];
    snprintf(key, sizeof(key), "%sX", prefix);
    if (!Config::ReadFloat(root, key, &out->x, -kMaxCoord, kMaxCoord))
        return false;
    snprintf(key, sizeof(key), "%sY", prefix);
    if (!Config::ReadFloat(root, key, &out->y, -kMaxCoord, kMaxCoord))
        return false;
    snprintf(key, sizeof(key), "%sZ", prefix);
    return Config::ReadFloat(root, key, &out->z, -kMaxCoord, kMaxCoord);
}

static void addVec(cJSON* root, const char* prefix, const cXyz& v)
{
    char key[32];
    snprintf(key, sizeof(key), "%sX", prefix);
    Config::AddNumber(root, key, (double)v.x);
    snprintf(key, sizeof(key), "%sY", prefix);
    Config::AddNumber(root, key, (double)v.y);
    snprintf(key, sizeof(key), "%sZ", prefix);
    Config::AddNumber(root, key, (double)v.z);
}

void LoadConfig(const cJSON* root)
{
    Config::ReadBool(root, "coordsEnabled", &s_enabled);

    bool has = false;
    Config::ReadBool(root, "coordsSaved", &has);
    Saved s = {};
    int room = 0, spawn = 0, layer = 0, angle = 0;
    has = has && Config::ReadString(root, "coordsStage", s.stage, sizeof(s.stage)) &&
          s.stage[0] &&
          Config::ReadInt(root, "coordsRoom", &room, -1, dStage_ROOM_COUNT - 1) &&
          Config::ReadInt(root, "coordsSpawn", &spawn, -32768, 32767) &&
          Config::ReadInt(root, "coordsLayer", &layer, dStage_LAYER_DEFAULT, 15) &&
          Config::ReadInt(root, "coordsLinkAngle", &angle, -32768, 32767) &&
          readVec(root, "coordsLink", &s.linkPos) &&
          readVec(root, "coordsCamAt", &s.camAt) &&
          readVec(root, "coordsCamEye", &s.camEye);
    if (!has) {
        memset(&s_saved, 0, sizeof(s_saved));
        s_hasSaved = false;
        return;
    }
    s.room = (s8)room;
    s.spawn = (s16)spawn;
    s.layer = (s8)layer;
    s.linkAngle = (s16)angle;
    s_saved = s;
    s_hasSaved = true;
}

void SaveConfig(cJSON* root)
{
    Config::AddBool(root, "coordsEnabled", s_enabled);
    Config::AddBool(root, "coordsSaved", s_hasSaved);
    if (!s_hasSaved)
        return;
    Config::AddString(root, "coordsStage", s_saved.stage);
    Config::AddNumber(root, "coordsRoom", (double)s_saved.room);
    Config::AddNumber(root, "coordsSpawn", (double)s_saved.spawn);
    Config::AddNumber(root, "coordsLayer", (double)s_saved.layer);
    Config::AddNumber(root, "coordsLinkAngle", (double)s_saved.linkAngle);
    addVec(root, "coordsLink", s_saved.linkPos);
    addVec(root, "coordsCamAt", s_saved.camAt);
    addVec(root, "coordsCamEye", s_saved.camEye);
}
}
}
