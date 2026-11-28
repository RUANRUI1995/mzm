/* The Wii U's self-updater: the same releases endpoint, notes and prompts as the 3DS one
 * (platform/3ds/source/port_updater_3ds.c), with the Wii U's own release tags and asset
 * (port_updater_parse.c: "wiiu-vX.Y.Z" and the general "vX.Y.Z", the .wuhb). The WUHB is
 * downloaded next to the one Aroma runs (WIIU_DATA_DIR, the folder of the ROM and the save)
 * and put in its place once whole; the new version runs from the next launch. */
#ifdef __WIIU__

#include <SDL.h>
#include <curl/curl.h>
#include <nn/ac.h>
#include <sysapp/launch.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_updater_3ds.h"
#include "port_updater_parse.h"
#include "wiiu_support.h"

#ifndef MZM_PORT_VERSION
#define MZM_PORT_VERSION "v0.0.0"
#endif

#define UPDATER_DEFAULT_URL "https://api.github.com/repos/RUANRUI1995/mzm/releases?per_page=8"
#define UPDATER_URL_MAX 200
#define UPDATER_WUHB_PATH WIIU_DATA_DIR "/mzm-wiiu.wuhb"
#define UPDATER_PART_PATH WIIU_DATA_DIR "/mzm-wiiu.wuhb.part"
#define UPDATER_OLD_PATH WIIU_DATA_DIR "/mzm-wiiu.wuhb.old"
#define UPDATER_SETTINGS_PATH WIIU_DATA_DIR "/updater.ini"
#define UPDATER_JSON_MAX (192 * 1024)
#define UPDATER_CHUNK (64 * 1024)
#define UPDATER_FILE_BUF (256 * 1024)
#define UPDATER_MSG_MAX 64
#define UPDATER_NOTES_MAX 6144

typedef enum { JOB_CHECK, JOB_AUTO_CHECK, JOB_INSTALL } UpdaterJob;

static volatile UpdaterState sState = UPDATER_IDLE;
static volatile int sProgress = 0;
static volatile bool sBusy = false;
static volatile UpdaterPrompt sPrompt = UPDATER_PROMPT_NONE;
static bool sAuto = true;
static bool sBeta = MZM_PORT_IS_BETA; /* a beta build follows the betas until told otherwise */
static char sUrlOverride[UPDATER_URL_MAX] = "";
static char sRemoteTag[32] = "";
static char sNotes[UPDATER_NOTES_MAX] = ""; /* under sTextLock */
static char sMessage[96] = "";
static UpdaterRelease sRelease;
static SDL_mutex* sTextLock;
static bool sNetUp = false;

static void Lock(void) {
    if (!sTextLock) sTextLock = SDL_CreateMutex();
    if (sTextLock) SDL_LockMutex(sTextLock);
}

static void Unlock(void) {
    if (sTextLock) SDL_UnlockMutex(sTextLock);
}

static void SetMessage(const char* msg) {
    Lock();
    snprintf(sMessage, sizeof(sMessage), "%s", msg ? msg : "");
    Unlock();
}

static void Fail(const char* msg) {
    SetMessage(msg);
    sState = UPDATER_ERROR;
}

/* ---- Settings: auto check, betas and the endpoint, kept on the SD card ------------------- */

static void SaveSettings(void) {
    FILE* f = fopen(UPDATER_SETTINGS_PATH, "w");
    if (!f) return;
    fprintf(f, "auto=%d\nbeta=%d\n", sAuto ? 1 : 0, sBeta ? 1 : 0);
    if (sUrlOverride[0]) fprintf(f, "update_url=%s\n", sUrlOverride);
    fclose(f);
}

static void LoadSettings(void) {
    char line[UPDATER_URL_MAX + 16];
    FILE* f = fopen(UPDATER_SETTINGS_PATH, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "auto=", 5) == 0) sAuto = atoi(line + 5) != 0;
        else if (strncmp(line, "beta=", 5) == 0) sBeta = atoi(line + 5) != 0;
        else if (strncmp(line, "update_url=", 11) == 0) snprintf(sUrlOverride, sizeof(sUrlOverride), "%s", line + 11);
    }
    fclose(f);
}

/* ---- HTTP (libcurl + mbedtls, as on the 3DS) --------------------------------------------- */

typedef struct {
    bool (*begin)(void* user, uint32_t total); /* the final 200 response; false aborts */
    bool (*data)(void* user, const uint8_t* buf, uint32_t size);
    void* user;
} HttpSink;

typedef struct {
    const HttpSink* sink;
    CURL* curl;
    bool started;
    bool aborted;
} CurlCtx;

static size_t CurlWrite(char* ptr, size_t size, size_t nmemb, void* userdata) {
    CurlCtx* c = (CurlCtx*)userdata;
    size_t n = size * nmemb;

    if (!c->started) {
        long status = 0;
        curl_off_t total = -1;

        curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status != 200) return n; /* an error page's body */
        curl_easy_getinfo(c->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &total);
        c->started = true;
        if (c->sink->begin && !c->sink->begin(c->sink->user, total > 0 ? (uint32_t)total : 0)) {
            c->aborted = true;
            return 0;
        }
    }
    if (!c->sink->data(c->sink->user, (const uint8_t*)ptr, (uint32_t)n)) {
        c->aborted = true;
        return 0;
    }
    return n;
}

static bool NetUp(void) {
    if (!sNetUp) {
        /* The console's own network connection (the system settings'): without it the
         * sockets have nowhere to go. */
        ACInitialize();
        ACConnect();
        curl_global_init(CURL_GLOBAL_DEFAULT);
        sNetUp = true;
    }
    return true;
}

static void NetDown(void) {
    if (sNetUp) {
        curl_global_cleanup();
        ACFinalize();
        sNetUp = false;
    }
}

static int HttpGet(const char* url, const HttpSink* sink) {
    CurlCtx ctx;
    CURL* curl;
    CURLcode cc;
    long status = 0;
    char buf[UPDATER_MSG_MAX];

    NetUp();
    curl = curl_easy_init();
    if (!curl) {
        Fail("CURL INIT");
        return -1;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.sink = sink;
    ctx.curl = curl;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "mzm-wiiu-updater/" MZM_PORT_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    /* No CA bundle on the console, as on the 3DS: the download is a release asset the
     * player chose to install from this repository's page. */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)UPDATER_CHUNK);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (ctx.aborted) return -2; /* the sink already said why */
    if (cc != CURLE_OK) {
        snprintf(buf, sizeof(buf), "CURL %d %s", (int)cc, curl_easy_strerror(cc));
        Fail(buf);
        return -3;
    }
    if (status != 200) {
        snprintf(buf, sizeof(buf), "HTTP %ld", status);
        Fail(buf);
        return -4;
    }
    return 0;
}

/* ---- Sinks ------------------------------------------------------------------------------- */

typedef struct {
    char* buf;
    uint32_t len;
} JsonSink;

static bool JsonData(void* user, const uint8_t* data, uint32_t size) {
    JsonSink* s = (JsonSink*)user;
    uint32_t room = UPDATER_JSON_MAX - 1 - s->len;
    if (size > room) size = room; /* the releases we want are at the top */
    memcpy(s->buf + s->len, data, size);
    s->len += size;
    s->buf[s->len] = '\0';
    return true;
}

typedef struct {
    FILE* file;
    uint32_t total;
    uint32_t written;
} FileSink;

static bool FileBegin(void* user, uint32_t total) {
    FileSink* s = (FileSink*)user;
    if (total == 0) {
        Fail("NO CONTENT-LENGTH");
        return false;
    }
    s->total = total;
    s->file = fopen(UPDATER_PART_PATH, "wb");
    if (!s->file) {
        Fail("CANNOT WRITE SD");
        return false;
    }
    setvbuf(s->file, NULL, _IOFBF, UPDATER_FILE_BUF);
    return true;
}

static bool FileData(void* user, const uint8_t* data, uint32_t size) {
    FileSink* s = (FileSink*)user;
    if (fwrite(data, 1, size, s->file) != size) {
        Fail("SD FULL?");
        return false;
    }
    s->written += size;
    sProgress = (int)(((uint64_t)s->written * 95) / s->total);
    return true;
}

/* ---- Worker ------------------------------------------------------------------------------ */

static bool DoCheck(void) {
    JsonSink js;
    HttpSink sink = { NULL, JsonData, &js };
    UpdaterRelease rel;
    char* notes;

    js.buf = (char*)malloc(UPDATER_JSON_MAX);
    js.len = 0;
    if (!js.buf) {
        Fail("OUT OF MEMORY");
        return false;
    }
    js.buf[0] = '\0';

    sState = UPDATER_CHECKING;
    SetMessage("");
    if (HttpGet(sUrlOverride[0] ? sUrlOverride : UPDATER_DEFAULT_URL, &sink) != 0) {
        free(js.buf);
        return false;
    }
    if (!Updater_PickRelease(js.buf, sBeta, &rel)) {
        free(js.buf);
        Fail("NO RELEASE FOUND");
        return false;
    }
    notes = (char*)malloc(UPDATER_NOTES_MAX);
    if (notes && Updater_CollectNotes(js.buf, sBeta, MZM_PORT_VERSION, MZM_PORT_IS_BETA, notes, UPDATER_NOTES_MAX) == 0)
        Updater_CollectNotes(js.buf, sBeta, "v0.0.0", false, notes, UPDATER_NOTES_MAX);
    free(js.buf);

    Lock();
    snprintf(sRemoteTag, sizeof(sRemoteTag), "%s%s", rel.tag, rel.prerelease ? " BETA" : "");
    snprintf(sNotes, sizeof(sNotes), "%s", notes ? notes : "");
    Unlock();
    free(notes);

    if (!Updater_IsNewerBuild(MZM_PORT_VERSION, MZM_PORT_IS_BETA, rel.tag, rel.prerelease)) {
        sState = UPDATER_UP_TO_DATE;
        return false;
    }
    sRelease = rel;
    sState = UPDATER_AVAILABLE;
    return true;
}

static void DoInstall(void) {
    FileSink fs;
    HttpSink sink = { FileBegin, FileData, &fs };
    int rc;

    memset(&fs, 0, sizeof(fs));
    sProgress = 0;
    sState = UPDATER_DOWNLOADING;
    SetMessage("");

    rc = HttpGet(sRelease.cia_url, &sink);
    if (fs.file) fclose(fs.file);
    if (rc != 0 || fs.written != fs.total) {
        remove(UPDATER_PART_PATH);
        if (sState != UPDATER_ERROR) Fail("DOWNLOAD CUT");
        return;
    }
    /* The whole file is there: it takes the old one's place (kept as .old until then, so a
     * failed rename leaves something that runs). */
    remove(UPDATER_OLD_PATH);
    rename(UPDATER_WUHB_PATH, UPDATER_OLD_PATH);
    if (rename(UPDATER_PART_PATH, UPDATER_WUHB_PATH) != 0) {
        rename(UPDATER_OLD_PATH, UPDATER_WUHB_PATH);
        Fail("CANNOT REPLACE WUHB");
        return;
    }
    remove(UPDATER_OLD_PATH);
    sProgress = 100;
    sState = UPDATER_INSTALLED;
    sPrompt = UPDATER_PROMPT_ASK_RESTART;
}

static int WorkerMain(void* arg) {
    UpdaterJob job = (UpdaterJob)(uintptr_t)arg;

    if (job == JOB_INSTALL) {
        DoInstall();
    } else if (DoCheck() && job == JOB_AUTO_CHECK) {
        sPrompt = UPDATER_PROMPT_ASK_INSTALL;
    }
    /* Only an install the player is watching reports its failure in the prompt. */
    if (sState == UPDATER_ERROR && sPrompt == UPDATER_PROMPT_PROGRESS) sPrompt = UPDATER_PROMPT_ERROR;
    NetDown();
    sBusy = false;
    return 0;
}

static void StartJob(UpdaterJob job) {
    SDL_Thread* t;
    if (sBusy) return;
    sBusy = true;
    t = SDL_CreateThread(WorkerMain, "updater", (void*)(uintptr_t)job);
    if (!t) {
        sBusy = false;
        Fail("THREAD");
        return;
    }
    SDL_DetachThread(t);
}

/* ---- Public API (port_updater_3ds.h) ----------------------------------------------------- */

void Port_Updater_Init(void) {
    LoadSettings();
    if (sAuto) StartJob(JOB_AUTO_CHECK);
}

void Port_Updater_CheckNow(void) { StartJob(JOB_CHECK); }

void Port_Updater_Install(void) {
    if (sState == UPDATER_AVAILABLE) StartJob(JOB_INSTALL);
}

/* Back to the Wii U menu: Aroma starts the new WUHB from there. */
void Port_Updater_Restart(void) { SYSLaunchMenu(); }

UpdaterState Port_Updater_GetState(void) { return sState; }
UpdaterPrompt Port_Updater_GetPrompt(void) { return sPrompt; }
bool Port_Updater_KeptCia(void) { return false; }

void Port_Updater_AnswerPrompt(bool yes) {
    switch (sPrompt) {
        case UPDATER_PROMPT_ASK_INSTALL:
            if (yes) {
                sPrompt = UPDATER_PROMPT_PROGRESS;
                Port_Updater_Install();
            } else {
                sPrompt = UPDATER_PROMPT_NONE;
            }
            break;
        case UPDATER_PROMPT_ASK_RESTART:
            if (yes) Port_Updater_Restart();
            else sPrompt = UPDATER_PROMPT_NONE;
            break;
        case UPDATER_PROMPT_ERROR:
            sPrompt = UPDATER_PROMPT_NONE;
            break;
        default:
            break;
    }
}

int Port_Updater_GetProgress(void) { return sProgress; }
const char* Port_Updater_GetRemoteTag(void) { return sRemoteTag; }
const char* Port_Updater_GetMessage(void) { return sMessage; }

size_t Port_Updater_CopyNotes(char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    Lock();
    snprintf(out, cap, "%s", sNotes);
    Unlock();
    return strlen(out);
}

bool Port_Updater_GetAuto(void) { return sAuto; }
void Port_Updater_SetAuto(bool enabled) {
    sAuto = enabled;
    SaveSettings();
}
bool Port_Updater_GetBeta(void) { return sBeta; }
void Port_Updater_SetBeta(bool enabled) {
    sBeta = enabled;
    SaveSettings();
}
const char* Port_Updater_GetUrlOverride(void) { return sUrlOverride; }
void Port_Updater_SetUrlOverride(const char* url) {
    snprintf(sUrlOverride, sizeof(sUrlOverride), "%s", url ? url : "");
    SaveSettings();
}

#endif /* __WIIU__ */
