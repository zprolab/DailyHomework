# Windows 构建与发布

本项目在 Windows 上有两种发布形态，按教室机的实际情况二选一。所有命令都在仓库根目录执行。

## 前置

- Windows 10/11 x64。
- Visual Studio（2022 或更新，含「使用 C++ 的桌面开发」工作负载）。脚本会自动选择 CMake 支持的最新 Visual Studio 生成器，无需改脚本。
- Qt 6.12.x（或 6.11.x）的 `msvc2022_64` 套件（默认路径 `C:\Qt\6.12.0\msvc2022_64`）。
- PowerShell 5.1 或更高版本（`Compress-Archive` 需要 5.0+）。

## 快速开始：构建 → 验收 → 打包

三条命令即可从干净仓库走到可分发的 zip：

```powershell
# 1) 构建（动态 Qt + windeployqt）
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
    -QtDir C:\Qt\6.12.0\msvc2022_64 -Deploy

# 2) 一键验收
powershell -ExecutionPolicy Bypass -File scripts\verify-windows.ps1

# 3) 打包（增量构建后重新部署并压缩）
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
    -QtDir C:\Qt\6.12.0\msvc2022_64 -Deploy -Zip
```

第 3 条命令复用同一个 `build\` 目录，不会重新编译全部源码；`-Zip` 必须与 `-Deploy` 一起使用。
脚本会打印 CMake 版本、Qt 路径、生成器，结束时打印 exe 的绝对路径与体积。

### `build-windows.ps1` 参数

| 参数 | 默认值 | 作用 |
|---|---|---|
| `-QtDir` | 自动探测 `C:\Qt\6.12.0\msvc2022_64` 等 | Qt 安装前缀 |
| `-Generator` | CMake 支持的最新 Visual Studio 生成器 | 覆盖生成器（例如 `"Visual Studio 17 2022"`、`Ninja`） |
| `-Force` | 关 | 单元测试失败时**仍然**继续 `-Deploy`/`-Zip`；脚本结束时仍返回非零退出码 |
| `-Fluent` | 关 | 构建 WinUI3/Fluent 外壳（vendored Fluent-Qt，`-DDHW_UI_FLUENT=ON`）；不加则用原生 Qt 样式外壳 |
| `-BuildDir` | `build` | 构建目录 |
| `-Config` | `Release` | VS 配置 |
| `-Jobs` | 自动（CPU 核数） | 并行编译任务数，原样传给 `cmake --build --parallel`，日志会打印实际值 |
| `-NoSccache` | 关 | PATH 里有 `sccache` 时默认启用编译缓存；加这个开关强制直接调用编译器 |
| `-StaticQt` | 关 | 与静态 Qt 套件配对：`/MT` 静态 CRT + 跳过 `windeployqt`；不能把共享套件变成自包含，见「形态二」 |
| `-SkipTests` | 关 | 跳过 `ctest` |
| `-Deploy` | 关 | 运行 `windeployqt`，把 Qt 运行库放进 exe 目录 |
| `-Zip` | 关 | 部署后生成 `dist\DaHoWork-win64.zip` |

`assets\app.ico` 不存在时脚本只打印警告和重新生成命令，不会联网下载任何东西；
此时 CMake 会跳过资源编译，exe 没有图标。仓库中已提交生成好的图标，正常 clone 不会遇到。

## 一键验收

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-windows.ps1
```

脚本默认检查 `build\apps\qt\Release\DaHoWork.exe`，可用 `-BuildDir` / `-Config` 覆盖；
静态单文件构建再加 `-Static`（额外一步 `dumpbin` 导入表检查）。
所有子进程都带 `QT_QPA_PLATFORM=offscreen`，**不会弹出可见窗口**。任一步失败立即 `exit 1`。

### 每一步的预期输出

1. **单元测试**
   - 命令：`ctest --test-dir build -C Release --output-on-failure`
   - 预期：`100% tests passed, 0 tests failed out of 1`；
     `dhw_tests` 自身打印 `157 passed, 0 failed`。
2. **自检**
   - 命令：`DaHoWork.exe --data-dir <临时数据目录> --selfcheck`
   - 预期：输出若干 `PASS ...`，最后一行 `selfcheck failures=0`，退出码 0。
   - 自检会写入数据，脚本先把 `fixtures\demo\data.json` 复制到临时目录，测完删除，
     不会碰发布目录或用户数据。
3. **控件样式**
   - 命令：`DaHoWork.exe --list-styles`
   - 预期：
     - Windows 11（build ≥ 22000）：`platform chain picks: windows11`、
       `selected style: windows11`；
     - Windows 10：`selected style: modernwindows`。
   - 若 Win11 上选中的不是 `windows11`，通常说明 Qt 低于 6.7（没有 `windows11` 样式），
     验收脚本会判失败；本项目已验证 Qt 6.12.0。
4. **无头出图**
   - 命令：`DaHoWork.exe --render build\verify\render-1920x1080.png`
   - 预期：`wrote ...render-1920x1080.png`；脚本读取 PNG 的 IHDR，
     宽高必须是 **1920 × 1080**。
5. **exe 同级数据目录**
   - 预期：首次运行后自动出现 `build\apps\qt\Release\data\`。
   - 如果该目录是本次验收创建的，脚本结束时会删除它，保证后续 `-Zip` 打出的包干净；
     验收前已存在的 `data\` 不会被删除。
6. **静态导入表（仅 `-Static`）**
   - 命令：`dumpbin /dependents build\apps\qt\Release\DaHoWork.exe`（脚本自动查找 `dumpbin`，
     找不到时用 `vswhere` 定位 MSVC 工具集；仍找不到则提示从 VS 开发者命令行运行）。
   - 预期：`imports: KERNEL32.dll, USER32.dll, ...`；**不能**出现 `Qt6*.dll`、
     `VCRUNTIME140*.dll`、`MSVCP140*.dll`、`CONCRT140*.dll`、`ucrtbase.dll`、`api-ms-win-crt-*.dll`。
   - `dhw_render_cli.exe` 同样检查（存在时）。

全部通过时最后打印：

```
All Windows checks passed.
```

## 应用图标与版本资源

- `assets/app.ico`：Windows 应用图标，内含 16/24/32/48/64/128/256 七档 PNG 条目。
- `assets/app-icon-256.png`：文档与商店页用的 256×256 预览图。
- `apps/qt/app.rc`：ASCII-only 资源脚本，包含 `IDI_ICON1` 与 `VERSIONINFO`。
- 版本字段：FileVersion / ProductVersion = `2.0.0.0`，ProductName = `DaHoWork`，
  FileDescription = `DaHoWork daily homework presenter`，OriginalFilename = `DaHoWork.exe`，
  LegalCopyright = `MIT License`。

图标由 `tools/make_icon` 用共享的 `dhw::icons` 矢量图元绘制：品牌蓝 `#2E5FA3` 圆角方块 +
白色书本图元，按每种目标尺寸单独栅格化。ICO 容器是手写的（ICONDIR + ICONDIRENTRY +
PNG 载荷），同一 Qt 版本下逐字节确定。

重新生成：

```powershell
cmake --build build --config Release --target dhw_make_icon
build\tools\Release\dhw_make_icon.exe --out assets\app.ico --png assets\app-icon-256.png
```

macOS 上对应：

```bash
cmake --build build-winprep
./build-winprep/tools/dhw_make_icon --out assets/app.ico --png assets/app-icon-256.png
```

连续运行两次，`shasum -a 256 assets/app.ico` 应完全一致。生成后的两个文件都要提交。

资源接入规则（`apps/qt/CMakeLists.txt`）：**只有 MSVC 且 `app.rc` / `assets/app.ico` 都存在**
时才 `enable_language(RC)` 并把 `app.rc` 加入可执行目标；`assets\` 通过
`target_include_directories` 提供给 `rc.exe` 解析 `"app.ico"`。其它平台和 MinGW 完全不受影响。
`WIN32_EXECUTABLE ON` 与 `OUTPUT_NAME DaHoWork` 保持不变。

检查版本资源是否写进 exe：

```powershell
(Get-Item build\apps\qt\Release\DaHoWork.exe).VersionInfo |
    Format-List FileVersion, ProductVersion, ProductName, FileDescription, LegalCopyright
```

## 形态一：动态 Qt + windeployqt（默认，最省事）

前提：本机装了 Visual Studio（2022 或更新，含 C++ 桌面开发）与 Qt 6（`msvc2022_64` 套件）。

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
    -QtDir C:\Qt\6.12.0\msvc2022_64 -Deploy
```

测试失败的场合（例如某个用例在 Windows 上还没修好，但你想先把包打出来装到教室机上试用）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
    -QtDir C:\Qt\6.12.0\msvc2022_64 -Deploy -Force
```

`-Force` 只影响「测试失败是否阻断部署」，**不会**掩盖失败：脚本结尾会打印警告并以退出码 1 结束。

脚本做四件事：CMake 配置（自动选择 VS 生成器，x64）→ 构建 → `ctest` 跑单元测试 → `windeployqt`
把 Qt 运行库拷到 `build\apps\qt\Release\`。

加 `-StaticQt` 时流程变成：配置前校验 `-QtDir` 是静态 Qt 套件（读 `mkspecs\qconfig.pri`，
共享套件直接报错）→ 构建 → `ctest` → 用 `dumpbin /dependents` 断言两个 exe 只导入系统 DLL，
不再执行 `windeployqt`。任何一步不满足都会以非零码退出，不会产出「看起来像单文件」的包。

产物目录可直接整体拷到教室机：`DaHoWork.exe` + Qt DLL + `platforms\qwindows.dll`。
教室机若没有 VC++ 运行库，`windeployqt` 已经带上 `msvcp140.dll` / `vcruntime140.dll`。

### `-Zip` 发布包结构

`-Deploy -Zip` 在 `dist\`（ASCII 目录名，避免 code page 问题）生成
`dist\DaHoWork-win64.zip`。压缩包根目录就是发布目录内容：

```
DaHoWork-win64.zip
├─ DaHoWork.exe
├─ dhw_render_cli.exe
├─ Qt6Core.dll / Qt6Gui.dll / Qt6Widgets.dll / ...
├─ platforms\qwindows.dll
├─ styles\...
├─ imageformats\...
└─ vc_redist 相关 DLL（如需要）
```

`data\`、`local_settings.json`、`style.json`、`crash.log` 等运行期文件会被排除；
教室机首次启动时在 exe 同级自动创建 `data\`。静态 Qt 构建同样支持 `-Zip`，
此时包里只有 `DaHoWork.exe` 和 `dhw_render_cli.exe`。

## 形态二：静态 Qt，单文件 exe（零依赖，双击即跑）

适合「不能装任何运行库、只能拷一个 exe」的教室机。需要在本机先准备一份静态 Qt
（一次性工作，约 20~40 分钟）：

```powershell
# 1) 准备目录
mkdir C:\src\qt-static
cd C:\src\qt-static

# 2) 取源码（用 Qt 官方安装器的 Sources，或 git clone）
git clone --depth 1 --branch v6.12.0 https://code.qt.io/qt/qt5.git
cd qt5
git submodule update --init --recursive

# 3) 配置静态构建（在对应你 Visual Studio 版本的 "x64 Native Tools Command Prompt" 里执行）
mkdir C:\src\qt-static\build
cd C:\src\qt-static\build
..\qt5\configure.bat -static -static-runtime -release -prefix C:\Qt\6.12.0-static `
    -nomake examples -nomake tests -no-opengl -skip qtwebengine -skip qtdeclarative

# 4) 构建并安装
cmake --build . --parallel
cmake --install .
```

然后：

```powershell
# 构建；脚本会校验静态套件，并在构建后用 dumpbin 检查导入表
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
    -QtDir C:\Qt\6.12.0-static -StaticQt -Deploy

# 验收（在原有 5 项之外追加导入表断言）
powershell -ExecutionPolicy Bypass -File scripts\verify-windows.ps1 -Static
```

`-StaticQt` 只负责把本项目的 MSVC 运行库切到 `/MT`（与 `-static-runtime` 编出的 Qt 对齐）
并跳过 `windeployqt`；**它不会把共享 Qt 变成静态**。真正的静态链接来自 `-QtDir` 指向的静态套件，
脚本会在配置前读 `mkspecs\qconfig.pri` 拒绝共享套件，构建后再用 `dumpbin` 确认。

验收输出里会打印实际导入表，例如：

```
imports: KERNEL32.dll, USER32.dll, GDI32.dll, ...
ok: no Qt or MSVC runtime DLL imports
```

出现 `Qt6Core.dll` / `VCRUNTIME140.dll` / `MSVCP140.dll` / `ucrtbase.dll` 就说明仍是动态链接：
检查 `-QtDir` 是否真的指向 `configure -static -static-runtime` 的安装前缀，以及 `-StaticQt` 是否漏传。
缺少 `dumpbin.exe` 时在 VS「x64 Native Tools Command Prompt」里运行验收脚本
（脚本也会用 `vswhere` 自动找 MSVC 工具集）。

得到的 `DaHoWork.exe` 不依赖 Qt DLL，也不依赖 VC++ 运行库，可单独拷走
（上面两条断言通过后，再拷到一台没装 Qt / VC++ Redist 的机器上双击复验）。
体积会明显变大（约 25~40 MB，release + strip 后更小）。

### 静态构建的注意事项

- 本项目的 Qt 依赖只有 `Qt6::Widgets` / `Qt6::Gui` / `Qt6::Core`（没有 QML、没有 WebEngine），
  所以静态裁剪很干净：`-skip qtdeclarative -skip qtwebengine -skip qtquick3d`。
- `-static-runtime` 会把 MSVC 运行库一起静态链进去，这才是「拷一个 exe 就能跑」的关键；
  项目侧必须同时传 `-StaticQt`（`-DDHW_STATIC_QT=ON` 只切 `/MT`），两边运行库一致才不会 LNK2038。
- 静态 Qt 的平台插件（`qwindows`）必须在链接期静态导入；如果 exe 在真机上报
  `no Qt platform plugin could be initialized`，就是插件没链进去，需要检查静态 Qt 套件与 CMake 的
  plugin 导入，而不是数据或字体问题。
- 中文字体不走 Qt 的字体引擎，由系统 `Microsoft YaHei UI` 提供，无需打包字体。
- 首次运行会在 exe 同级创建 `data\` 目录（数据、备份、图片存档都在里面）。

## 数据目录与兼容

- 新版本的默认数据目录是 `<exe 同级>\data\`，数据文件 `<data>\data.json`。
- 旧版本（重写前）用的是 `<exe 同级>\每日作业数据\`。程序启动时会优先用新目录；
  新目录没有数据而旧目录有数据时，**继续使用旧目录**，不会静默地生成一份空数据。
  旧目录里的 `每日作业本机设置.json`、`班徽.png`、`家庭作业_<日期>.png` 也都能被读到。
- 想彻底切换成新目录：把旧目录整体改名成 `data`，再启动一次即可。

## 无头出图（可选，用于核对版式）

CLI 与外壳共用同一套排版引擎与渲染器，出图逐像素一致：

```powershell
build\tools\Release\dhw_render_cli.exe --data "C:\path\data\data.json" --date 2026-10-12 --out out.png
build\apps\qt\Release\DaHoWork.exe --data-dir "C:\path\data" --date 2026-10-12 --render out2.png
```

样式（字号、徽章尺寸、圆角等）可以用数据目录里的 `style.json` 调整，无需重新编译；
`dhw_render_cli --dump-style style.json` 会导出一份带全部可调项的默认文件。

## 本仓库在 macOS 上的交叉验证范围

**Windows 二进制必须在 Windows 上构建与验证。** 原因：目标文件格式（PE/COFF）、资源编译器
（`rc.exe`）、MSVC 运行库、`windeployqt`、Qt 的 `windows11` / `modernwindows` 样式都只在
Windows 上存在，macOS 的 clang + Homebrew Qt 无法替代。

macOS 侧只验证以下内容（`scripts/verify-macos.sh`）：

- 同一套 CMake 工程在 Clang + Qt 6.11 下**编译期**无警告、无错误；
- 157 个单元测试全部通过；
- 整棵工作树的文件/目录名均为 ASCII；
- CLI 与外壳在 `QT_QPA_PLATFORM=offscreen` 下的 1920×1080 出图逐字节一致；
- `dhw_make_icon` 能生成 `assets/app.ico` / `assets/app-icon-256.png`，且重复运行 SHA-256 相同。

macOS 上**没有**验证、也无法验证：

- `app.rc` 的 RC 编译与 `VERSIONINFO` 是否进入 PE（MSVC 分支在 macOS 上是关闭的）；
- `windeployqt` 的依赖收集、`-Zip` 压缩包内容；
- `verify-windows.ps1` 的 5 项验收（`--list-styles` 在 macOS 上只会选 `macos` / `fusion`）；
- 静态 Qt 单文件链接，以及静态套件检测 / `dumpbin` 导入表断言（这些检查只在 Windows 上运行）。

因此，发布前必须在 Windows 真机上完整跑一遍「快速开始」的三条命令。

## 工具链版本说明（VS 2022 / 2026 混用）

- 生成器由脚本自动探测（`cmake --help` 里列出哪些就用最新那个），**不要**在脚本或 CMake 里写死。
- Qt 官方只提供 `msvc2022_64` 这类套件；MSVC ABI 向后兼容，所以 VS 2022 编的 Qt 可以配 VS 2026 的
  编译器使用。若链接期出现运行库不匹配（`MSVCRT` / `vcruntime` 冲突），两个解法：
  1) 用 Visual Studio Installer 补装「MSVC v143 生成工具」（与 Qt 套件完全对齐）；
  2) 或在 Qt MaintenanceTool 里选与当前 VS 版本匹配的套件（若官方已提供）。
- CMake 太旧会认不出新 VS：脚本第一行会打印 `cmake --version`，若探测不到生成器，升级 CMake 或手传
  `-Generator "Visual Studio NN YYYY"`。

## 常见问题：`Could not find any instance of Visual Studio`

症状（两条一起出现就是同一个原因）：

```
generator: Visual Studio 17 2022 (x64)          <- 脚本选了旧的
CMake Error: Generator: Visual Studio 17 2022   <- CMake 找不到对应 VS
Could not find any instance of Visual Studio.
```

原因：**CMake 版本比你的 Visual Studio 旧**，`cmake --help` 里没有你那个 VS 版本的生成器
（例如只列到 `Visual Studio 17 2022`，而机器上装的是 VS2026）。脚本现在会把
「CMake 支持的生成器」和「vswhere 检测到的已装 VS」交叉比对，不匹配时直接报可执行的修复建议。

修复二选一：

**A. 升级 CMake（推荐）**

```powershell
winget upgrade --id Kitware.CMake -e
cmake --version                                  # 应为 4.x
cmake --help | findstr /i "Visual Studio"        # 应看到与你 VS 匹配的那一行
```

**B. 不升级 CMake，改用 Ninja（立刻可用）**

在开始菜单里打开 **「x64 Native Tools Command Prompt for VS 2026」**（该环境已把 `cl.exe` /
`rc.exe` 放进 PATH），然后：

```powershell
cd C:\src\DaHoWork
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
    -QtDir C:\Qt\6.12.0\msvc2022_64 -Generator Ninja -Deploy
```

Ninja 是单配置生成器，产物在 `build\apps\qt\DaHoWork.exe`（没有 `Release\` 子目录）；
构建/验收脚本都已按生成器自动选择路径。

可选生成器：`-Generator Ninja`（推荐，需要 PATH 里有 `ninja`）或 `-Generator "NMake Makefiles"`
（`nmake` 由 VS 自带）。**脚本已自动回退**：检测不到可用 VS 生成器时，会依次尝试 PATH 里的
`ninja`、`nmake`，都不在则报错并提示怎么装。

> **不要在可移动的 exFAT 盘上构建**：构建会生成几百 MB 中间文件，且 exFAT 没有权限位/符号链接，
> 容易触发工具链怪问题。把源码拷到本地盘（例如 `D:\src\DaHoWork` 或 `C:\src\DaHoWork`）再构建，
> 移动硬盘只用于传输。

诊断信息（贴给我看最快）：

```powershell
cmake --version
cmake --help | findstr /i "visual studio"
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -all -prerelease -format json |
    findstr /i "installationVersion displayName"
```

## Fluent（WinUI3）外壳

仓库 vendored 了 `calvinhxx/Fluent-Qt`（MIT，见 `third_party/fluentqt/README.vendored.md`），
构建时不需要联网。默认构建的是原生 Qt 样式外壳；要 WinUI3 观感就加 `-Fluent`：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 `
    -QtDir C:\Qt\6.12.0\msvc2022_64 -Fluent -Deploy
```

脚本会打印 `options: ... Fluent=True`，并把 `-DDHW_UI_FLUENT=ON` 传给 CMake。
注意：切换这个开关后建议删掉 `build\` 目录重新配置，避免 CMake 缓存前后不一致。
