# Vendored: calvinhxx/Fluent-Qt

A WinUI 3 / Fluent Design widget library for Qt Widgets, used by the settings page
pilot (`apps/qt/settings_page.*`) behind `-DDHW_UI_FLUENT=ON`.

## Provenance

| Field | Value |
|---|---|
| Upstream | `https://github.com/calvinhxx/Fluent-Qt` |
| Mirror used | `https://ghproxy.net/https://github.com/calvinhxx/Fluent-Qt.git` |
| Tag | `v1.10.1` (latest stable tag at vendoring time) |
| Commit | `d624fd325c11d0720993db6f3c714a4c4aef1cab` |
| Commit subject | `fix(menus): guard command menu teardown callbacks` |
| Commit date | 2026-10-17 20:42:43 +0800 |
| Vendored on | 2026-10-18 |
| Licence | MIT (verified: `LICENSE` is the MIT text, "Copyright (c) 2026 calvinhxx") |
| Third-party licences | Inter, Noto Sans SC, Fluent UI System Icons — texts kept under `third_party/` |

`github.com` itself is unreachable from this machine (port 443 times out); the tag was
fetched through the ghproxy mirror above with `git clone --depth 1 --branch v1.10.1`.

## What was removed

Nothing that the `FluentQt` library target builds or loads was touched. Removed:

- `.git/` (shallow clone metadata, 24 MB)
- `.github/`, `.agents/`, `.githooks/` (CI, agent skills, git hooks)
- `app/` (the Gallery executable; only built when `FLUENT_QT_BUILD_GALLERY=ON`)
- `tests/` (upstream test suite, gtest based; our project forbids a third-party test framework)
- `examples/`, `docs/`, `site/`, `support/`, `scripts/`, `tools/`
- `bindings/` (PySide6), `plugin/`, `platforms/`, `third_party/runtime/` (optional modules, off by default)
- `CMakePresets.json`, `vcpkg.json`, `llms.txt`, `AGENTS.md`, `README*.md`, `CODE_OF_CONDUCT.md`,
  `CONTRIBUTING.md`, `SECURITY.md`, `SUPPORT.md`
- `third_party/fonts/*` and `third_party/icons/*` binaries (upstream authoring sources,
  not referenced by the build); only their `LICENSE.txt` / `NOTICE.txt` remain
- `res/fonts/FluentQtUISimplifiedChinese-Regular.otf` (2.0 MB)

Kept: `CMakeLists.txt`, `cmake/`, `include/`, `src/`, `res/`, `uilib/`, `resources.qrc`,
`LICENSE`, `THIRD_PARTY_NOTICES.md`, `TRADEMARKS.md`, and the third-party licence texts.

**Final size: 9.6 MB, 455 files.**

### Note on the removed CJK font

Upstream embeds `res/fonts/FluentQtUISimplifiedChinese-Regular.otf` in `resources.qrc`
only for WebAssembly builds (`FLUENT_QT_OPTIONAL_RESOURCE_ENTRIES` is empty otherwise), and
`src/FluentQt.cpp` loads it behind a `QFile::exists()` guard, so a native build never reads
it. Chinese UI text therefore falls back to the platform font (PingFang SC on macOS), which
is what the pilot screenshots show. To re-embed it, restore the `.otf` and add the same
`<file alias="res/fonts/...">` line to `resources.qrc` for non-Emscripten builds.

## Patches

**No source patches.** Every vendored file is byte identical to the tag. Qt 6.12 does
produce one class of warning from the upstream sources, so two build-level settings in
*our* CMake isolate it instead of editing the vendored tree:

1. `third_party/fluentqt/src/compatibility/*` still tests the macro `Q_OS_MAC`, which Qt
   6.12 marks deprecated (`-Wdeprecated-pragma`). The root `CMakeLists.txt` adds
   `-Wno-deprecated-pragma` to the `FluentQt` target (Clang only), and `apps/qt/CMakeLists.txt`
   adds `third_party/fluentqt/{include,src}` as **SYSTEM** include directories of `dhw_app`.
   Effect: the warning is silenced for the third-party code only; the App itself stays at
   its usual `-Wall -Wextra -Wpedantic -Wshadow` and at zero warnings.
2. `FLUENT_QT_BUILD_*` / `FLUENT_QT_INSTALL` are pinned OFF before `add_subdirectory`, so a
   future upstream default cannot pull spdlog, gtest, Qt Quick or install rules into this build.

No macro, no compiler define and no generated file is needed to compile the library.

## File names

All 455 vendored source paths (+ this README) are pure ASCII, so `scripts/verify-macos.sh`'s file-name guard
passes without renaming anything. The vendored tree is **not** ignored by `.gitignore`
(it is source we build from), with the single exception of upstream's own `build*/`
convention, which our root `.gitignore` already covers for the whole tree.
