# PrismLauncher Fork — Custom Modloader Enhancements

This fork adds quality-of-life features on top of upstream PrismLauncher `develop`,
absorbs stalled community PRs, and stays rebase-friendly (each feature lives on its
own branch forked directly from upstream `develop`).

## Feature branches

### `feature/github-mod-source` (PR: lilita111/PrismLauncher#1, upstream: PrismLauncher/PrismLauncher#6475)

GitHub as a first-class mod source, next to Modrinth and CurseForge:

- **GitHub tab in "Download Mods"** (`launcher/ui/pages/modplatform/github/`).
- **`GithubAPI`** (`launcher/modplatform/github/GithubAPI.{h,cpp}`) implements the
  existing `ResourceAPI` interface on the GitHub REST API:
  - repository search via `/search/repositories`, sorted by best match / stars /
    last updated, scoped with `minecraft in:name,description,readme`;
  - direct project lookup through the existing `#` search prefix: `#owner/repo`;
  - versions map to the first non-`-sources`/`-javadoc` `.jar` asset of each
    release; prereleases map to the Beta release type.
- **Networking**: `Request::Spec` gains a `headers` field and a `Spec`-based
  `makeByteArray` overload (`launcher/net/Request.{h,cpp}`). GitHub rejects API
  calls without a `User-Agent`; all GitHub requests set `User-Agent`/`Accept`.
- **Packwiz metadata**: installed GitHub mods get `[update.github]`
  (`repo` + `tag`) entries (`launcher/modplatform/packwiz/Packwiz.cpp`), so
  provider/version info shows in the mods list and survives reinstalls.
- **Update checking**: `GithubCheckUpdate` compares the newest allowed release's
  tag against the stored one; wired into `ResourceUpdateDialog` next to the
  Modrinth/CurseForge checkers. Fixes upstream #1155.
- **"Change version"** support for GitHub mods (`ResourceDownloadDialog`).
- **`ResourceProvider::GITHUB`** enum value with capability/meta-URL handling;
  every provider `switch` updated (`ModIndex`, `EnsureMetadataTask`).
- **Version filtering**: the GitHub page uses a `ModModel` subclass
  (`GithubModModel`) that only applies the release-type filter, because GitHub
  releases carry no loader/MC-version metadata.
- Absorbs stalled upstream PR **#5741**: CurseForge share links
  (`curseforge.com/minecraft/share/...`) are rewritten to
  `api.curseforge.com/v1/shared-profile/...` so they import correctly
  (`launcher/ui/pages/modplatform/ImportPage.cpp`).

Known limitations: mods tab only; no dependency resolution (GitHub releases
carry no dependency info); unauthenticated GitHub rate limits apply
(60 req/h core, 10/min search).

### `feature/auto-update-mods-on-launch` (PR: lilita111/PrismLauncher#2)

Absorbs stalled upstream PR **#5615** (author dyredhead) and adapts it to
current `develop`:

- New per-instance setting **"Automatically Update Mods"** with
  `AllMods` / `OnlyEnabledMods` modes (`launcher/minecraft/mod/ModUpdateTask.*`,
  settings UI).
- Launch pipeline step that checks for mod updates and applies them before the
  game starts (skippable).
- `ScanModFolders` disconnect safeguards so cancelling mid-update doesn't crash.
- Adaptation commit replaces the removed `ModFolderModel::allMods()` with
  `allResources()` and uses the existing `updateFinished` signal.

## Verification status

- **No CI has run yet.** Forks have GitHub Actions disabled by default; the
  upstream PR (PrismLauncher/PrismLauncher#6475) has its build jobs gated
  behind maintainer approval for first-time contributors.
- To verify: fork **Settings → Actions → General → Allow all actions**, then
  close & reopen PR #1/#2 (or push any commit) to trigger the `Build` workflow
  (Linux, Windows MinGW/MSVC, macOS matrix).
- The code has been carefully reviewed against current `develop` (includes,
  signal/slot signatures, `ResourceAPI` contract), but is **compile-unverified**.

## Building

Standard PrismLauncher build; see upstream [BUILD.md](BUILD.md). In short:

```sh
cmake --preset ci            # configure (uses vcpkg for dependencies)
cmake --build --preset ci    # build
ctest --preset ci            # tests
```

Qt 6.8+, CMake ≥ 3.29 and a C++23 compiler are required; on Windows use the
Visual Studio or MinGW-w64 presets, on Linux install the distro Qt6 dev
packages first.
