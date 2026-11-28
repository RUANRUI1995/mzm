#ifndef PORT_UPDATER_PARSE_H
#define PORT_UPDATER_PARSE_H

/* Pure logic of the self-updater: version comparison and release-list
 * parsing. No 3DS headers, so it builds and is unit-tested on the host
 * (tests/updater_parse_test.c). */

#include <stdbool.h>
#include <stddef.h>

/* The platform this build is (its own release tags are "<UPDATER_PLATFORM>-vX.Y.Z", next to the
 * general "vX.Y.Z") and the release asset it installs. */
#ifndef UPDATER_PLATFORM
#define UPDATER_PLATFORM "3ds"
#endif
#ifndef UPDATER_ASSET_SUFFIX
#define UPDATER_ASSET_SUFFIX ".cia"
#endif

typedef struct {
    char tag[32];       /* "v0.6.5", "3ds-v0.6.6" */
    char cia_url[384];  /* browser_download_url of the UPDATER_ASSET_SUFFIX asset (.cia) */
    bool prerelease;
} UpdaterRelease;

/* The "X.Y.Z..." part of a release tag this build takes: "vX.Y.Z" (every platform) or
 * "<UPDATER_PLATFORM>-vX.Y.Z" (this one); NULL for another platform's tag or a non-version. */
const char* Updater_TagVersion(const char* tag);

/* Compares "vX.Y.Z[-dev...]" strings (or this platform's tags, Updater_TagVersion). Returns true when `remote` is a newer
 * build than `current`. A "-dev" build sits on the way to its own X.Y.Z, so
 * the plain X.Y.Z release counts as newer than it. Unparseable input -> false. */
bool Updater_IsNewer(const char* current, const char* remote);

/* Same, for a build that knows its channel. A beta of X.Y.Z is the same number
 * as the stable X.Y.Z it later becomes, so on top of Updater_IsNewer a beta
 * counts the *stable* release of its own version as newer: `remotePrerelease`
 * false and equal numbers. (A beta page of the same tag is not newer, or a
 * beta would keep offering to install itself.) */
bool Updater_IsNewerBuild(const char* current, bool currentIsBeta,
                          const char* remote, bool remotePrerelease);

/* Scans a GitHub `GET /repos/{owner}/{repo}/releases` JSON body (newest
 * first) and picks the first release of this platform (Updater_TagVersion) that has its asset
 * (UPDATER_ASSET_SUFFIX) and, unless
 * `allowBeta`, is not a prerelease. Tolerates a truncated body. */
bool Updater_PickRelease(const char* json, bool allowBeta, UpdaterRelease* out);

/* Builds the "what's new" text for a player on `current`: every release in the
 * `GET .../releases` body that is newer than `current` (and not a prerelease
 * unless `allowBeta`), newest first, each as a "== vX.Y.Z ==" header followed
 * by the lines of its notes block. The block is whatever sits between
 * "<!-- mzm-notes -->" and "<!-- /mzm-notes -->" in the release body; the
 * markdown is flattened to plain "- " lines (headings, emphasis and code
 * marks dropped) because the console draws it with a 5x7 bitmap font. A
 * release without a block still gets its header, with a note saying so.
 * Returns how many releases were written; `out` is always NUL-terminated. */
int Updater_CollectNotes(const char* json, bool allowBeta, const char* current,
                         bool currentIsBeta, char* out, size_t outSize);

#endif
