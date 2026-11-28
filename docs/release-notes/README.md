# Release notes

One file per tag, `<tag>.md`, written before tagging. The release workflow puts
it in the GitHub release body, and the consoles' updaters show it before the
player installs. The rules (language, length, what a stable release must
include) are in `CLAUDE.md`, section "Release notes".

## Tags: one version counter, three kinds

| Tag | Ships | Release page holds |
|---|---|---|
| `vX.Y.Z` | every platform | the 3DS CIA and the Wii U WUHB |
| `3ds-vX.Y.Z` | the 3DS only | the CIA |
| `wiiu-vX.Y.Z` | the Wii U only | the WUHB |

All three share **one** version counter: a number is used once, by whichever
kind of tag needs it (`v0.9.0`, then `3ds-v0.9.1`, then `wiiu-v0.9.2`, then
`v0.9.3`). Each console's updater takes the general tags and its own and never
sees the other platform's (`port_updater_parse.c`, `Updater_TagVersion`), so a
Wii U only release is never offered to a 3DS, and its notes never show there.
A platform's version numbers therefore have gaps; that is expected.

The notes file is named after the full tag: `v0.9.0.md`, `3ds-v0.9.1.md`,
`wiiu-v0.9.2.md`. A general release's notes are read on both consoles: say
which platform a line is about when it is not both.

3DS builds older than v0.8.2 only know `vX.Y.Z` tags: the first release after
them must be a general one (v0.8.2 is), or they would never hear of it.

Example (`v0.7.0.md`):

```
- Achievement badges now download on demand and are cached on the SD card.
- RetroAchievements settings moved to the Achievements tab.
- The updater can show what changed before you install.
```
