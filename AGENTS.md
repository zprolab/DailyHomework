# AGENTS.md — 每日作业 · DaHoWork

教室用「每日作业 / 公告 / 演讲提醒」放映应用。**一套 C++20 代码库同时跑 Windows 与 macOS**
（重写前的 Tauri 版与 Win32 版已删除；其领域与版式规格提取在 `docs/spec/`）。

## 模块边界（不要越界）

| 目录 | 职责 | 硬约束 |
|---|---|---|
| `core/` | 数据模型、JSON、日期、文件系统、持久化、上学日规则、排版引擎、DrawCmd | **禁止 include 任何 Qt 头**；不依赖平台 API（除 `fs.cpp` 的平台分支） |
| `render/` | `DrawCmd` → `QPainter`/`QImage`，`QtTextMetrics` | 只依赖 `dhw_core` + Qt Gui/Widgets |
| `apps/qt/` | 界面外壳；`AppController` 是控件接触数据的**唯一入口** | 控件不得自己读写 `data.json`，不得自己算领域规则 |
| `tools/` | `dhw_render_cli`：无头出图、样式导出 | 与外壳共用 `core` + `render`，出图必须逐字节一致 |
| `tests/` | `dhw_tests`（自研极简框架，文件被 CMake glob 自动收录） | 不引第三方测试框架 |

`docs/spec/domain-and-store.md` 与 `docs/spec/ui-and-layout.md` 是从旧实现逐行提取的规格，
**行为有疑问时以它们为准**（它们记录了旧版的所有边界情形与坑）。

## 构建与测试

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/dhw_tests <substring>       # 只跑匹配的用例；--list 列出全部
scripts/verify-macos.sh                   # 干净重建 + 全部测试 + ASCII 文件名检查 + 两种出图比对
```

Windows 见 `docs/BUILD-WINDOWS.md`；静态 Qt 单文件发布需自备 `configure -static -static-runtime`
的套件并传 `-StaticQt`（`-DDHW_STATIC_QT=ON` 只负责 `/MT` 运行库，不会把共享 Qt 变静态；
构建脚本与 `verify-windows.ps1 -Static` 会用 `dumpbin` 校验导入表）。

## 命名与语言（**硬性**）

- **文件与目录名一律 ASCII**，禁止中文（`scripts/verify-macos.sh` 会扫描整棵工作树）。
- 中文只出现在**界面文案**（按钮、标题、提示、toast、诊断对话框）里。
- 标识符英文（C++ 函数 `snake_case`、类型 `PascalCase`；Qt 类按 Qt 习惯）。
- 注释与日志英文。旧版文件名的中文常量**只能出现在**「读取/迁移兼容」的位置
  （见 `core/src/store.cpp` 里的 `kLegacy*`），**任何新写入的路径都不许用中文**。

## 已定决策（不要重新翻案）

- **界面控件主题用 Qt 自带原生样式**：Windows 依次尝试 `windows11` → `modernwindows` →
  `windowsvista`（按系统版本优先），macOS 用 `macos` → `fusion`。手写 QSS 只允许保留极少量品牌点缀
  （≤40 行），其余交给原生样式与 `QPalette`。
- **例外（2026-10-18 决策）**：外壳改用 vendored 的 `calvinhxx/Fluent-Qt`（MIT，Qt Widgets，
  WinUI3/Fluent 风格），开关 `-DDHW_UI_FLUENT=ON/OFF`（默认 OFF 保持旧外壳可构建）。
  官方放映画面仍自绘，不受主题影响；改动只允许在 `apps/qt/`。其他第三方主题库仍不引入。
- 被否掉的方案，别再重提：`zhiyiYo/PyQt-Fluent-Widgets`（QFluentWidgets）是 **Python + GPL-3.0**，
  我们的外壳是 C++ Widgets，用不了且传染性授权与 MIT 冲突；`zhuzichu520/FluentUI` 是 QML/MIT，
  要用得把三页用 QML 重写；`mowangshuying/FluentUI` 是 Widgets/LGPL-2.1，可用但不引入。
- **外观只跟随系统**：`LocalSettings::appearance` 仅为兼容旧数据保留，程序不再强制浅/深
  （强制方案在部分样式下无效：样式自绘的 surface 会忽略应用调色板）。
- **放映画面永远是自绘的**（`DrawCmd` → Qt 渲染层），不受控件主题影响；换主题只影响编辑/设置/课表三页。

## 改动的正确姿势

- **视觉数值（字号、徽章、圆角、内边距、行距系数、自适应上下限）**：加到 `LayoutStyle`
  （`core/include/dhw/layout.h`）并接到 `core/src/layout.cpp`，**不要在 layout.cpp 里写死**。
  用户可用数据目录的 `style.json` 覆盖；`--dump-style` 导出模板；`tests/style_test.cpp` 锁住默认值。
- **版式结构/规则**：改 `core/src/layout.cpp`，同时补 `tests/layout_test.cpp` 的不变量断言
  （不越画布、不越卡片、编号规则、截断、层级）。**不要**在渲染器里做排版。
- **领域规则**（上学日、今天、建议、公告、视图模型）：改 `core/schedule.cpp` + `tests/schedule_test.cpp`。
- **持久化/文件布局**：改 `core/store.{h,cpp}` + `tests/store_test.cpp`；路径一律来自 store。
- **渲染语义**：`DrawCmd::text` 的 `y` 是**墨迹垂直中心**（不是基线），新后端必须按此实现，
  否则整版会偏。

## 不能破坏的不变量（都有测试守护）

1. **字号层级**：日期主标题 ≥ 卡片标题 > 公告 ≥ 作业正文 > 课表/演讲正文
   （`layout_type_hierarchy_is_never_inverted`）。
2. **编号体系**：英语A/英语B 合占一个编号但各占一行；同学科多条行内「序号 + 空格」；
   只有一条时不显示序号；「要交」是橙色圆勾图元且整体不拆行、仅要交模式不画。
3. **数据兼容**：`data.json` 的字段名/类型/顺序冻结；未知字段忽略、缺失取默认；
   `fixtures/` 四份旧数据必须能读能写能往返（`model_reads_every_bundled_fixture`）。
4. **原子保存**：`data.json.tmp` → fsync → rename，失败不留半截文件；
   备份最多 30 天且同日只留最新；损坏时按备份新→旧恢复并回写。
5. **屏幕与导出一致**：外壳 `render_current_png()` 与 `dhw_render_cli` 在同一输入下逐字节相同。
6. **无 UI 依赖**：`core/` 编译不需要 Qt（`-DDHW_BUILD_QT=OFF` 也要能构建并跑测试）。

## 已知未完成（接手时可做）

- 常驻与单实例：旧版「关窗隐藏 + 再双击秒开 + 唤醒后按当前时间重新定位」尚未在新外壳实现。
- 侧边栏缩略图拖拽交给外部程序（微信）需要落地成真实文件。
- 全局 tooltip（400ms 延迟）、设置页/课表总览页的部分交互仍在补齐。
- Windows 侧目前只在 macOS 上做了交叉验证（编译期），真机验证仍需在 Windows 上跑
  `scripts/build-windows.ps1`。
