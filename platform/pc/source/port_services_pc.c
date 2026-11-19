/* PC versions of the 3DS services the bottom-screen UI (port/ui/port_bottom_ui.c)
 * calls. The declarations are the 3DS headers in platform/3ds/source.
 *
 * - Settings (Port_Config_*) are kept in memory so the Options tab responds;
 *   they are not saved, and the 3DS display ones change nothing here.
 * - Save states, RetroAchievements, the updater, FTP and the DSP firmware
 *   prompt do not exist on PC: they report "not available" / idle, so the UI
 *   shows them disabled or empty instead of acting.
 * - The gameplay helpers (auto bomb, ...) keep their switch; the 3DS hooks
 *   that use them are not part of this build.
 *
 * A Wii U frontend can start from this file and replace stubs one by one. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "port_ui_platform.h"
#include "port_save_state.h"
#include "port_updater_3ds.h"
#include "port_retroachievements_3ds.h"
#include "port_dsp_firm_prompt_3ds.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

/* ---- port_ui_platform.h ------------------------------------------------------ */

uint64_t PortUi_NowMs(void) { return SDL_GetTicks64(); }

bool PortUi_PollBattery(uint8_t* level, bool* charging) {
    int percent = -1;
    SDL_PowerState state = SDL_GetPowerInfo(NULL, &percent);
    if (state == SDL_POWERSTATE_UNKNOWN || state == SDL_POWERSTATE_NO_BATTERY || percent < 0) return false;
    *level = (uint8_t)((percent + 10) / 20 > 5 ? 5 : (percent + 10) / 20);
    *charging = (state == SDL_POWERSTATE_CHARGING);
    return true;
}

int PortUi_WifiStrength(void) { return 3; }

bool PortUi_LocalTime(time_t t, struct tm* out) {
#ifdef _WIN32
    return localtime_s(out, &t) == 0;
#else
    return localtime_r(&t, out) != NULL;
#endif
}

/* ---- System / renderer ---------------------------------------------------------- */

bool Platform3DS_IsNew3DS(void) { return true; }
double Port_PPU_3DS_CurrentFps(void) { return 60.0; }
bool Port_PPU_3DS_LastFrameUsedGpu(void) { return false; }
bool PlatformGpu3DS_IsRecording(void) { return false; }
bool PlatformGpu3DS_IsPerfRecording(void) { return false; }
uint32_t PlatformGpu3DS_LastRecTickUs(void) { return 0; }
bool PlatformGpu3DS_RecordingToRam(void) { return false; }

/* ---- Settings ------------------------------------------------------------------- */

static int sFpsPosition, sGbaFxGrade, sGbaFxGrid, sGbaFxVignette, sFramePacing, sCstickMode;
static int sAspectRatio = 1, sDisplayStyle;
static bool sAutoHideHud, sHideSpoilers, sHudOutside, sGbaBezel, sWideAi, sWideAiAsked = true;
#define BUTTON_SLOTS 16
static int sButtonMap[BUTTON_SLOTS];

void Port_Config_Save(void) {}
bool Port_Config_GetShowFps(void) { return sFpsPosition != 0; }
int Port_Config_GetFpsPosition(void) { return sFpsPosition; }
void Port_Config_CycleFpsPosition(void) { sFpsPosition = (sFpsPosition + 1) % 5; }
bool Port_Config_GetAutoHideHud(void) { return sAutoHideHud; }
void Port_Config_SetAutoHideHud(bool on) { sAutoHideHud = on; }
bool Port_Config_GetHideSpoilers(void) { return sHideSpoilers; }
void Port_Config_SetHideSpoilers(bool on) { sHideSpoilers = on; }
bool Port_Config_GetHudOutside(void) { return sHudOutside; }
void Port_Config_ToggleHudOutside(void) { sHudOutside = !sHudOutside; }
bool Port_Config_GetGbaBezel(void) { return sGbaBezel; }
void Port_Config_ToggleGbaBezel(void) { sGbaBezel = !sGbaBezel; }
int Port_Config_GetGbaFxGrade(void) { return sGbaFxGrade; }
void Port_Config_SetGbaFxGrade(int level) { if (level >= 0 && level < 4) sGbaFxGrade = level; }
int Port_Config_GetGbaFxGrid(void) { return sGbaFxGrid; }
void Port_Config_SetGbaFxGrid(int level) { if (level >= 0 && level < 4) sGbaFxGrid = level; }
int Port_Config_GetGbaFxVignette(void) { return sGbaFxVignette; }
void Port_Config_SetGbaFxVignette(int level) { if (level >= 0 && level < 4) sGbaFxVignette = level; }
int Port_Config_GetFramePacing(void) { return sFramePacing; }
void Port_Config_SetFramePacing(int mode) { if (mode >= 0 && mode < 2) sFramePacing = mode; }
int Port_Config_GetCstickMode(void) { return sCstickMode; }
void Port_Config_SetCstickMode(int mode) { if (mode >= 0 && mode < 4) sCstickMode = mode; }
int Port_Config_Get3DSAspectRatio(void) { return sAspectRatio; }
void Port_Config_Cycle3DSAspectRatio(void) { sAspectRatio = sAspectRatio == 3 ? 1 : 3; }
int Port_Config_Get3DSDisplayStyle(void) { return sDisplayStyle; }
void Port_Config_Cycle3DSDisplayStyle(void) { sDisplayStyle = (sDisplayStyle + 1) % 3; }
bool Port_Config_GetWideAi(void) { return sAspectRatio == 3 && sWideAi; }
bool Port_Config_WideAiQuestionDue(void) { return sAspectRatio == 3 && !sWideAiAsked; }
void Port_Config_AnswerWideAi(bool yes) { sWideAi = yes; sWideAiAsked = true; }

int Port_Config_GetButtonMapping(int buttonIndex) {
    return (buttonIndex >= 0 && buttonIndex < BUTTON_SLOTS) ? sButtonMap[buttonIndex] : 0;
}
void Port_Config_SetButtonMapping(int buttonIndex, int action) {
    if (buttonIndex >= 0 && buttonIndex < BUTTON_SLOTS) sButtonMap[buttonIndex] = action;
}
void Port_Config_ResetButtonMappingDefault(void) { memset(sButtonMap, 0, sizeof(sButtonMap)); }
const char* Port_Config_GetActionName(int action, int lang) {
    static char name[16];
    (void)lang;
    snprintf(name, sizeof(name), "ACTION %d", action);
    return name;
}

/* ---- Gameplay helpers -------------------------------------------------------------- */

static bool sAutoBomb, sAutoSpaceJump, sChargePull;
bool Port_AutoBomb_Enabled(void) { return sAutoBomb; }
void Port_AutoBomb_SetEnabled(bool on) { sAutoBomb = on; }
bool Port_AutoSpaceJump_Enabled(void) { return sAutoSpaceJump; }
void Port_AutoSpaceJump_SetEnabled(bool on) { sAutoSpaceJump = on; }
bool Port_ChargePull_Enabled(void) { return sChargePull; }
void Port_ChargePull_SetEnabled(bool on) { sChargePull = on; }

/* ---- Save states: not available -------------------------------------------------- */

bool Port_SaveState_Available(void) { return false; }
void Port_SaveState_RequestScan(void) {}
void Port_SaveState_RequestSaveNew(void) {}
void Port_SaveState_RequestOverwrite(uint32_t id) { (void)id; }
void Port_SaveState_RequestLoad(uint32_t id) { (void)id; }
void Port_SaveState_RequestDelete(uint32_t id) { (void)id; }
void Port_SaveState_RequestThumb(uint32_t id) { (void)id; }
PortSaveStateJob Port_SaveState_CurrentJob(void) { return PORT_SS_JOB_NONE; }
bool Port_SaveState_IsBusy(void) { return false; }
bool Port_SaveState_Scanned(void) { return true; }
int Port_SaveState_Count(void) { return 0; }
bool Port_SaveState_GetInfoAt(int index, PortSaveStateInfo* out) { (void)index; (void)out; return false; }
const char* Port_SaveState_AreaName(unsigned area) { (void)area; return ""; }
const char* Port_SaveState_LastMessage(void) { return ""; }
int Port_SaveState_MessageTtl(void) { return 0; }
const uint16_t* Port_SaveState_TakeThumb(uint32_t* outId) { (void)outId; return NULL; }
bool PortStateThumb_HasImage(void) { return false; }
void PortStateThumb_Set(const uint16_t* pixels) { (void)pixels; }
void PortStateThumb_Draw(float x, float y, float w, float h, float depth) {
    (void)x; (void)y; (void)w; (void)h; (void)depth;
}

/* ---- RetroAchievements: disabled ----------------------------------------------------- */

static RetroAchievementSort sRaSort = RA_SORT_DEFAULT;
static bool sRaDescending, sRaSound = true, sRaOnTop = true;
static uint32_t sRaSubset;

bool Port_RA_IsEnabled(void) { return false; }
void Port_RA_SetEnabled(bool enabled) { (void)enabled; }
bool Port_RA_IsHardcore(void) { return false; }
void Port_RA_SetHardcore(bool hardcore) { (void)hardcore; }
RetroAchievementsStatus Port_RA_GetStatus(void) { return RA_STATUS_DISABLED; }
const char* Port_RA_GetStatusString(int lang) { (void)lang; return "NOT AVAILABLE ON PC"; }
const char* Port_RA_GetUsername(void) { return ""; }
const char* Port_RA_GetLastDebugLog(void) { return ""; }
void Port_RA_PromptLogin(void) {}
void Port_RA_Update(void) {}
void Port_RA_RenderToastOverlay(void) {}
void Port_RA_ShowPreviewToast(void) {}
uint32_t Port_RA_GetAchievementCount(void) { return 0; }
uint32_t Port_RA_GetUnlockedCount(void) { return 0; }
uint32_t Port_RA_GetHardcoreUnlockedCount(void) { return 0; }
uint32_t Port_RA_GetViewCount(void) { return 0; }
const RetroAchievementItem* Port_RA_GetViewAchievement(uint32_t index) { (void)index; return NULL; }
uint32_t Port_RA_GetSubsetCount(void) { return 0; }
const RetroAchievementSubset* Port_RA_GetSubset(uint32_t index) { (void)index; return NULL; }
const uint32_t* Port_RA_GetBadgePixels(const char* badgeName) { (void)badgeName; return NULL; }
RetroAchievementSort Port_RA_GetListSort(void) { return sRaSort; }
void Port_RA_SetListSort(RetroAchievementSort sort) { sRaSort = sort; }
bool Port_RA_GetListDescending(void) { return sRaDescending; }
void Port_RA_SetListDescending(bool descending) { sRaDescending = descending; }
uint32_t Port_RA_GetListSubset(void) { return sRaSubset; }
void Port_RA_SetListSubset(uint32_t subset) { sRaSubset = subset; }
bool Port_RA_GetNotificationSound(void) { return sRaSound; }
void Port_RA_SetNotificationSound(bool sound) { sRaSound = sound; }
bool Port_RA_GetNotifyOnTopScreen(void) { return sRaOnTop; }
void Port_RA_SetNotifyOnTopScreen(bool onTop) { sRaOnTop = onTop; }

/* ---- Updater: idle ------------------------------------------------------------------ */

UpdaterPrompt Port_Updater_GetPrompt(void) { return UPDATER_PROMPT_NONE; }
bool Port_Updater_KeptCia(void) { return false; }
void Port_Updater_AnswerPrompt(bool yes) { (void)yes; }
void Port_Updater_CheckNow(void) {}
void Port_Updater_Install(void) {}
void Port_Updater_Restart(void) {}
UpdaterState Port_Updater_GetState(void) { return UPDATER_IDLE; }
int Port_Updater_GetProgress(void) { return 0; }
const char* Port_Updater_GetRemoteTag(void) { return ""; }
const char* Port_Updater_GetMessage(void) { return "NOT AVAILABLE ON PC"; }
size_t Port_Updater_CopyNotes(char* out, size_t cap) {
    if (cap) out[0] = '\0';
    return 0;
}
bool Port_Updater_GetAuto(void) { return false; }
void Port_Updater_SetAuto(bool enabled) { (void)enabled; }
bool Port_Updater_GetBeta(void) { return false; }
void Port_Updater_SetBeta(bool enabled) { (void)enabled; }

/* ---- FTP install server and DSP firmware prompt: not on PC ----------------------------- */

bool FtpServer_InstallOnly(void) { return false; }
void FtpServer_Address(char* out, unsigned cap) { if (cap) out[0] = '\0'; }
void FtpServer_Cancel(void) {}
const char* FtpServer_Notice(void) { return NULL; } /* NULL: no standing notice */
int FtpServer_Progress(unsigned long long* done, unsigned long long* total) {
    if (done) *done = 0;
    if (total) *total = 0;
    return 0;
}

int Port_DspFirm_State(void) { return DSPFIRM_PROMPT_NONE; }
bool Port_DspFirm_Missing(void) { return false; }
const char* Port_DspFirm_Failure(void) { return ""; }
void Port_DspFirm_Answer(bool yes) { (void)yes; }
void Port_DspFirm_Pump(void) {}
