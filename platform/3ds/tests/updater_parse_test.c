#include <stdio.h>
#include <string.h>

#include "port_updater_parse.h"

static int sFailures;

#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); sFailures++; } } while (0)

/* Newest first, like the GitHub API: a beta, a stable, an older stable. */
static const char kJson[] =
    "[{\"tag_name\":\"v0.7.0\",\"prerelease\":true,\"assets\":["
    "{\"name\":\"a.zip\",\"browser_download_url\":\"http://h/a.zip\"},"
    "{\"name\":\"mzm.cia\",\"browser_download_url\":\"http://h/beta.cia\"}]},"
    "{\"tag_name\":\"v0.6.5\",\"prerelease\":false,\"assets\":["
    "{\"browser_download_url\":\"http://h/stable.cia\"}]},"
    "{\"tag_name\":\"v0.6.4\",\"prerelease\": false,\"assets\":["
    "{\"browser_download_url\":\"http://h/old.cia\"}]}]";

/* Bodies as GitHub returns them: markdown JSON-escaped, notes block between
 * the markers, the generated changelog after it. */
static const char kNotesJson[] =
    "[{\"tag_name\":\"v0.7.2\",\"prerelease\":true,\"assets\":[],"
    "\"body\":\"> **Beta build.**\\n\\n<!-- mzm-notes -->\\n## What's new\\n\\n"
    "- **Fixed** the `SETTINGS` button\\r\\n* Caf\\u00e9 \\\"quoted\\\" text\\n"
    "<!-- /mzm-notes -->\\n\\n## Changelog\\n- fix: something (abc)\"},"
    "{\"tag_name\":\"v0.7.1\",\"prerelease\":false,\"assets\":[],"
    "\"body\":\"<!-- mzm-notes -->\\n- Stable change\\n<!-- /mzm-notes -->\"},"
    "{\"tag_name\":\"v0.7.0\",\"prerelease\":false,\"assets\":[],\"body\":\"no block here\"},"
    "{\"tag_name\":\"v0.6.9\",\"prerelease\":false,\"assets\":[],\"body\":null}]";

static void TestChannels(void) {
    char out[1024];

    /* A beta of 0.7.2 sees the stable 0.7.2 as newer, but not a beta page of itself. */
    CHECK(Updater_IsNewerBuild("v0.7.2", true, "v0.7.2", false));
    CHECK(!Updater_IsNewerBuild("v0.7.2", true, "v0.7.2", true));
    CHECK(Updater_IsNewerBuild("v0.7.2", true, "v0.7.3", true));
    /* A stable build never moves to a same-number release, whatever its page says. */
    CHECK(!Updater_IsNewerBuild("v0.7.2", false, "v0.7.2", false));
    CHECK(!Updater_IsNewerBuild("v0.7.2", false, "v0.7.2", true));
    CHECK(Updater_IsNewerBuild("v0.7.2", false, "v0.7.3", false));
    /* Never goes backwards, and dev builds keep their own rule. */
    CHECK(!Updater_IsNewerBuild("v0.7.2", true, "v0.7.1", false));
    CHECK(Updater_IsNewerBuild("v0.7.2-dev.1+abc", false, "v0.7.2", false));
    CHECK(Updater_IsNewerBuild("v0.7.2-dev.1+abc", false, "v0.7.2", true)); /* dev: any release of its number */
    CHECK(!Updater_IsNewerBuild("garbage", true, "v0.7.2", false));

    /* The notes of the promoted stable page reach a beta of the same number. */
    CHECK(Updater_CollectNotes(kNotesJson, true, "v0.7.1", true, out, sizeof(out)) == 2);
    CHECK(Updater_CollectNotes(kNotesJson, true, "v0.7.1", false, out, sizeof(out)) == 1);
    CHECK(Updater_CollectNotes(kNotesJson, false, "v0.7.1", true, out, sizeof(out)) == 1);
}

static void TestNotes(void) {
    char out[1024];
    int n;

    /* From v0.7.0 with betas: the two newer releases, newest first. */
    n = Updater_CollectNotes(kNotesJson, true, "v0.7.0", false, out, sizeof(out));
    CHECK(n == 2);
    CHECK(strncmp(out, "== v0.7.2 ==\n", 13) == 0);
    CHECK(strstr(out, "- Fixed the SETTINGS button\n") != NULL);
    CHECK(strstr(out, "- Caf\xc3\xa9 \"quoted\" text\n") != NULL);
    CHECK(strstr(out, "What's new") == NULL);        /* heading dropped */
    CHECK(strstr(out, "Changelog") == NULL);         /* outside the block */
    CHECK(strstr(out, "Beta build") == NULL);
    CHECK(strstr(out, "\n== v0.7.1 ==\n- Stable change\n") != NULL);
    CHECK(strstr(out, "v0.7.0") == NULL);            /* not newer than current */

    /* Stable channel skips the beta. */
    n = Updater_CollectNotes(kNotesJson, false, "v0.7.0", false, out, sizeof(out));
    CHECK(n == 1);
    CHECK(strstr(out, "v0.7.2") == NULL);

    /* A far older install sees everything newer, and the releases without a
     * block say so instead of vanishing. */
    n = Updater_CollectNotes(kNotesJson, true, "v0.6.4", false, out, sizeof(out));
    CHECK(n == 4);
    CHECK(strstr(out, "== v0.7.0 ==\n- (no notes for this version)\n") != NULL);
    CHECK(strstr(out, "== v0.6.9 ==\n- (no notes for this version)\n") != NULL);

    /* A -dev build sits before its own X.Y.Z. */
    n = Updater_CollectNotes(kNotesJson, true, "v0.7.1-dev.3+abc", false, out, sizeof(out));
    CHECK(n == 2);

    /* Nothing newer, junk, empty. */
    CHECK(Updater_CollectNotes(kNotesJson, true, "v0.7.2", false, out, sizeof(out)) == 0 && out[0] == '\0');
    CHECK(Updater_CollectNotes("[]", true, "v0.1.0", false, out, sizeof(out)) == 0);
    CHECK(Updater_CollectNotes("{\"message\":\"rate limited\"}", true, "v0.1.0", false, out, sizeof(out)) == 0);

    /* Tiny buffer: truncated at a line boundary, still terminated. */
    n = Updater_CollectNotes(kNotesJson, true, "v0.6.4", false, out, 40);
    CHECK(strlen(out) < 40);
    CHECK(Updater_CollectNotes(kNotesJson, true, "v0.6.4", false, out, 0) == 0);

    /* Truncated JSON mid-body: the block so far is still used. */
    n = Updater_CollectNotes("[{\"tag_name\":\"v1.0.0\",\"body\":\"<!-- mzm-notes -->\\n- Part", true, "v0.9.0", false, out, sizeof(out));
    CHECK(n == 1);
    CHECK(strstr(out, "- Part") != NULL);
}

/* Tags per platform (one version counter): a Wii U only release, a 3DS only one, a general one
 * with both assets. Built twice: as the 3DS (the defaults) and as the Wii U
 * (-DUPDATER_PLATFORM="wiiu" -DUPDATER_ASSET_SUFFIX=".wuhb"). */
static const char kPlatJson[] =
    "[{\"tag_name\":\"wiiu-v0.9.2\",\"prerelease\":false,\"assets\":["
    "{\"browser_download_url\":\"http://h/mzm-wiiu.wuhb\"}],"
    "\"body\":\"<!-- mzm-notes -->\n- Wii U only\n<!-- /mzm-notes -->\"},"
    "{\"tag_name\":\"3ds-v0.9.1\",\"prerelease\":false,\"assets\":["
    "{\"browser_download_url\":\"http://h/mzm-3ds.cia\"}],"
    "\"body\":\"<!-- mzm-notes -->\n- 3DS only\n<!-- /mzm-notes -->\"},"
    "{\"tag_name\":\"v0.9.0\",\"prerelease\":false,\"assets\":["
    "{\"browser_download_url\":\"http://h/mzm-3ds-090.cia\"},"
    "{\"browser_download_url\":\"http://h/mzm-wiiu-090.wuhb\"}],"
    "\"body\":\"<!-- mzm-notes -->\n- Both\n<!-- /mzm-notes -->\"}]";

static void TestPlatforms(void) {
    UpdaterRelease rel;
    char out[1024];
    const bool wiiu = strcmp(UPDATER_PLATFORM, "wiiu") == 0;
    const char* own = wiiu ? "wiiu-v0.9.2" : "3ds-v0.9.1";
    const char* other = wiiu ? "3ds-v0.9.1" : "wiiu-v0.9.2";

    CHECK(Updater_TagVersion("v0.9.0") != NULL);
    CHECK(Updater_TagVersion(own) != NULL && strcmp(Updater_TagVersion(own), own + strlen(own) - 5) == 0);
    CHECK(Updater_TagVersion(other) == NULL);
    CHECK(Updater_TagVersion("garbage") == NULL);
    CHECK(Updater_IsNewerBuild("v0.9.0", false, own, false));
    CHECK(!Updater_IsNewerBuild("v0.8.2", false, other, false)); /* never another platform's */
    CHECK(!Updater_IsNewerBuild(other, false, "v0.9.3", false));
    CHECK(Updater_IsNewerBuild(own, false, "v0.9.3", false));   /* its own tag as the current version */

    /* The newest release of its own: the other platform's newer one is skipped. */
    CHECK(Updater_PickRelease(kPlatJson, false, &rel));
    CHECK(strcmp(rel.tag, own) == 0);
    CHECK(strcmp(rel.cia_url, wiiu ? "http://h/mzm-wiiu.wuhb" : "http://h/mzm-3ds.cia") == 0);
    /* A general release gives each platform its own asset. */
    CHECK(Updater_PickRelease(strstr(kPlatJson, "{\"tag_name\":\"v0.9.0\""), false, &rel));
    CHECK(strcmp(rel.cia_url, wiiu ? "http://h/mzm-wiiu-090.wuhb" : "http://h/mzm-3ds-090.cia") == 0);

    /* What's new: its own and the general notes, never the other platform's. */
    CHECK(Updater_CollectNotes(kPlatJson, false, "v0.8.2", false, out, sizeof(out)) == 2);
    CHECK(strstr(out, "- Both\n") != NULL);
    CHECK(strstr(out, wiiu ? "- Wii U only" : "- 3DS only") != NULL);
    CHECK(strstr(out, wiiu ? "3DS only" : "Wii U only") == NULL);
}

int main(void) {
    UpdaterRelease rel;

    CHECK(Updater_IsNewer("v0.6.4", "v0.6.5"));
    CHECK(!Updater_IsNewer("v0.6.5", "v0.6.5"));
    CHECK(!Updater_IsNewer("v0.6.5", "v0.6.4"));
    CHECK(Updater_IsNewer("v0.6.9", "v0.7.0"));
    CHECK(Updater_IsNewer("v0.6.4-dev.3+abc", "v0.6.4"));
    CHECK(!Updater_IsNewer("v0.6.4", "v0.6.4-dev.3+abc"));
    CHECK(Updater_IsNewer("v0.6.4-dev.3+abc", "v0.6.5"));
    CHECK(!Updater_IsNewer("garbage", "v0.6.5"));
    CHECK(!Updater_IsNewer("v0.6.4", ""));

    /* The .cia releases: the 3DS build's (the Wii U one takes .wuhb, TestPlatforms). */
    if (strcmp(UPDATER_ASSET_SUFFIX, ".cia") == 0) {
    CHECK(Updater_PickRelease(kJson, false, &rel));
    CHECK(strcmp(rel.tag, "v0.6.5") == 0);
    CHECK(strcmp(rel.cia_url, "http://h/stable.cia") == 0);
    CHECK(!rel.prerelease);

    CHECK(Updater_PickRelease(kJson, true, &rel));
    CHECK(strcmp(rel.tag, "v0.7.0") == 0);
    CHECK(strcmp(rel.cia_url, "http://h/beta.cia") == 0);
    CHECK(rel.prerelease);
    }

    CHECK(!Updater_PickRelease("[]", true, &rel));
    CHECK(!Updater_PickRelease("{\"message\":\"rate limited\"}", true, &rel));
    /* Truncated mid-URL: the incomplete release must be skipped, not garbled. */
    CHECK(!Updater_PickRelease("[{\"tag_name\":\"v1.0.0\",\"assets\":[{\"browser_download_url\":\"http://h/x.ci", true, &rel));

    TestNotes();
    TestChannels();
    TestPlatforms();

    if (sFailures == 0) printf("updater_parse_test (%s): OK\n", UPDATER_PLATFORM);
    return sFailures ? 1 : 0;
}
