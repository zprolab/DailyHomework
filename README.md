# 每日作业 · DaHoWork

教室用「每日作业 / 公告 / 演讲提醒」放映应用。**一套 C++20 代码库同时跑 Windows 与 macOS**，
放映画面 1920×1080 固定版式，屏幕所见与导出图片逐像素一致。

```
core/     领域层：数据模型、JSON、持久化、上学日规则、排版引擎（无 UI 依赖，纯 C++20）
render/   Qt 渲染层：DrawCmd → QPainter/QImage（屏幕与 PNG 共用）
apps/qt/  Qt 6 界面外壳：编辑页 / 设置 / 课表总览 / 放映窗
tools/    dhw_render_cli：把 data.json 无头渲染成 PNG，用于核对版式
tests/    dhw_tests：单元测试（自研极简框架，无第三方依赖）
docs/     文档；docs/spec/ 下是从旧实现逐条提取的领域与版式规格
```

## 为什么是这套结构

旧版本是两份实现（Tauri 2 + Vue/Rust 一套、Win32 C++ 一套），版式靠脚本逐命令对照，
改一处要改两边。现在领域规则、排版引擎、渲染约定都只有一份：

- **排版与渲染分离**：`layout_presentation()` 只输出 `DrawCmd` 列表（纯数据、可测试、
  不依赖 Qt），`render_commands()` 负责把它画成像素。屏幕预览与 PNG 导出走同一条路径，
  所以「放映页看到的」和「导出的图」不可能不一致。
- **文本度量是唯一平台相关点**：`TextMetrics` 接口在测试里用确定性桩，
  在外壳里用 `QFontMetricsF`；排版算法本身与平台无关，可以无头测。
- **文件与目录名一律 ASCII**，中文只出现在界面文案里（`scripts/verify-macos.sh` 会检查）。

## 构建与运行

macOS（本机开发环境；Qt 6.12.0 官方安装器套件，Homebrew 的 qt6 也可以）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build build
ctest --test-dir build --output-on-failure      # 或 ./build/tests/dhw_tests
./build/apps/qt/DaHoWork                         # 默认数据目录 = exe 同级 data/
```

一条命令做完「干净重建 + 全部测试 + ASCII 文件名检查 + 两种出图逐字节比对」：

```bash
scripts/verify-macos.sh
```

Windows：见 [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md)（`scripts/build-windows.ps1`，
支持动态 Qt + windeployqt，或自备静态 Qt 套件的单文件 exe；`-StaticQt` 只切 `/MT`，
静态链接取决于套件，构建脚本与 `verify-windows.ps1 -Static` 会用 `dumpbin` 校验）。

无头出图与样式核对：

```bash
build/tools/dhw_render_cli --data fixtures/demo/data.json --date 2026-10-12 --out out.png
build/tools/dhw_render_cli --dump-style style.json     # 导出全部可调项
```

## 数据与兼容

- 数据文件 `<exe 同级>/data/data.json`，schema 与旧版**完全兼容**（同一份 JSON、同样的字段名、
  未知字段忽略、缺失字段取默认值）。`fixtures/` 下四份旧数据在测试里逐份验证过能读能写。
- 旧版本的「每日作业数据」目录会被识别：新目录没有数据而旧目录有数据时继续用旧目录，
  不会静默生成空数据（详见 `docs/BUILD-WINDOWS.md` 末尾）。
- 保存是原子的（`data.json.tmp` → fsync → rename），保存前自动备份，最多保留 30 天、
  同一天只留最新；主文件损坏时按备份新→旧自动恢复并回写。

## 调样式（不用重新编译）

两件不同的事，别混：

**1) 放映画面的版式数值** —— 数据目录里的 `style.json`（23 项：字号层级、徽章尺寸、圆角、内边距、
行距系数、自适应上下限等）。`dhw_render_cli --dump-style style.json` 导出默认值当模板，改完重启应用即生效。
`tests/style_test.cpp` 会把默认值与可调项清单锁住，避免悄悄漂移。

**2) 界面控件的主题** —— 用 **Qt 自带原生平台样式**，不写自绘皮肤：Windows 上用 `windows11`
（Qt 6.7+ 原生 uxtheme 样式）/ `modernwindows` / `windowsvista`，macOS 上用系统 `macos` 样式；
深浅色**只跟随系统**（设置页的外观开关为只读；旧数据里的 appearance 字段仅作兼容保留）。手写 QSS 只保留极少量品牌点缀。想看某个 style 的效果可用
`--widget-style <name>` 覆盖（名字取 `QStyleFactory::keys()`）。

字号层级由测试守护（`layout_type_hierarchy_is_never_inverted`）：
日期主标题 ≥ 卡片标题 > 公告 ≥ 作业正文 > 课表 / 演讲正文。

## 测试

```bash
./build/tests/dhw_tests            # 全部
./build/tests/dhw_tests layout     # 只跑名字含 layout 的用例
./build/tests/dhw_tests --list     # 列出用例
```

覆盖：JSON、模型与向后兼容、日期/日历、文件系统（含原子写）、UTF-8、文本度量与换行、
DrawCmd、领域规则（上学日 / 今天 / 建议 / 公告）、持久化（备份 / 恢复 / 迁移 / 图片归档）、
排版不变量（不越界、不叠字、编号规则、截断、层级）。

## 当前状态

已完成：核心领域层、排版引擎、Qt 渲染层、无头出图工具、macOS 与 Windows 构建脚本、
样式文件化。界面外壳（编辑页 / 设置 / 课表总览 / 放映窗）在 `apps/qt/` 下持续完善中；
`docs/spec/` 里的规格是它的验收依据。
