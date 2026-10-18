# Third-party notices

The project's own source code is MIT licensed (see `LICENSE`). Binary builds also
contain the third-party components below; keep this file and `LICENSES/` next to the
executable when you redistribute a build.

## Qt 6 (Core, Gui, Widgets)

- License: GNU Lesser General Public License version 3 (LGPLv3). Qt is also offered
  by The Qt Company under commercial terms; some modules are additionally available
  under GPLv2/GPLv3. This project relies on the LGPLv3 option.
- The project uses **unmodified** Qt shared libraries: on Windows `windeployqt`
  copies `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, plugins and the MSVC runtime
  DLLs; on macOS the frameworks are deployed with the app. The libraries can be
  replaced with a locally built Qt of the same major.minor version, and nothing in
  the distribution prevents reverse engineering for debugging modifications of Qt.
- Corresponding source: the official Qt source archives for the exact release used
  are available at <https://download.qt.io/official_releases/qt/> (project builds use
  Qt 6.10.x / 6.12.x). No changes are made to Qt itself.
- License texts: `LICENSES/LGPL-3.0-only.txt` and `LICENSES/GPL-3.0-only.txt`
  (LGPLv3 incorporates terms of GPLv3).
- More about the obligations: <https://www.qt.io/licensing/open-source-lgpl-obligations>

### Static builds

A genuinely static executable (built against a static Qt with `-StaticQt`) cannot be
relinked by end users. LGPLv3 therefore additionally requires giving recipients the
means to relink the application against a modified Qt (typically the application
object files and build scripts). If that is not what you want to ship, use the
default dynamic deployment, or obtain a Qt commercial license. The default release
artifacts of this project are dynamic.

## Fluent-Qt (vendored under `third_party/fluentqt`)

- License: MIT (`LICENSES/FluentQt-MIT.txt`, upstream
  `third_party/fluentqt/LICENSE`).
- The library bundles renamed derivatives of the Inter and Noto Sans SC fonts
  (SIL Open Font License 1.1) and Fluent UI System Icons (MIT). Their notices and
  licenses are in `third_party/fluentqt/THIRD_PARTY_NOTICES.md` and copied into
  `LICENSES/` (`Inter-OFL-1.1.txt`, `Noto-Sans-SC-OFL-1.1.txt`,
  `FluentUI-System-Icons-MIT.txt`, `FluentUI-System-Icons-NOTICE.txt`).

## Distribution packages

`scripts/build-windows.ps1 -Deploy` copies `LICENSE`, this file and `LICENSES/` into
the deployed folder (and therefore into `dist/DaHoWork-win64.zip`). The macOS GitHub
Actions artifact contains the same files under `DaHoWork-macos/`.
