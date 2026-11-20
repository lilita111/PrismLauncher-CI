# Feature registry — per-feature deletion guide

Purpose: when a build fails, identify the **one** feature that owns the failing code
and delete **only that feature**. Never delete the whole tree because one part broke.

## How to use this file

1. Get the failing build log (branch `ci-logs-<platform>` in `PrismLauncher-CI`, path
   `logs/build.log`; grep `-B5 -A15 "error"`).
2. Find the **first** failing file in the log.
3. Look it up in the ownership table below.
4. Apply that feature's deletion procedure.
5. Re-push the branch and let CI re-verify. Only if the failure persists in core
   (un-owned) files do we touch anything else.

## Ownership table

| File | Owner feature | What removing it entails |
|---|---|---|
| `launcher/modplatform/github/*` | F1 GitHub mod source | delete dir |
| `launcher/ui/pages/modplatform/GithubResourcePages.*` | F1 | delete files |
| `launcher/resources/multimc/scalable/instances/github.svg` + `multimc.qrc` entry | F1 | delete file + revert qrc line |
| `launcher/modplatform/EnsureMetadataTask.cpp` (GITHUB switch) | F1 | revert file to develop |
| `launcher/modplatform/ModIndex.*` | F1 | revert to develop |
| `launcher/modplatform/packwiz/Packwiz.cpp` | F1 | revert to develop |
| `launcher/net/Request.*` | F1 | revert to develop |
| `launcher/ui/dialogs/ResourceDownloadDialog.cpp` | F1 | revert to develop |
| `launcher/ui/dialogs/ResourceUpdateDialog.*` | F1 | revert to develop |
| `launcher/ui/pages/modplatform/ImportPage.cpp` | F1 | revert to develop |
| `launcher/CMakeLists.txt` — `GITHUB_SOURCES` block + `${GITHUB_SOURCES}` line | F1 | remove those lines only |
| `launcher/minecraft/MinecraftInstance.cpp` — *no F1 hunks* (its diff vs develop is F2's) | F2 | see F2 |
| `launcher/minecraft/update/ModUpdateTask.*` | F2 Auto-update mods | delete files |
| `launcher/minecraft/launch/ScanModFolders.*` | F2 | revert to develop |
| `launcher/minecraft/MinecraftInstance.cpp` — `AutomaticallyUpdateMods*` registerSetting block + launch step | F2 | remove that block only |
| `launcher/ui/widgets/MinecraftSettingsWidget.cpp/.ui` — `autoModUpdateGroup` block | F2 | remove that block only |
| `launcher/CMakeLists.txt` — `ModUpdateTask.cpp/.h` lines | F2 | remove those lines only |
| `launcher/minecraft/OfficialLauncherExportTask.*` | F3 Official-launcher export | delete files |
| `launcher/ui/MainWindow.cpp/.h/.ui` — export menu action + handler | F3 | remove those hunks only |
| `launcher/CMakeLists.txt` — `OfficialLauncherExportTask.*` lines | F3 | remove those lines only |
| `launcher/minecraft/mod/ModStore.*` | F4 Shared mod store | delete files |
| `launcher/minecraft/mod/ModStoreTasks.*` | F4 | delete files |
| `launcher/ui/dialogs/ModStoreDialog.*` | F4 | delete files (.ui picked up by AUTOUIC) |
| `launcher/ui/MainWindow.cpp/.h/.ui` — `actionModStore` + handler | F4 | remove those hunks only |
| `launcher/CMakeLists.txt` — `ModStore*` + `ModStoreDialog.*` lines | F4 | remove those lines only |
| `FORK.md`, `FEATURES.md` | docs | always safe to keep/delete |

**Note on `launcher/CMakeLists.txt`:** all four features touch it, but in disjoint,
clearly-labeled hunks (`GITHUB_SOURCES` block vs. single `ModUpdateTask` lines vs.
single `OfficialLauncherExportTask` lines vs. `ModStore*` lines). A failure here means
CMake config, not the features — check the error before deleting anything.

## F4 — Shared mod store (cross-instance dedup)

- Branch `feature/shared-mod-store`. Cleanly cut from develop.
- Feature commits: see `git log origin/develop..feature/shared-mod-store`.
- **Delete F4:** `git checkout feature/shared-mod-store && git reset --hard origin/develop`,
  then remove its files per the ownership table. No other feature references it.
- The store data itself (`<data>/modstore/`) is runtime state, not code — deleting F4
  code does not touch it; delete the folder manually if you want the disk back.

## F1 — GitHub as a mod source

- Branch `feature/github-mod-source` (fork PR lilita111/PrismLauncher#1).
- **Topology warning:** this branch was cut from F2, so it *contains* F2 underneath.
  `git merge-base develop feature/github-mod-source` = `1a6438dcf4` (F2 stack), not develop.
- Status: CI green (run #23, all 6 platforms).
- Feature commits (on top of F2): `471fa955`, `467644a4`, `d6472cda`,
  `d9d9cb94`, `5d92b382`, `a84afdb8`, `5a8063e1`, `f722038b`, `6f266881`.
- **Delete F1 only** (keep F2):
  `git checkout feature/github-mod-source && git reset --hard 3971d722` (= F2 tip).
- **Delete F1 from a merged/future product:** remove the F1 files listed above and
  revert the F1-owned shared files to develop.

## F2 — Auto-update mods on launch

- Branch `feature/auto-update-mods-on-launch` (fork PR lilita111/PrismLauncher#2).
- Branched cleanly from develop.
- Status: CI green (run #16/#22, all 6 platforms).
- Feature commits: `b6e7d29c`, `3ef109ba`, `d788f1cb`, `ff45dce1`, `1680b2f8`,
  `f341c698`, `3971d722`.
- **Delete F2:** `git checkout feature/auto-update-mods-on-launch && git reset --hard origin/develop`,
  then delete `launcher/minecraft/update/ModUpdateTask.*`.
- **Delete F2 from under F1** (since F1 contains F2): apply the same removals to
  `feature/github-mod-source`; F1's own code does not reference `ModUpdateTask`.

## F3 — Export to Official Minecraft Launcher

- Branch `feature/export-official-launcher`. Cleanly cut from develop.
- Status: CI run #25 (in progress at time of writing).
- Feature commits: `fad9c6b6`, `1f6032e9`.
- **Delete F3:** `git checkout feature/export-official-launcher && git reset --hard origin/develop`,
  then remove its 6 files/hunks per the ownership table. No other feature references it.

## F0 — CI instrumentation (separate repo)

- Lives only in `lilita111/PrismLauncher-CI` (build.yml log capture, ci-logs branches,
  `ci-latest` release publishing). **Never in the fork.**
- Deleting it never affects fork code. If CI itself misbehaves, fix build.yml there.

## Verified-green anchor

`origin/develop` (fork) + F0 (CI repo) is the only fully green combination at all times.
Any feature branch whose CI goes red and resists two fix rounds gets deleted per above,
not patched indefinitely.
