// tools/save_load_coords.cpp -- see save_load_coords.h.
#include "tools/save_load_coords.h"

#include "game/game.h"
#include "imgui.h"
#include "input.h"
#include "logger.h"
#include "overlay.h"
#include "tools/flycam.h"

#include <stdio.h>
#include <string.h>

namespace Tools {
namespace SaveLoadCoords {

static bool s_enabled = false;
static bool s_hasSaved = false;
static SavedCoordinates s_saved = {};
static bool s_stampRoomNextFrame = false;
static bool s_roomLoadPending = false;
static bool s_roomLoadSlowWarning = false;
static unsigned s_roomLoadFrames = 0;
static char s_status[128] = {};

static const unsigned ROOM_LOAD_SLOW_FRAMES = 900;

static bool sameStage(const char* a, const char* b)
{
    return a && b && strncmp(a, b, DSTAGE_NAME_LEN) == 0;
}

static void copyStageName(char* out, const char* stage)
{
    memset(out, 0, 9);
    if (stage)
        memcpy(out, stage, strnlen(stage, DSTAGE_NAME_LEN));
}

static bool applySavedTransform()
{
    fopAc_ac_c* link = dComIfGp_getPlayer();
    CameraXform* cam = dCam_getCurrentXform();
    if (!link || !cam)
        return false;

    dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
    dStage_setLinkPos(&s_saved.linkPos, s_saved.linkAngle, s_saved.room);
    daAlink_procWaitInit(link);
    daAlink_clearMomentum(link);
    return dCam_snapXform(&s_saved.camAt, &s_saved.camEye);
}

static void clearRoomLoadRequest()
{
    if (s_roomLoadPending)
        dStage_clearRoomRequest();
    s_roomLoadPending = false;
    s_roomLoadSlowWarning = false;
    s_roomLoadFrames = 0;
}

static bool beginRoomLoad()
{
    const s8 stayRoom = dStage_getStayRoomNo();
    if (s_saved.room < 0 || s_saved.room >= 64 || stayRoom < 0 ||
        stayRoom >= 64) {
        snprintf(s_status, sizeof(s_status),
                 "Cannot stream room %d from active room %d.",
                 (int)s_saved.room, (int)stayRoom);
        Logger::LogWarn("[tphd_tools][coords] room load rejected: "
                        "saved room=%d active stay room=%d",
                        (int)s_saved.room, (int)stayRoom);
        return false;
    }

    s_roomLoadPending = true;
    s_roomLoadSlowWarning = false;
    s_roomLoadFrames = 0;

    // dStage_RoomCheck(nullptr) consumes mRoomReadId and follows the normal
    // RTBL room-group path: setStayNo, retire rooms outside the group, then
    // create any missing room scenes. It must be called again while those
    // asynchronous scene operations are still settling.
    dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
    dStage_requestRoom(s_saved.room);
    if (!applySavedTransform()) {
        clearRoomLoadRequest();
        snprintf(s_status, sizeof(s_status),
                 "Cannot start room load while Link or camera is unavailable.");
        return false;
    }

    snprintf(s_status, sizeof(s_status), "Loading room %d...",
             (int)s_saved.room);
    Logger::Log("[tphd_tools][coords] native room load started: stage='%s' "
                "active=%d target=%d spawn=%d layer=%d",
                s_saved.stage, (int)stayRoom, (int)s_saved.room,
                (int)s_saved.spawn, (int)s_saved.layer);
    return true;
}

static void tickRoomLoad()
{
    const char* currentStage = dStage_getStageName();
    fopAc_ac_c* link = dComIfGp_getPlayer();
    if (!currentStage || !currentStage[0] ||
        !sameStage(currentStage, s_saved.stage) || !link ||
        dStage_warpPending()) {
        clearRoomLoadRequest();
        snprintf(s_status, sizeof(s_status),
                 "Room load cancelled because the scene changed.");
        Logger::LogWarn("[tphd_tools][coords] native room load cancelled: "
                        "scene no longer available");
        return;
    }

    ++s_roomLoadFrames;
    dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
    dStage_requestRoom(s_saved.room);

    // Room scenes are created asynchronously. Pin Link at the destination with
    // no velocity until daBg_Create registers its collision. Link's normal
    // ground-driven RoomCheck then either finds no polygon or confirms the same
    // target room instead of switching the stay room back to the source.
    dStage_setLinkPos(&s_saved.linkPos, s_saved.linkAngle, s_saved.room);
    daAlink_procWaitInit(link);
    daAlink_clearMomentum(link);
    dCam_snapXform(&s_saved.camAt, &s_saved.camEye);

    if (dStage_getStayRoomNo() == s_saved.room &&
        dStage_isRoomBackgroundReady(s_saved.room)) {
        const unsigned loadFrames = s_roomLoadFrames;
        clearRoomLoadRequest();
        if (!applySavedTransform()) {
            snprintf(s_status, sizeof(s_status),
                     "Room loaded, but Link or camera is unavailable.");
            Logger::LogWarn("[tphd_tools][coords] room %d became ready but "
                            "the saved transform could not be applied",
                            (int)s_saved.room);
            return;
        }

        s_stampRoomNextFrame = true;
        snprintf(s_status, sizeof(s_status), "Loaded saved coordinates.");
        Logger::Log("[tphd_tools][coords] native room ready after %u frames: "
                    "room=%d status=%02X; saved transform applied",
                    loadFrames, (int)s_saved.room,
                    (unsigned)dStage_getRoomStatusFlags(s_saved.room));
        return;
    }

    // Do not abandon Link in an unloaded room on a slow disc/scene creation.
    // Keep waiting safely, but surface a one-time diagnostic if this is unusual.
    if (!s_roomLoadSlowWarning &&
        s_roomLoadFrames >= ROOM_LOAD_SLOW_FRAMES) {
        s_roomLoadSlowWarning = true;
        snprintf(s_status, sizeof(s_status),
                 "Still loading room %d; waiting for collision...",
                 (int)s_saved.room);
        Logger::LogWarn("[tphd_tools][coords] native room load still pending: "
                        "frames=%u stay=%d target=%d status=%02X",
                        s_roomLoadFrames, (int)dStage_getStayRoomNo(),
                        (int)s_saved.room,
                        (unsigned)dStage_getRoomStatusFlags(s_saved.room));
    }
}

static bool saveCoordinates()
{
    if (s_roomLoadPending) {
        snprintf(s_status, sizeof(s_status),
                 "Wait for the current room load to finish.");
        return false;
    }

    fopAc_ac_c* link = dComIfGp_getPlayer();
    CameraXform* cam = dCam_getCurrentXform();
    const char* stage = dStage_getStageName();
    if (!link || !cam || !stage || !stage[0] || dStage_warpPending()) {
        snprintf(s_status, sizeof(s_status),
                 "Cannot save coordinates while the scene is unavailable.");
        return false;
    }

    memset(&s_saved, 0, sizeof(s_saved));
    copyStageName(s_saved.stage, stage);
    s_saved.room = dStage_getStayRoomNo();
    if (s_saved.room < 0 || s_saved.room >= 64)
        s_saved.room = dStage_getRoomNo();
    s_saved.layer = dStage_getLayer();
    s_saved.spawn = dStage_getSpawn();
    s_saved.linkPos = link->current.pos;
    s_saved.linkAngle = link->shape_angle.y;
    s_saved.camAt = cam->at;
    s_saved.camEye = cam->eye;
    s_hasSaved = true;

    snprintf(s_status, sizeof(s_status), "Saved %s room %d, spawn %d, layer %d.",
             s_saved.stage, (int)s_saved.room, (int)s_saved.spawn,
             (int)s_saved.layer);
    Logger::Log("[tphd_tools][coords] saved: stage='%s' room=%d spawn=%d "
                "layer=%d link=(%.1f,%.1f,%.1f) angle=%d "
                "camAt=(%.1f,%.1f,%.1f) camEye=(%.1f,%.1f,%.1f)",
                s_saved.stage, (int)s_saved.room, (int)s_saved.spawn,
                (int)s_saved.layer, s_saved.linkPos.x, s_saved.linkPos.y,
                s_saved.linkPos.z, (int)s_saved.linkAngle, s_saved.camAt.x,
                s_saved.camAt.y, s_saved.camAt.z, s_saved.camEye.x,
                s_saved.camEye.y, s_saved.camEye.z);
    return true;
}

static bool loadCoordinates()
{
    if (s_roomLoadPending) {
        snprintf(s_status, sizeof(s_status),
                 "Wait for the current room load to finish.");
        return false;
    }

    if (!s_hasSaved) {
        snprintf(s_status, sizeof(s_status), "No coordinates have been saved.");
        return false;
    }

    const char* currentStage = dStage_getStageName();
    fopAc_ac_c* link = dComIfGp_getPlayer();
    CameraXform* cam = dCam_getCurrentXform();
    if (!link || !cam || !currentStage || !currentStage[0] ||
        dStage_warpPending()) {
        snprintf(s_status, sizeof(s_status),
                 "Cannot load coordinates while the scene is unavailable.");
        return false;
    }
    if (!sameStage(currentStage, s_saved.stage)) {
        char stage[9];
        copyStageName(stage, currentStage);
        snprintf(s_status, sizeof(s_status),
                 "Stage mismatch: saved %s, current %s.", s_saved.stage, stage);
        Logger::LogWarn("[tphd_tools][coords] load rejected: saved stage='%s' "
                        "current stage='%s'", s_saved.stage, stage);
        return false;
    }

    // A flying camera owns the camera transform and freeze flags every frame.
    // Release it before direct placement.
    FlyCam::Stop();

    const s8 stayRoom = dStage_getStayRoomNo();
    if (stayRoom != s_saved.room ||
        !dStage_isRoomBackgroundReady(s_saved.room))
        return beginRoomLoad();

    if (!applySavedTransform()) {
        snprintf(s_status, sizeof(s_status),
                 "Cannot load coordinates while Link or camera is unavailable.");
        return false;
    }
    s_stampRoomNextFrame = true;
    snprintf(s_status, sizeof(s_status), "Loaded saved coordinates.");
    Logger::Log("[tphd_tools][coords] loaded in place: stage='%s' room=%d "
                "spawn=%d layer=%d action=%u; room re-stamp armed",
                s_saved.stage, (int)s_saved.room, (int)s_saved.spawn,
                (int)s_saved.layer, (unsigned)DAALINK_ACTION_WAIT);
    return true;
}

void DrawMenuItem()
{
    bool enabled = s_enabled;
    if (ImGui::Checkbox("Save/Load Coordinates", &enabled))
        SetEnabled(enabled);
    if (!s_enabled)
        return;

    char saveHotkey[64];
    char loadHotkey[64];
    Input::HotkeyToString(ov::g_settings.saveCoordinatesCombo,
                          saveHotkey, sizeof(saveHotkey));
    Input::HotkeyToString(ov::g_settings.loadCoordinatesCombo,
                          loadHotkey, sizeof(loadHotkey));

    ImGui::Indent();
    ImGui::TextDisabled("Save: %s", saveHotkey);
    ImGui::TextDisabled("Load: %s", loadHotkey);
    if (ImGui::Button("Save coordinates"))
        saveCoordinates();
    ImGui::SameLine();
    if (ImGui::Button("Load coordinates"))
        loadCoordinates();
    if (s_hasSaved) {
        ImGui::TextDisabled("Slot: %s  room %d  spawn %d  layer %d",
                            s_saved.stage, (int)s_saved.room,
                            (int)s_saved.spawn, (int)s_saved.layer);
    } else {
        ImGui::TextDisabled("Slot: empty");
    }
    if (s_status[0])
        ImGui::TextWrapped("%s", s_status);
    ImGui::Unindent();
}

void Tick()
{
    Input::SetCoordinateHotkeysArmed(s_enabled);
    if (!s_enabled) {
        s_stampRoomNextFrame = false;
        clearRoomLoadRequest();
        return;
    }

    if (s_roomLoadPending) {
        tickRoomLoad();
        return;
    }

    // The game can recompute Link's room from his outgoing frame after a large
    // in-place teleport. Re-stamp the live descriptor and actor room once on the
    // following frame, after that update has completed.
    if (s_stampRoomNextFrame) {
        dStage_setCurrentInfo(s_saved.room, s_saved.spawn, s_saved.layer);
        dStage_setLinkRoom(s_saved.room);
        fopAc_ac_c* link = dComIfGp_getPlayer();
        if (link) {
            daAlink_procWaitInit(link);
            daAlink_clearMomentum(link);
        }
        dCam_snapXform(&s_saved.camAt, &s_saved.camEye);
        s_stampRoomNextFrame = false;
        Logger::Log("[tphd_tools][coords] next-frame re-stamp: room=%d "
                    "spawn=%d layer=%d action=%u", (int)s_saved.room,
                    (int)s_saved.spawn, (int)s_saved.layer,
                    (unsigned)DAALINK_ACTION_WAIT);
    }

    if (Input::SaveCoordinatesHotkeyFired())
        saveCoordinates();
    if (Input::LoadCoordinatesHotkeyFired())
        loadCoordinates();

}

void OnApplicationStart()
{
    Input::SetCoordinateHotkeysArmed(false);
    s_stampRoomNextFrame = false;
    clearRoomLoadRequest();
    s_status[0] = '\0';
}

bool IsEnabled() { return s_enabled; }
void SetEnabled(bool enabled)
{
    if (!enabled && s_roomLoadPending) {
        snprintf(s_status, sizeof(s_status),
                 "Wait for the current room load to finish.");
        return;
    }

    s_enabled = enabled;
    if (!enabled) {
        Input::SetCoordinateHotkeysArmed(false);
        s_stampRoomNextFrame = false;
    }
}

bool GetSavedCoordinates(SavedCoordinates* out)
{
    if (!s_hasSaved)
        return false;
    if (out)
        *out = s_saved;
    return true;
}

void SetSavedCoordinates(const SavedCoordinates* saved)
{
    if (!saved || !saved->stage[0]) {
        memset(&s_saved, 0, sizeof(s_saved));
        s_hasSaved = false;
        return;
    }
    s_saved = *saved;
    s_saved.stage[sizeof(s_saved.stage) - 1] = '\0';
    s_hasSaved = true;
}

} // namespace SaveLoadCoords
} // namespace Tools
