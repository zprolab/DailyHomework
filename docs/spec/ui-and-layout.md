# UI 与放映版式规格（删代码前的提取）

> 本文是 C++20 + CMake + Qt 6 单一代码库重写前的**规格冻结文档**，只描述「必须被复现的行为」。
> 来源：只读通读 `src/` 前端源码（App.vue、api.ts、draft.ts、layout.ts、render-canvas.ts、image.ts、
> quit.ts、nav.ts、icons.ts、theme.ts、tooltip.ts、fit.ts、text-export.ts、clipboard.ts、utils.ts、
> types.ts、variant.ts、style.css、main.ts，views/ 与 components/ 全部），并与根目录 `AGENTS.md`
> 交叉验证。
>
> **冲突时以代码为准**，本文档已在相应位置标出与 `AGENTS.md` 不一致的两处（见 §3.6、§8.1）。
>
> 术语：**页面日**（`view.date`，作业所属的那一天）≠ **画面日**（`vm.display_date`，仅要交模式 =
> 页面日的下一上学日）。

---

## 1. 页面与导航

### 1.0 应用外壳（App.vue / api.ts / main.ts）

- 启动：`main.ts` 装全局自绘 tooltip（`installTooltip()`）后挂载 `#app`。
- 数据未就绪时整屏只显示一行灰字：**「加载中…」**（`.loading`）。
- 就绪后分两条互斥分支：
  - `mode === "present"` → 只渲染 `PresentView`（整屏 canvas，无侧边栏）。
  - 否则 → `.shell` = `Sidebar` + `.main`；`.main` 里按 `ui.page` 三选一：
    `settings` → SettingsView，`week` → WeekView，其余 → EditView。
- 路由状态：`ui.page: "edit" | "settings" | "week"`；`openPage(page)` 是 **toggle**：
  再点当前页会回到 `edit`。侧边栏导航用 `ui.page = page`（不 toggle）。
- toast：`showToast(text)` 覆盖显示在底部居中，**2600ms** 后自动清空；同时只显示一条
  （后一条替换前一条）。`.toast` 无交互（`pointer-events: none`）。
- 进入放映：`enterPresent()` = `refreshVm()` → 读本机设置里的 `screen_device`（非空才
  `set_present_monitor`）→ `set_presenting(true)` → `mode = "present"`。
  退出：`mode = "edit"` + `set_presenting(false)`。
- `App.vue` 每 **30s** tick 一次 `tickRelocate()`：拿后端 `get_launch_view()`，若「日期或模式跨越了」
  且用户当前正停在上一次自动定位结果上（`view.date/submitOnly === lastLaunch`），就跟随自动定位并 toast
  「已切换到今天早上（仅显示要交）」/「已切换到今天（显示全部）」。放映中不触发。
- 收到后端 `activated` 事件（关窗后再双击 exe 唤出）＝一次「重启」：重新定位（等价 Ctrl+T）+
  `refreshSidebarDefault()`（侧边栏回到本机设置的默认值）+ `pruneEmptyDraft()`（丢掉只选了学科没写内容的空条目）
  + `requestAnnScrollBottom()`（公告滚到底）；放映中忽略。
- 收到 `auto-image-request` 事件 → `generateImage(false)`（自动出图，见 §5）。
- 内置显示大小：`ui_scale ∈ [50%,150%]` 写到 `document.documentElement.style.zoom`；
  **放映中写空串（=1）**。`prefetchIdle()` 在空闲时预加载 PresentView 分包。

### 1.1 编辑页（views/EditView.vue）

布局：`编辑页 = 顶部工具栏(52px) + .cards 网格`。

`.cards` 网格：两列 `minmax(0,1.45fr) minmax(0,1fr)`，行 `auto minmax(0,1fr)`，gap 12px，
padding `12px 16px`。第一行是**公告卡（跨两列）**，第二行左列 = **作业卡**，右列 = `.rightcol`
（纵向 flex，gap 12px，内含**课表卡在上、演讲卡在下**；两张卡 `flex: 1 1 auto` 分摊右列剩余高度）。

工具栏自左向右（**顺序即视觉顺序，必须保留**）：

| 位置 | 控件 | 图标 | aria-label | data-tip（逐字） | 行为 |
|---|---|---|---|---|---|
| 1 | 汉堡按钮 | `hamburger` E700 | 收起侧边栏 | `收起 / 展开侧边栏（Ctrl+B）` | `toggleSidebar()`（不落盘） |
| 2 | 品牌文字 | — | — | — | 静态「每日作业」（16px 粗体蓝） |
| 3 | 上一天 | `chevronLeft` E76B | 上一天 | `上一天（←）` | `emit('nav', -1)` |
| 4 | 日期栏 `.days` | — | — | `滚轮可左右滚动日期栏` | 横向 chip 条，见下 |
| 5 | 下一天 | `chevronRight` E76C | 下一天 | `下一天（→）` | `emit('nav', +1)` |
| 6 | 模式分段 | — | — | — | Segmented：`全部` / `仅要交` |
| 7 | 回到今天 | `mapPin` E707 | 回到今天 | `回到今天（Ctrl+T）` | `emit('today')` |
| 8 | 放映（主按钮 primary） | `play` E768 | 全屏放映 | `全屏放映（F5）` | `emit('present')` |
| 9 | 出图 | `download` E896 | 生成图片 | `生成当前这一天的图片（Ctrl+D）` | `generateImage(true)` |
| 10 | 完全退出（danger） | `close` E711 | 完全退出 | `完全退出（Ctrl+Q）：先生成今天的作业图片，然后退出程序` | `quitApp()`；点击后按钮禁用 |

日期栏 chip：
- 文案逐字为 `${M}.${D} 周${中文星期}`（例如 `10.1 周三`，月/日不补零）。
- `data-tip` = `编辑 ${YYYY-MM-DD}`。
- 状态类：`today`（= `store.today`，蓝描边+蓝字+粗体）、`current`（= `view.date`，蓝底白字）。
- 点击 = 切天：`view.date = d; view.submitOnly = false; refreshVm()`（**切天一定回到「全部」模式**）。
- 滚轮：`deltaX` 绝对值大于 `deltaY` 时用 `deltaX`，否则用 `deltaY`，加到 `scrollLeft`
  （只横滚这一条栏，**不切天**）；`scrollbar-width: none`，滚动条隐藏。
- 切天后 `nextTick` 把 `.chip.current` `scrollIntoView({block:'nearest',inline:'nearest'})`。
- 日期栏内容 = 有内容的天（作业或演讲非空）+ 今天 + 下一上学日 + `pinned_dates`（★ 快捷显示）；
  隐藏且无条目的日期被剔除；正在编辑的那天只要有条目就始终保留。

模式分段：`全部` / `仅要交`。切换只改 `view.submitOnly` 后 `refreshVm()`，**不改 `view.date`**
（避免把正在编辑的草稿切走）；若值没变直接返回。

### 1.2 设置页（views/SettingsView.vue）

外壳：`.page-head`（52px）= 汉堡（aria「收起侧边栏」，tip 同编辑页）+ 返回（`chevronLeft`，aria「返回」，
tip `返回编辑页（Esc）`）+ `<h2>设置</h2>`。正文 `.page-body` 内 `.wrap`（max-width 920px，纵向卡列，gap 14px）。

**卡片 1「常规」**（`h3` 只有「常规」一个文本节点）：

| 字段标签（逐字） | 控件 | 说明文案（逐字） |
|---|---|---|
| 外观 | Segmented：`浅色` / `深色` / `跟随系统` | `只影响编辑页/设置/课表；放映画面固定浅色` |
| 侧边栏默认收起 | Toggle | `只决定下次启动（冷启动/唤出窗口）时收不收；运行中用 Ctrl+B 或汉堡收起不会改这里` |
| 数据目录 | 只读文本框（显示 `store.dataDir`）+ 按钮 `打开文件夹` + 按钮 `更换…` | 独立一段说明：`默认用 exe 同级的「每日作业数据」；程序不在 APPDATA 等系统目录写任何东西，也**不记住**你选的目录——每次启动都从 exe 同级开始，那里没有数据就会问你新建还是指定。` |
| 出图间隔（秒） | Stepper（±10 秒，夹在 10~3600） | `修改后超过该时长自动生成家庭作业图片` |
| 早晨分界线 | `<input type="time">`（任意 HH:MM，**不是步进器**） | `早于该时间打开自动进入「仅要交」` |
| 班徽（仅通用版 `GENERIC`） | 按钮 `上传班徽…` + 按钮 `删除班徽`（无班徽时禁用）+ 状态文字 | 状态：`已上传（存在数据目录的 班徽.png）` / `还没上传班徽`；下方独立说明：`建议上传**透明背景**的 PNG 图片（例如 512×512），放在蓝色标题带上才不会出现一块白底。` |
| 放映时显示班徽 | 复选框；通用版在没上传时禁用 | 通用版未上传时附注：`上传班徽后才能打开` |
| 英语分 A/B 班 | 复选框 | `开启后作业/演讲的学科建议会把「英语」拆成 英语A / 英语B` |
| 显示大小 {N}% | range，min 50 max 150 step 5，拖动即时预览（`previewScale`）、松开 `save` | `与系统显示缩放叠加，只影响编辑页/设置/课表，放映页保持原样` |
| 清理图片 | 按钮 `清理 exe 同级图片` | `把 exe 同级里除今天以外的家庭作业图移入数据目录的「家庭作业图片」存档` |

**卡片 2「放映」**（`h3` 内 hint：`这些设置只对本机生效，不写进共享的 data.json`）：

| 字段 | 控件 | 说明 |
|---|---|---|
| 放映时不熄屏 | Toggle（`keep_awake`） | `全屏放映期间不让显示器睡眠、也不让系统休眠` |
| 放映时锁定切换 | Toggle（`lock_switching`） | `放映时吞掉 Alt+Tab 与 Win 键（Ctrl+Shift+Esc 仍可用）` |
| 放映屏幕 | Select（首项 `自动（窗口当前所在屏幕）`，其余 `名称（宽×高）`；记着的那块屏不在了自动回到「自动」） | `投到哪一块显示器；「自动」= 窗口当前所在的那块` |

**卡片 3「天数管理」**（`h3` 内 hint 为长段逐字文案）：
`调休补课日用；「上哪天的课」决定课表模版来源；列表只列工作日（周末调休日用「新增日期」加）；无内容的日期可「删除」出列表并「全部恢复」；编辑页日期栏只列「有作业/演讲内容的天 + 今天 + 下一上学日 + ★」；「快捷显示」只让那天在首页日期栏多一格，不改任何判定`

- 新增行：`新增日期` + `<input type="date">` + Select（`按当天星期` / `上周一的课` / `上周二的课` /
  `上周三的课` / `上周四的课` / `上周五的课`）+ 主按钮 `新增`（日期为空时禁用）。
  新增后 toast：`已新增 {date}（有作业内容后才会出现在编辑页日期栏，也可用★快捷显示）`。
- 日期行列表 `.day-list`（max-height 240px，内部滚动；挂载时默认滚到 `[data-date] = store.today` 那行，
  今天不在列表里就滚到第一个 ≥ 今天的行）：
  - 行文本 `${date}　周${X}`；`store.today` 那行额外一个灰字 `今天`；正在编辑的那天额外灰字 `当前正在编辑`。
  - Select：调休来源（`按当天星期`/`上周X的课`）；**没有条目的日期改这个下拉会现建一个空条目**
    （`emptyDay()`），于是这行随即获得「删除」按钮。
  - 删除按钮语义分两种（两边都叫 `删除`）：
    - **该天有条目** → 两步确认：先点 `删除` → 变成 `确认删除` + `取消`，确认后 `delete_day`，
      toast `已删除 {date}`；若删的正是当前页面日则跳到 `store.today` 并回「全部」。
    - **该天没有条目** → 写入 `hidden_dates` 把它移出列表（`title` = `这一天还没有内容，删除只是把它从列表里移出（可全部恢复）`），
      toast `已把 {date} 移出列表`。
  - ★/☆ `快捷显示` 按钮：**只对「本来不会进首页日期栏」的行显示**
    （判据 `inBarByDefault(d) = dayHasContent(days[d]) || d===today || d===nextSchoolDay`）。
    title：`已在首页日期栏显示，点击取消（只影响显示）` / `在首页日期栏里给这一天留一格入口（只影响显示，不改调休/休息日/工作日/今天）`；
    toast：`{date} 已显示在首页日期栏` / `已取消 {date} 的快捷显示`。
  - 底部（有被移出的日期时）：`已移出列表 N 天` + 按钮 `全部恢复` → toast `已恢复移出列表的日期`。
  - 列表构成：已有条目的日期 + 真实今天前 7 天到后 60 天里的**工作日**（周一到周五）+ `store.today`；
    `hidden_dates` 里没有条目的日期剔除。周末只有「已有条目（调休）」时才出现。

**卡片 4「快捷键」**（`h3` hint：`编辑时可以不离开键盘：Alt+数字跳区、回车加条目、Ctrl+回车标记`）
14 行，逐字：

```
Alt+1~4 跳到 公告 / 作业 / 演讲 / 课表
Enter 在输入框里新建下一条（课表跳下一节）
Ctrl+Enter 标记当前条目（要交 / 公告完成）
Alt+↑↓ 在条目之间上下切换
←→ 切换上一天 / 下一天
F5 进入放映 / 退出放映
Ctrl+Z 撤销（Ctrl+Shift+Z 重做）
Ctrl+T 回到今天
Ctrl+D 立即生成图片
Ctrl+K 课表总览
Ctrl+S 立即保存
Ctrl+Q 完全退出
Ctrl+, 设置
Esc 退出放映 / 从设置、课表总览返回
```

**卡片 5「关于」**：`每日作业 v{version}`（`__APP_VERSION__` = package.json version，当前 `1.5.5`）+
灰字 `数据文件：{dataDir}\data.json（自动备份至 backups\）`。

保存语义：设置页**即时保存，没有「保存设置」按钮**（`update_settings` 后 toast `设置已保存`）。

### 1.3 课表总览（views/WeekView.vue）

- 头部：汉堡（tip 同前）+ 返回（aria「返回」，tip `返回编辑页（Esc）`）+ `<h2>课表总览</h2>` +
  spacer + hint `本周（{周一日期} 起）；调课显示为红色` + 按钮 `修改周课表模版`（编辑中隐藏）。
- 首页视图：7 列 × 9 行 `<table class="week">`（圆角 12px 且表头一起圆角，**不要用直角表头盖掉上两角**）。
  表头：第一列空，其后 `周一`…`周日` + 换行灰字 `MM-DD`；今天那列背景 `--blue-soft`。
  行首列是节次 1~9（灰字，背景 `--head`）；调课的格子红字加粗（`.changed`）。
  加载中显示 `正在加载本周课表…`。
  图例：红点 + `红色 = 该日单独调课（在编辑页或设置 → 天数管理中修改）`。
- 模版编辑态：表头变成 `周一`…`周日`，每格一个文本框；底部右侧 `取消` + 主按钮 `保存模版`
  （保存后 toast `周课表模版已更新` 并重载首页视图）。

### 1.4 放映页（views/PresentView.vue）

- `.present` 固定全屏（`position: fixed; inset: 0`），底色黑；一块 `.present-canvas` 铺满。
- 绘制：`canvas.width/height = 客户区 × devicePixelRatio`，`ctx.setTransform(dpr,0,0,dpr,0,0)`，
  先整块填黑，再 `scale = min(cw/1920, ch/1080)` 等比缩放并平移居中（**黑边 letterbox**），
  然后在缩放后的坐标里跑 `layout(vm, show_badge && badgeHasInk())` → `renderCmds`。
- 重新绘制的时机：`vm` 变化、窗口 resize。
- **禁切换**：放映中不响应编辑页的快捷键（只剩 F5 / Esc / Ctrl+Q）。
- 鼠标指针：默认 `cursor: none`（隐藏）；右键菜单打开时 `.menu-open` 恢复 `cursor: default`。
- 右键菜单（`@contextmenu` 时 `preventDefault`）逐字条目与行为：

| 顺序 | 文案 | 行为 |
|---|---|---|
| 1 | `切换为显示全部` / `切换为仅要交`（按当前 `submitOnly` 取反） | 翻转 `view.submitOnly` → `refreshVm()` → toast `已切换为仅要交` / `已切换为显示全部` |
| 2 | `生成作业图片` | `generateImage(true)` |
| 3 | `复制作业图片` | 渲染当前 1920×1080 PNG 写剪贴板；成功 toast `作业图片已复制到剪贴板`，失败回退成出图 + toast `复制失败，已生成图片：{path}`，异常 toast `复制作业图片失败：{e}` |
| 4 | `复制文字版作业` | `slidePlainText(vm)` 写文本剪贴板；toast `文字版作业已复制到剪贴板` / `复制到剪贴板失败` |
| — | 分隔线 | — |
| 5 | `放映屏幕 ▸` | 悬浮或点击展开右侧独立子菜单 |
| — | 分隔线 | — |
| 6 | `退出放映`（danger） | 关菜单 + 退出放映 |

- 子菜单（右侧**独立绝对定位浮层**，不参与主菜单布局、不撑大主菜单；宽度同为 232px）：
  - 选项：`自动（窗口当前所在屏幕）` + 每块显示器 `名称（宽×高）`（是当前屏再补 ` · 当前`）；
    选中项前缀 `● ` 并加粗蓝字。
  - 右侧空间不足（`vw - menu.right - 6 < 子菜单宽`）→ 翻到主菜单**左边**（`left:auto; right:100%`）；
    底部放不下 → 整体上移 `subShift`，保证整块在屏幕内。
  - 鼠标移出「放映屏幕」或子菜单 **180ms** 后才收起（给移进子菜单留时间）。
  - 选择后关菜单 + `set_present_monitor`；toast `放映时投到：{name}` / `放映时用窗口当前所在的屏幕`。
- 主菜单整体夹进屏幕：先按上一帧量到的尺寸夹一次，渲染后 `nextTick` 用真实尺寸再校正一次
  （边距 8px；`.menu` 宽度 232px，`border-radius: 12px`，`box-shadow: 0 14px 36px rgba(0,0,0,.38)`）。
- **无操作 6000ms 自动收起**（`MENU_IDLE_MS = 6000`；鼠标每次 move 都重新计时）；
  点击菜单外任意处立即收起；菜单开着时 Esc 只关菜单、不退出放映（捕获阶段 `stopPropagation`）。

### 1.5 侧边栏（components/Sidebar.vue）

宽度 190px，`linear-gradient(180deg, var(--sidebar-top), var(--sidebar))`，右边框 1px；
收起时宽度 0（transition 0.18s cubic-bezier(0.4,0,0.2,1)）。自上而下：

1. 应用名 `每日作业`（粗体蓝 16px）。
2. 分组标题 `页面`（12px 灰）。
3. 三个导航按钮（图标 + 文案）：`编辑`（E70F）/ `课表总览`（E787）/ `设置`（E713）；
   `data-tip`：编辑页 → `编辑（Alt+1~4 在条目间跳）`，其余 → 自己的 label；当前页 `.on`（蓝底蓝字粗体）。
   点击 `ui.page = page`（不 toggle）。
4. 底部脚注 `.foot`：圆点（蓝 = 已保存，橙 = 未保存 `draftPending`）+ 数据目录**末级文件夹名**
   （取不到时显示 `每日作业数据`）；`data-tip` = `数据目录；圆点 = 已保存 / 未保存`。
5. 放映缩略图 `.thumbbox`：140×78.75（16:9），1px 描边，圆角 7px，底色 `#f8faff`；
   `data-tip` = `点一下出图 · 拖到微信即可发送`，`<img alt="作业放映预览">`。
   - 内容是 `layout()` 渲出的 1920×1080 PNG 的 ObjectURL（**预渲染 `<img>`**，
     `vm` 或 `draftPending` 变化时重渲；重渲用 `rendering/again` 合并重叠调用）。
   - 单击 = `generateImage(true)`（手动出图）。
   - 拖拽：只往 `dataTransfer` 塞一个**真实 `File`**（`家庭作业_{view.date}.png`，`effectAllowed='copy'`），
     拖拽预览图用 `<img>` 自身（偏移 70,39）；`img` 设 `pointer-events:none; -webkit-user-drag:none`，
     拖拽源是外层 `.thumbbox`。**不要** `setData("DownloadURL"/"text/uri-list")`。

### 1.6 卡片通用结构（style.css）

- `.card`：白底、1px `--border` 描边、圆角 12px、padding `12px 16px`、纵向 flex、`min-height: 0`。
- `.card > h3`：15px 蓝字，两端对齐（左 `.card-head`，右 `.hint`）。
- `.card-head` = `.card-icon`（26×26 圆角 8px、`--blue-soft` 底、图标字族 14px）+ 名称 + `.badge`
  （12px 灰字、`--bg` 底、1px 描边、胶囊，文案见各卡）。
- `.hint`：12px 灰字、右对齐、normal 字重。
- `.card-body`：`flex:1; min-height:0; overflow-y:auto; display:grid; grid-template-columns:1fr 1fr;
  gap:0 12px; align-content:start`（课表/演讲卡的两列网格）。
- `.edit-row`：横向 flex，gap 10px，`padding: 2px 0`；`.sub`（行首标签列）`min-width: 64px` 右对齐。
- `.warn-banner`：`--danger-soft` 底、红字粗体、1px `#f3c1c3` 描边、圆角 8px。
- 复选框是自绘样式（17×17、圆角 5px、选中蓝底 + 白色对勾）。

### 1.7 四张编辑卡片的逐字文案与交互

**公告卡（AnnouncementCard.vue）** —— 跨两列：
- 标题：图标 `megaphone` E789 + `公告`；徽标（未完成数 > 0 时）`{N} 条未完成`。
- hint：`所有日期共用这一份公告；勾选完成后放映不再显示；回车切换完成，文字可直接修改`。
- 列表 `.ann-list`：**固定显示 4 行**（`max-height: 176px`，行高固定 44px，多出的在这块内滚动；
  行高固定所以切断正好落在行边界），挂载时、条目数变化时、收到 `annScrollTick`（热启动）时都滚到最底。
- 每行：复选框（无文案，`title="完成（回车切换）"`，`data-nav="ann"`）+ 可**就地编辑**的文本框
  （无 placeholder，300ms 防抖整体 `set_announcements`，只改 `text`、不动 `id`/`done`）+
  删除按钮（`close` E711，aria「删除这条公告」，tip `删除这条公告`）。
  已完成行为：文本删除线 + 灰字。
- 末尾**新增行**（`showGhost` 无条件为真，公告卡永远保留一行）：
  灰掉的禁用复选框（`title="保存后即可勾选完成"`）+ 输入框（`placeholder="新公告，回车或点「添加」保存…"`，
  `data-nav="ann"` 且 `data-nav-new="ann"`）+ 按钮 `添加`（内容为空时禁用）。
  提交 = `trim()` 非空即 `add_announcement`，之后行清空继续可写。
- 键盘：条目复选框上 `Enter` = 切换完成；条目文本框里 `Enter` = 跳到末尾新增行。

**作业卡（HomeworkCard.vue）**：
- 标题：图标 `book` E736 + `作业`；徽标（写有内容的条目数 > 0 时）`共 {N} 条`。
- hint：`一个学科可加多条，每条单独勾「要交」；回车加一条，Ctrl+回车切换要交；留空不显示`。
- 结构：`.hw-list`（`flex:1; min-height:0; overflow-y:auto`）→ 每学科一个 `.hw-group`，
  网格 `64px minmax(0,1fr)`（学科列 64px、gap 10px），组之间 1px 上分隔线。
  学科名右对齐、14px、`padding-top: 8px`、不换行。
- 学科行来自「学科清单」= 作业建议（`homework_suggestions`）→ 草稿里已有的学科 → 本会话记住的自定义学科；
  某学科没有条目时**也渲染一行空行**让用户直接输入。
- 每条作业一行：序号 `idx`（只有 `row 存在 && 该学科条数 > 1` 才 `visibility:visible`，否则 `hidden`；
  最小宽 18px 右对齐、蓝字 13px 粗体、tabular-nums）+ 文本框（`placeholder="作业内容…"`，`data-nav="hw"`）+
  `要交` 复选框（label 文案逐字 `要交`）+ 删除按钮（`close`，aria「删除这一条」，tip `删除这一条`；
  仅当 `有条目 && (条数>1 || 本条有内容)` 可见且可 tab 到）+ ＋按钮（`plus` E710，aria「再加一条」，
  tip `给{学科}再加一条作业（回车）`，只有该学科最后一行可见）。
- 键盘与语义：
  - `Enter` → 给同学科再加一条并聚焦（`addEntry`；若原来 0 条则一次加 2 条——先兑现眼前空行）。
    `Alt+Enter` 不处理。
  - `Ctrl/Cmd+Enter` → 切换本条 `submit_next`（先从空行 `materialize` 成真实条目）。
  - 删除某学科最后一条 → **行保留**（学科不消失），只是回到可输入的空行。
  - 空条目（只选学科、没写内容）不落盘（草稿侧 `pruneEmptyDraft` + 后端 `normalize()`）。
- 末尾空白行（ghost）：见 §8.3；学科框 92px 宽（作业卡），占住上面「学科列 + 序号列」的宽度；
  隐藏的删除/＋按钮占位，保证输入框右边界与上面一致。

**课表卡（ScheduleCard.vue）**：
- 标题：图标 `calendar` E787 + `课表（{基准星期}）`，调休时追加 `·调休`
  （`课表（周三·调休）`）；徽标（当天调课节数 > 0）`{N} 节调课`。
- hint：`修改自动标红，改回模版自动取消；回车跳下一节`。
- `.card-body` 两列网格，9 行：行首 `.sub` 显示节次 `1`…`9`，输入框
  `placeholder` = 该节模版课程名，模版为空时 `（空）`；输入值 = 覆盖值，等于模版或空串则写回 `null`；
  覆盖行加 `.overridden`（红描边 + 红粗字）。
- 键盘：`Enter`（无修饰键）→ 聚焦下一节并 `select()`。
- 9 个输入框共用一个 `draft`，每次输入 `saveDraft()`（300ms 防抖）。

**演讲卡（SpeechCard.vue）**：
- 标题：图标 `mic` E720 + `演讲提醒`（无徽标）。
- hint：`按下一上学日课表预填学科；同学科多学号用空格分隔；回车补一个学科`。
- 红色横幅（`vm.speech_warn`，即演讲全空时）：`⚠ 演讲提醒未填写，请填写下一上学日的学号`。
- 行 = 建议学科（`speech_suggestions`）逐个 + 草稿里多出来的学科（可删）：
  `.sub` 学科名 + 文本框（`placeholder="学号"`，`data-nav="speech"`）+ 多出来的学科带删除按钮
  （aria「删除这个学科」，tip `删掉这个学科的演讲提醒`，仅在写了学号时可见）。
- 键盘：`Enter` → 取 `ALL_SUBJECTS` 里第一个未用学科补一行并聚焦。
- 末尾 ghost 行跨满两列；学科框固定 **64px**（正好占住左列「学科」标签那一格，
  触发器内边距收窄 `padding:0`、文字右对齐 `padding:5px 4px; text-align:right`），
  于是它的输入框与左列各行的输入框同一条竖线。

### 1.8 通用控件（components/ui/）

- **Segmented**：胶囊分段滑块（`padding:3px`，指示块 `--card` 白底 + `0 1px 3px rgba(20,26,38,.18)`，
  `transform: translateX(calc(index*100%))`，transition 0.2s）；选项文字 13px，选中蓝字粗体，未选中灰字。
- **Toggle**：42×24 胶囊 + 18px 圆形 knob（`left:2px → 20px`，0.15s）；`role="switch"`、
  `aria-checked`、`aria-label="开关"`；关闭底色 `#dfe4ee`，打开 `--blue`；disabled 0.5 透明。
- **Stepper**：高 28px 白底圆角框；左侧值区（min-width 74px、13px），右侧 22px 宽上下箭头列
  （`chevronUp` E70E / `chevronDown` E70D，aria「增加」/「减少」）。步长与边界由调用方给。
- **Select**：见 §8.2。

### 1.9 toast / 对话框 / 提示全表

toast（2600ms，覆盖式）：

| 触发 | 文案 |
|---|---|
| Ctrl+S | `已保存` |
| 撤销 / 重做 | `已撤销：{action}` / `已重做：{action}` |
| 没有可撤销 / 可重做 | `没有可撤销的操作` / `没有可重做的操作` |
| 自动定位跨越 | `已切换到今天早上（仅显示要交）` / `已切换到今天（显示全部）` |
| 手动出图成功 / 失败 | `已生成：{path}` / `生成图片失败：{e}` |
| 完全退出 | `正在生成今天的作业图片…` |
| 放映切模式 | `已切换为仅要交` / `已切换为显示全部` |
| 复制作业图片 | `作业图片已复制到剪贴板` / `复制失败，已生成图片：{path}` / `复制作业图片失败：{e}` |
| 复制文字版 | `文字版作业已复制到剪贴板` / `复制到剪贴板失败` |
| 放映屏幕 | `放映时投到：{name}` / `放映时用窗口当前所在的屏幕` |
| 设置页 | `设置已保存` / `保存失败：{e}` |
| 出图间隔/早晨分界线/显示大小/班徽开关等改动 | 走 `update_settings` 后 `设置已保存` |
| 数据目录 | `打开失败：{e}` / `数据目录已迁移到：{dir}` / `迁移失败：{e}` |
| 清理图片 | `已把 {N} 张图片移入「家庭作业图片」存档` / `没有需要清理的图片` / `清理失败：{e}` |
| 班徽（通用版） | `班徽已上传；「放映时显示班徽」已打开` / `班徽已删除` / `还没有上传过班徽` / `上传失败：{e}` / `删除失败：{e}` |
| 天数管理 | `已新增 {date}（有作业内容后才会出现在编辑页日期栏，也可用★快捷显示）` / `新增失败：{e}` / `已删除 {date}` / `已把 {date} 移出列表` / `已恢复移出列表的日期` / `{date} 已显示在首页日期栏` / `已取消 {date} 的快捷显示` / `{date} 的课表已按当天星期` / `{date} 的课表已按{上周X的课}` |
| 周课表模版 | `周课表模版已更新` |
| 后端通知（`StatePayload.notice`） | 逐字由后端给出（如备份恢复提示），前端原样 toast |

对话框：前端**没有**自绘对话框。原生对话框（数据目录选择、启动时「新建 or 指定目录」、班徽文件选择、
换目录选择）都在 Rust 侧，重写时按同一语义保留：**两步确认**这类交互在设置页天数管理里用行内按钮实现
（`删除` → `确认删除` + `取消`），不要引入模态框。

---

## 2. 快捷键全表

全部在 `App.vue` 的 window `keydown` 上集中处理（顺序即优先级）。

### 全局

| 组合键 | 生效范围 | 行为 |
|---|---|---|
| `Ctrl+Q`（也接受 `Cmd+Q`） | 任何页面、放映中 | 完全退出：`flushDraft()` → 重出今天图片 → `quit_app` |
| `F5` | 编辑/设置/课表 | 进入放映（`enterPresent`） |
| `F5` / `Esc` | 放映中 | 退出放映（`F5` 与 `Esc` 等价） |
| `Esc` | 设置 / 课表总览 | 回编辑页 |
| `Esc` | 放映且右键菜单开着 | **只关菜单**（捕获阶段吞掉，不退出放映） |
| `Esc` | 编辑页 | 无动作 |
| `Alt+1` | 编辑页 | 跳到公告区（**特判**：末尾 `data-nav-new` 的「新增公告」输入框） |
| `Alt+2` | 编辑页 | 跳到作业区第一条 `[data-nav="hw"]` |
| `Alt+3` | 编辑页 | 跳到演讲区第一条 `[data-nav="speech"]` |
| `Alt+4` | 编辑页 | 跳到课表第一条 `[data-nav="sched"]` |
| `Alt+↓` / `Alt+↑` | 编辑页 | 在 `[data-nav]` 条目间上下移动（DOM 顺序 = 阅读顺序），到头/到尾夹住；当前没有 `[data-nav]` 焦点时进第一条/最后一条 |
| `Ctrl+B` | 全局（非放映） | 收起 / 展开侧边栏（**不落盘**） |
| `Ctrl+Z` | 全局（非放映） | 撤销 |
| `Ctrl+Shift+Z` / `Ctrl+Y` | 全局（非放映） | 重做 |
| `Ctrl+S` | 全局（非放映） | 立即保存（`save_now`） |
| `Ctrl+D` | 全局（非放映） | 手动出图（当前屏幕所见） |
| `Ctrl+T` | 全局（非放映） | 回到今天（后端 `get_launch_view` → 应用） |
| `Ctrl+K` | 全局（非放映） | 课表总览（已在课表总览则回编辑页） |
| `Ctrl+,` | 全局（非放映） | 设置（已在设置页则回编辑页） |
| `←` / `→` | 仅编辑页，且焦点不在输入框/文本域/select/contenteditable | 上一天 / 下一天（在日期栏内夹取，切天回「全部」） |

注意：**`Alt+数字` 与 `Alt+↑↓` 在输入框里也会触发**（该分支在「正在打字」判断之前）；
撤销/重做/保存/出图/定位/切页等 `Ctrl+*` 同样优先于输入框。

### 条目级

| 组合键 | 位置 | 行为 |
|---|---|---|
| `Enter` | 公告条目复选框 | 切换完成（不区分修饰键） |
| `Enter` | 公告条目文本框 | 跳到末尾「新增公告」输入框 |
| `Enter` | 新增公告输入框 | 保存（trim 非空），行保留继续写 |
| `Enter` | 作业文本框 | 同学科再加一条并聚焦；`Alt+Enter` 不处理 |
| `Ctrl+Enter`（或 `Cmd+Enter`） | 作业文本框 | 切换本条「要交」 |
| `Enter` | 演讲文本框 | 补下一个未用学科并聚焦（`Alt`/`Ctrl`/`Cmd` 组合不处理） |
| `Enter` | 课表文本框 | 聚焦下一节并全选（`Alt`/`Ctrl`/`Cmd` 组合不处理） |
| `Enter` | Select（未打开） | 打开列表 |
| `↓` / `Space` | Select（未打开） | 打开列表 |
| `↑` / `↓` | Select（打开） | 移动高亮项 |
| `Enter` | Select（打开，普通模式） | 选中高亮项 |
| `Esc` | Select（打开） | 关闭浮层（阻止冒泡，不触发页面 Esc） |
| `↑` / `↓` | Select **editable** | 打开并/或移动高亮，`navigated = true` |
| `Enter` | Select **editable** | 若已用 ↑/↓ 翻到选项 → 选中该选项；否则**优先提交自己打的文本**（自定义学科） |

注：`AGENTS.md` 行为规则 9 说「`Ctrl+Enter` 标记当前条目（作业要交 / 公告完成）」——
代码里公告复选框是**裸 `Enter`** 即切换完成，`Ctrl+Enter` 也能切换（未排除修饰键）。

---

## 3. 放映版式规格（1920×1080）

### 3.0 渲染契约（layout → render 分离）

`layout(vm, showBadge)` 只产出与渲染无关的 `DrawCmd[]`，`render-canvas` 负责屏幕与导出两条路径，
严格 WYSIWYG（同一份 `DrawCmd`、同一个 Canvas 引擎测量与绘制）。重写时必须保持这一分层。

```
DrawCmd =
  | { rect: x,y,w,h,r, fill?, fill2?, vertical?: false | diag?, radial?: {cx,cy,r,to}, stroke?, lineWidth?, clip? }
  | { text: x,y,text,size,bold?,color,align?: left|center|right,tracking?,icon?,clip? }
  | { path: points:[x,y][], stroke, lineWidth, clip? }
  | { image: x,y,w,h, clip? }            // 班徽：按**素材墨迹范围**等比居中
ClipRect = { x,y,w,h,r }                 // 圆角裁剪框；命令切换时压栈/出栈（只维护一层）
```

绘制约定（必须保留，否则整版视觉偏移）：

- `text.y` 是**墨迹垂直中心**（不是基线）：render 用 `textBaseline="middle"` 再补 `size * 0.07`
  （Microsoft YaHei 的中文字形墨迹中心比 em 盒中心高约 0.07em）。多行文本上下留白才相等。
- 字族：正文字体串 `"{bold?"bold ":""}{size}px \"Microsoft YaHei\", \"SimHei\", sans-serif"`；
  图标字族 `"{size}px \"Segoe Fluent Icons\", \"Segoe MDL2 Assets\""`（命令带 `icon:true` 时切换）。
- `text.align` 默认 left；`tracking` 写进 `letterSpacing`（画完复位为 `0px`）。
- `path` 用 `lineCap/lineJoin = "round"`。
- `rect.radial` 用 `createRadialGradient(cx,cy,0 → cx,cy,r)`：内圈 = `fill`，外圈 = `radial.to`；
  `fill2 + diag` = 左上→右下对角线性渐变；`fill2 + vertical!==false` = 竖直；`vertical:false` = 水平。
- `roundRect(x,y,w,h,r)` 画所有矩形（`r` 可为 0）。
- 文字测量：同一 Canvas 引擎（`measureText`）；换行是**按字符贪心**（CJK 友好），
  显式 `\n` 强制断行。
- 三处「按墨迹对齐」的辅助函数（为了视觉而不是排版对齐，必须复刻）：
  - `inkLeft`：一行墨迹相对绘制原点的左偏移（缓存）。
  - `inkLeftEdge`：**实边**左偏移 = 从布局原点起第一条纵向墨迹覆盖 ≥ 墨迹高度 45% 的列
    （中日韩首字向左探出的细笔画不计入）。
  - `inkBox`：一行墨迹的上下边界（相对绘制 y）；`centerTwoLines` 用两行**实测墨迹**把整块垂直居中，
    行间留 `gap`。
- `pushShadow`：无高斯模糊时的柔和投影，**9 层同心圆角矩形**叠出衰减：
  `spread = blur*1.5`，每层 alpha `= round(255*maxAlpha/9)/255`，第 i 层
  `inflate = spread*(1 - i/9)`，`r = radius + inflate`，矩形按画布 `[0,W]×[0,H]` 夹取，
  第 i 层的 y 再加 `dy`。`per <= 0` 直接不画。

### 3.1 版式常量（layout.ts，全部是像素）

| 常量 | 值 | 含义 |
|---|---|---|
| `W` / `H` | 1920 / 1080 | 设计画布 |
| `M` | 36 | 外边距（四边） |
| `GAP` | 22 | 卡片/区块之间的间隔；右列课表与演讲之间也用它 |
| `HEAD_H` | 132 | 顶部标题带高度 |
| `HEAD_PAD` | 28 | 标题带左右内边距 |
| `BAND_R` | 34 | 标题带圆角 |
| `PAD` | 26 | 卡片内边距 |
| `TITLE_H` | 96 | 卡片面板头高度（到分隔线） |
| `CARD_R` | 30 | 卡片圆角 |
| `CARD_STROKE` | 1.6 | 卡片描边宽 |
| `CONTENT_W` | 1848 | `W - M*2` |
| `LEFT_W` | 1464 | 左列宽（公告条、作业卡都用它） |
| `RIGHT_X` | 1522 | `M + LEFT_W + GAP` |
| `RIGHT_W` | 362 | `CONTENT_W - LEFT_W - GAP` |
| `INDENT` | 80 | 作业正文缩进（编号徽章 56 + 间隔 24） |
| `SPEECH_TAG_W` | 112 | 演讲学科标签列宽（学号都从这条竖线之后开始） |
| `LABEL_CAP_TEXT` | `"字字字字字："` | 作业标签列宽上限（5 个 CJK 字 + 全角冒号） |

派生坐标：

- 标题带：`x = M = 36`，`y = M = 36`，`w = 1848`，`h = 132`，竖直中心 `cy = 102`。
- 正文顶 `bodyTop = M + HEAD_H + GAP = 190`；底 `bottom = H - M = 1044`（左列可用高 854）。
- 右列从 `bodyTop` 起占满到 `bottom`，**与公告无关**（公告只在左列，不再挤右列）。
- 左列：公告条（有公告时）从 `bodyTop` 起、宽 `LEFT_W`；作业卡紧接其下（`+GAP`）吃到 `bottom`。

### 3.2 背景与标题带

背景（3 条命令，铺满整幅）：

1. 竖直渐变 `#f8faff → #e5eefa`（`bgTop → bgBottom`）。
2. 左上蓝光晕：`#5c99f0` α0.3 径向，圆心 `(W/2-700, H/2-470) = (260, 70)`，半径 `420`，
   外圈透明。
3. 右下青光晕：`#59c7d9` α0.22 径向，圆心 `(W/2+720, H/2+380) = (1680, 920)`，半径 `380`，
   外圈透明。

标题带（`headerBand`）：

- 投影：`pushShadow(x36,y36,w1848,h132, r34, blur 24, dy 10, color #2b5aa5, maxAlpha 0.35)`。
- 本体：`r34` 对角渐变（左上→右下）`#669ce0 → #2b5aa5`（`bandTop → bandBottom`）。
- 带内装饰（**全部裁进圆角矩形** `clip = {36,36,1848,132,r34}`）：
  - 白色模糊实心圆：圆心 `(1600, cy-96 = 6)`，`r = 180`，径向 `rgba(255,255,255,0.13) → 透明`，
    渐变半径 `r*1.18 = 212.4`。
  - 白色细圆环：圆心 `(1390, cy+74 = 176)`，`r = 105`，描边 `rgba(255,255,255,0.12)`，宽 2。
- 带内白描边：整块 `r34`、`rgba(255,255,255,0.16)`、宽 1.4。
- 内容：
  - 班徽框（`showBadge` 为真时）：`box = 62 + 14*2 = 90`，圆角 26，白 18% 底、白 28% 描边宽 1.4；
    图片画在 `(tx+7, cy-45+7, 76, 76)`，**按墨迹范围等比居中**；之后 `tx += 90 + 22`。
  - 中文标题 `每日作业`：46px 粗体白，左对齐 `tx`。
  - 英文副标 `DAILY HOMEWORK`：13px 粗体 `rgba(255,255,255,0.62)`，tracking 3.4，
    x = `tx + inkLeftEdge("每日作业",46,true) - inkLeftEdge("DAILY HOMEWORK",13,true,3.4)`
    （按实边对齐，副标与标题视觉左缘齐）。
  - 两行用 `centerTwoLines(cy, gap 8, 标题, 副标)` 整体按墨迹垂直居中。
  - 右侧（右对齐到 `bx+bw-HEAD_PAD = 1856`）：日期 `YYYY.M.D`（`dotDate`，月日不补零）54px 粗体白；
    星期（`vm.weekday`，如「周三」）34px 白 82%；两行同样按墨迹整体居中，间距 8。
- 班徽是否绘制 = `show_badge && badge_available()`（通用版内置素材无墨迹时 `badgeHasInk()` 为 false）。

### 3.3 左列 · 公告条

**只在 `vm.announcements.length > 0` 时绘制**（`vm.announcements` 已由后端过滤掉 `done`；
layout 自己不再过滤）；宽度 = `LEFT_W`（与作业卡同宽），排在左列顶部。

常量与画法：

| 项 | 值 |
|---|---|
| 左右内边距 `ANN_PAD_H` | 26 |
| 上下内边距 `ANN_PAD_V` | 20 |
| 喇叭圆 `ANN_ICON` | 62（白 60% 圆 + `megaphone` E789，28px，色 `#732614`，居中） |
| 图标与圆点的间隔 `ANN_ICON_GAP` | 20 |
| 条目圆点直径 `ANN_DOT` | 13（半径 6.5，色 `annText #732614` α0.6） |
| 圆点与正文间隔 `ANN_DOT_GAP` | 14 |
| 正文左缘 `ANN_TEXT_LEFT` | `26*2 + 62 + 20 + 13 + 14 = 161` |
| 行高 `annLineH(size)` | `round(size * 1.27)` |
| 条目间距 `annItemGap(size)` | `round(size * 0.5)`（**大于条内换行行距**，读起来才分得清条目） |
| 最小高度 | `ANN_ICON + ANN_PAD_V*2 = 102` |

- 投影 `pushShadow(r28, blur 18, dy 8, #f5c487, 0.45)`；本体 `r28` 竖直渐变
  `#ffe0a9 → #f5c487`，描边 `rgba(255,255,255,0.55)` 宽 1.6。
- 高度 `h = max(102, ANN_PAD_V*2 + 总行高 + 条目间距合计)`；
  第一条文本的起始中心 `cy0 = y + (h - contentH)/2 + lineH/2`，每条（`first`）之前若不在最前再 `+= gap`。
- 正文按 `wrapText(text, size, true, LEFT_W - 161)` 贪心换行（**粗体**、色 `annText`）；
  换行后的行**不缩进**（仍从 `textX` 开始），但视觉上靠条目圆点区分。
- 字号 `annSize` 由 §3.7 的算法从 `ANN_SIZES = [52, 46, 40, 36, 32, 28]` 里挑。

### 3.4 左列 · 作业卡

面板头（所有卡片共用 `panel()`）：

- `pushShadow(x,y,w,h, r30, blur 22, dy 8, #213861, 0.1)`；
- 白面 `r30`，描边 `#e0e7f3` 宽 1.6；
- 图标胶囊：52×52、圆角 16、`blueSoft #e8f0fb` 底，位于 `(x+PAD, cy-26)`，`cy = y + 48`；
  图标 26px `#2e5fa3` 居中（作业 = `book` E736，课表 = `calendar` E787，演讲 = `mic` E720）；
- 标题 38px 粗体 `#2e5fa3`，x = `x + PAD + 52 + 14`；
- 分隔线：`y + TITLE_H - 14 = y + 82`，高 1.5，`x+PAD … x+w-PAD`，色 `cardStroke`。

卡标题（逐字）：

- 全部模式：`家庭作业`；
- 仅要交模式：`今天要交的作业 · 来自 {vm.homework_date.slice(5)}`（例如 `今天要交的作业 · 来自 10-01`）。
- 空状态（在卡片正文区水平+垂直居中，34px 灰字 + 图标，图标宽 `34*1.15`、间距 16）：
  - 无组：仅要交 → 图标 `check` E73E + `今天没有要交的作业`；否则 `check` + `今天还没有作业`。

几何：`ax = x + PAD`，`ay = y + TITLE_H`，`aw = w - 2*PAD`，`ah = h - TITLE_H - PAD`，
`textX = ax + INDENT = ax + 80`，`right = ax + aw`，行高 `lineH = size * 1.4`。

**编号与分组规则（必须保留）**：

1. `homeworkGroups(vm)`：把 `vm.homework`（可含同一学科多条）按 `subject` 收成 `HwSub{label, entries[]}`。
2. 组顺序 = `vm.homework_suggestions` 里出现的学科**按建议顺序**在前，其余学科按 `vm.homework` 的
   **首次出现顺序**在后（去重）。
3. **英语A / 英语B 合占一个编号**：两者被收进同一个组（`eng`），**内部相对顺序跟随上面的序列**
   （不再强制 A 在前）；组内每个学科各占**一行**（所以合占一个编号、画两行）。
   只有一边有内容时该组只有一行。
4. 组内多条作业（`sub.entries.length > 1`）在**同一行内**用「序号 + 半角空格」分隔，条目之间用
   全角空格 `　`（`sep`）分隔；自动换行由 `flow()` 负责，换行后的续行仍从 `contentX` 起（不再重复学科名）。
   **只有一条时不显示序号。**
5. 每个组的编号徽章：56×56、圆角 18、竖直渐变 `#5c94de → #2e5fa3`（`badgeTop → blue`），
   外加 `pushShadow(圆角 18, blur 8, dy 3, #2e5fa3, 0.28)`；数字 32px 粗体白居中，从 1 递增。
6. 「要交」标记（`Piece{kind:"check"}`）画法（**不是字体里的 ✓**，用图元更可控也更粗）：
   - 尺寸 `d = round(size * 1.05)`；
   - 圆：白底、橙色 `#e07b00` 描边、圆角 `d/2`（即正圆），描边宽 `max(3, round(d*0.11))`；
   - 勾：`path`（圆头圆角），描边宽 `max(3, round(d*0.15))`，橙色，点为
     `(x+0.30d, cy+0.02d) → (x+0.45d, cy+0.18d) → (x+0.73d, cy-0.19d)`；
   - **整体不拆行**：放不下时先换行再画；画完 `x += d + 6`；
   - 标记紧跟在本条作业文本之后（前面补一个半角空格）；
   - 「仅要交」模式下**不画标记**（`hideSubmit = vm.submit_only`，此时所有条目都是要交）。
7. 标签列宽 `hwLabelW(groups, size) = min(当天实际最长学科名+「：」的宽度, measure("字字字字字："))`
   ——所有学科内容的**首个字符对齐同一条竖线**；超过上限的学科名用 `fitLabel` 截断成
   `前若干字…：`（循环砍尾直到 `名字…：` 放得下，至少保留 1 个字）。
8. 组间距 `lineH * 0.35`；一行的绘制顺序：徽章 → 学科标签（`fitLabel`）→ `flow` 出的内容片段。

### 3.5 右列 · 课表卡

- 面板头图标 `calendar` + 标题 `今日课表`。
- 正文 `ax = x+PAD`，`ay = y+TITLE_H`，`aw = w-2PAD`，`ah = h-TITLE_H-PAD`。
- 取 `vm.slots` 里 `name` 非空的条目、`slice(0,9)`；空 → 空状态（图标 `sun` E706 + `今日无课`）。
- **两栏**：`perCol = ceil(n/2)`，`col = floor(i/perCol)`，`row = i % perCol`；
  满 9 节时正好 **1-5 左栏 / 6-9 右栏**；列宽 `colW = (aw - 24)/2`，列间距 24。
- 行距 `pitch = clamp((ah - 4)/perCol, 20, 56)`（下限 `PITCH_FLOOR = 20`，上限 `SCHED_ROW_MAX = 56`）；
  块高 `blockH = perCol*pitch`，整体在 `ah` 里垂直居中：`topY = ay + max(0, (ah-blockH)/2)`，
  行中心 `cy = topY + row*pitch + pitch/2`。
- 字号 `rowSize = clamp(round(pitch * 0.78), 24, 42)`（**字号 = 行距 × 0.78**，下限 24、上限 42）。
- 序号小胶囊：`chip = clamp(pitch - 6, 16, 30)`，圆角 `chip*0.3`，字号 `round(chip*0.63)`；
  - 正常节次：底 `blueSoft`，字 `--gray`（`#8a93a3`）、正常字重；
  - **调课**（`slot.changed`）：底 `red #c0272d`，字白色粗体。
- 课程名：`x = cx + chip + 12`，`rowSize` 粗体；正常 `#1b212f`，调课 `red`。

### 3.6 右列 · 演讲提醒卡

- 面板头图标 `mic` + 标题 `演讲提醒`；正文同几何。
- 无条目（`students.trim()` 全空）时画红色提示块：
  - `ah < 24` → 整块不画（宁可没有，也不越界）；
  - 块高 `bh = clamp(ah, 0, 100)`；y 居中；圆角 20，底 `redSoft #fdebec`，描边 `#c0272d` α0.25 宽 1.6；
  - 字号 `size = clamp(round(bh*0.38), 18, 42)`，图标 `iconSize = size*0.9`、宽 `size*0.9*1.15`，
    间距 14；**再按宽度收字号**：`while (size > 12 && iconW+gap+measure("演讲提醒未填写", size, true) > aw) size--;`
  - 内容：图标 `warning` E7BA（红色）+ 粗体红字 `演讲提醒未填写`，整组在块里水平居中
    （左侧至少留 4px）。
  - ⚠ **与 AGENTS.md 的差异**：AGENTS.md 写「最高 100、最低 56」，代码里只有
    `max(0, min(100, ah))`，**没有 56 的下限**（`ah < 24` 直接不画）。以代码为准。
- 有条目时：
  - 学号正文左缘 `textX = ax + SPEECH_TAG_W = ax + 112`（**学号首位钉在同一条缩进线上**）；
  - 学科是**小胶囊**：底 `blueSoft`，圆角 10，宽 `min(SPEECH_TAG_W - 8, measure(学科, tagSize, true) + 24)`
    （即随学科长短收窄，上限 104），高 `round(cand * 1.05)`，居中放学科名（`--blue` 粗体）；
  - 每个学科的**第一行**与胶囊同中心，续行（自动换行的学号）从 `ax` 起（不再缩进到 112）；
  - 行距 `lineH = round(cand*1.25)`，条目间距 `rowGap = 14`；
  - **顶部对齐**：`ty` 从 `ay` 起（首行紧贴标题分隔线，不在卡片里垂直居中）。
  - 字号候选档：`[42, 38, 34, 30, 28, 26]`，从大到小取**第一个能让所有条目总高 ≤ ah** 的档；
    每档同时算 `tagSize = max(24, round(cand*0.8))`（学科胶囊跟着行字号一起变大，下限 24）。
  - 截断：某个学科放不下（`ty + lineH > limit`，`limit = ay + ah`）就**停止后续条目**；
    同一学科内部续行若 `ly > limit - 8` 则跳过该行。

### 3.7 字号基准与自适应算法（核心，必须逐条复刻）

字号基准：**公告 52（最大，主视觉）、面板标题 38、作业自适应 28~46、课表行上限 42 下限 24
（字号 = 行距×0.78）、演讲 42**（备选 42/38/34/30/28/26）。

```
ANN_SIZES   = [52, 46, 40, 36, 32, 28]      // 公告候选（从大到小）
PITCH_FLOOR = 20                            // 课表行距下限
SCHED_ROW_MAX = 56                          // 课表行距上限
HW_MAX = 46, HW_MIN = 28                    // 作业正文自适应区间
```

**`fitSize(need, avail, max, min)`**：整数二分，找最大的 `s ∈ [min,max]` 使 `need(s) <= avail`；
全程用真实测量（`measure`/`wrapText`），没有估算系数。作业字号 = `fitSize(s => hwNeed(...), ah, 46, 28)`。

**`hwNeed(groups, x, w, size, submitOnly)`**（作业卡需要的高度，含组间距；也是公告反推的输入）：

```
lineH   = size * 1.4
textX   = x + PAD + INDENT            // 缩进 80
contentX= textX + hwLabelW(groups,size)
right   = x + PAD + (w - PAD*2)
total   = Σ_组 ( Σ_学科 flow(0, lineH, contentX, right, subPieces(...), maxLine=∞) * lineH )
        + Σ_组 lineH * 0.35
```

**`annFor(hwSize)`**（公告字号与作业字号相称的反推，线性映射到 `[HW_MIN, ANN_SIZES[0]]`）：

```
t = HW_MIN + ((hwSize - HW_MIN) / (HW_MAX - HW_MIN)) * (ANN_SIZES[0] - HW_MIN)
  = 28 + (hwSize - 28) / 18 * 24          // hwSize=28 → 28；hwSize=46 → 52
取第一个 s ∈ ANN_SIZES 使 s <= t
```

**公告字号分配**（`layout()` 里，只有真有公告时才跑）：

```
annSize = 28                                  // 兜底 = 候选里最小的那个
for cand of ANN_SIZES:                        // 52 → 28
    avail = bottom - (bodyTop + annHeight(vm, LEFT_W, cand) + GAP) - TITLE_H - PAD
    hwSize = groups.length === 0 ? HW_MAX
             : fitSize(s => hwNeed(groups, M, LEFT_W, s, submit_only), avail, 46, 28)
    if (cand <= annFor(hwSize)) { annSize = cand; break }
```

语义：**先用当前公告字号算出作业能取到多大，再要求公告字号不超过与那个作业字号相称的大小**；
所以作业多 → 作业字号小 → 公告字号也小（两块一起缩），作业少时公告恒为 52，
作业实在太多时两者都停在最小字号，再由作业卡截断。`hwNeed` 与 `fitSize` **共用同一个高度函数**，
不要各算一套（否则两块字号会脱节）。公告宽度固定用 `LEFT_W`。

**`rightColumn(availH, slotCount, speechRows)`**（右列高度统一分配；调用时 `availH = (bottom-bodyTop) - GAP`）：

```
perCol     = ceil(slotCount / 2)
base       = TITLE_H + PAD                     // 122
schedNeed  = base + perCol * SCHED_ROW_MAX     // 课表理想高度（行距取上限）
speechNeed = base + (speechRows === 0 ? 60 : speechRows * 70)
minSched   = base + perCol * PITCH_FLOOR
minSpeech  = base + 60
sum        = schedNeed + speechNeed
schedH     = sum <= availH ? schedNeed : round(schedNeed * availH / sum)      // 不够就按比例压
schedH     = max(minSched, min(schedH, max(minSched, availH - minSpeech)))    // 两边都保住下限
pitch      = min(SCHED_ROW_MAX, (schedH - base - 4) / perCol)
speechH    = availH - schedH                  // 余量全给演讲
```

- 调用方只用 `schedH / speechH`（`pitch` 返回值当前未被使用——`schedulePanel` 用自己的几何
  重新算 pitch：`clamp((ah-4)/perCol, 20, 56)`，两者等价，重写时可只保留一套）。
- 排布：课表 `(RIGHT_X, bodyTop, RIGHT_W, schedH)`；演讲 `(RIGHT_X, bodyTop + schedH + GAP, RIGHT_W, speechH)`。
- `slotCount = min(当天的课节数 || 1, 9)`；`speechRows = 学号非空的演讲条目数`。

### 3.8 截断规则汇总（「不叠字 / 不溢出」）

| 位置 | 规则 |
|---|---|
| 作业卡整体 | 字号二分到 28 仍放不下时：按组遍历，`if (ty + lineH > limit) break;`（**丢掉后面的学科**）；卡内不溢出 |
| 作业组内 | `flow(..., maxLine = floor((limit - ty)/lineH) - 1, emit)`：超出允许行数的片段直接**不绘制**（整行丢） |
| 作业学科名 | `fitLabel`：超过 `measure("字字字字字：")` 就砍尾加 `…`，保证标签列宽恒定 |
| 公告 | 换行贪心；`annSize` 逐档下调；没有「截断」，靠缩小字号与整体高度（公告条不设高度上限，但会让作业字号被动变小） |
| 课表 | 行距 `pitch` 自适应（20~56），字号随行距（下限 24）；最多画 9 节（`slice(0,9)`），`perCol` 保证都在卡内 |
| 演讲 | 字号降到 26 仍放不下 → 停止后续学科；学科内续行超出 `limit - 8` 跳过 |
| 演讲红色提示块 | `bh` 随 `ah` 收缩（上限 100）；`ah < 24` 整块不画；宽度不足时逐像素缩字号 |
| 复核矩阵 | 改版式后按 6/7/8/10/12 条公告 × 9/18/27 条作业各渲染一遍（含老师真实数据那天），确认不叠字、不越界 |

---

## 4. 「必须保留」vs「可自由重设计」

### 4.1 必须保留（信息结构 / 行为 / 数字约束）

1. **放映画布固定 1920×1080**，导出图也是这个像素尺寸；屏幕显示按 `min(cw/1920, ch/1080)` 等比 + 黑边 letterbox。
2. **关键词号体系**：公告 52（最大）、面板标题 38、作业 28~46 自适应、课表行距 ≤56 且字号 = 行距×0.78、
   演讲候选 [42,38,34,30,28,26]；以及「作业与公告一起缩」的反推关系（`annFor`/`hwNeed`/`fitSize` 共用一个高度函数）。
3. **作业编号与分组**：英语A/英语B 合占一个编号（各自一行，相对顺序跟随编辑页/建议顺序）；
   同一学科多条在行内以「序号 + 空格」分隔、条目间用全角空格；只有一条时不显示序号；
   组顺序 = 作业建议顺序 → 数据首次出现顺序。
4. **「要交」橙色圆勾标记的画法与位置语义**：紧跟条目文本、整体不拆行、仅要交模式下不画。
5. **标签列对齐**：作业学科标签列宽 = 当天最长学科名（含「：」），上限「5 个字 + 冒号」，
   超长截断为「前若干字…：」，所有内容首字对齐同一条竖线；演讲学号首位钉在 `SPEECH_TAG_W = 112`
   这条缩进线上，学科是小胶囊。
6. **课表两栏编号**（1-5 / 6-9，按 `ceil(n/2)` 均分）+ 调课红色（红底白字胶囊 + 红字课名）。
7. **公告条只在有公告时绘制**，且宽度与作业卡一致、排在左列顶部；右列（课表+演讲）从 `bodyTop`
   起占满到底、与公告无关。
8. **卡片版式骨架**：外边距 36 / 区块间隔 22 / 标题带高 132 / 面板头高 96 / 卡片内边距 26 /
   左列宽 1464（右列剩余），标题带在上、左列公告+作业、右列课表在上演讲在下。
9. **截断与不溢出规则**（§3.8 全表）：不叠字、不越卡片、不越画布。
10. **线条与文本的墨迹对齐语义**：`text.y` = 墨迹垂直中心（+0.07em 补偿）、两行按实测墨迹整体居中、
    副标按实边对齐、班徽按素材墨迹范围等比居中。
11. **编辑页页面结构**：工具栏顺序（汉堡 · 品牌 · 上一天 · 日期栏 · 下一天 · 全部|仅要交 · 今天/放映/出图/退出）、
    公告卡跨两列、作业在左、右列课表在上演讲在下、卡片标题（图标胶囊 + 名称 + 计数徽标）与 hint 文案。
12. **逐字中文文案**：按钮、标题、提示语、占位符、toast、菜单项（见 §1、§2 各表）。
13. **全部快捷键语义**（§2 全表），包括 `Alt+数字`/`Alt+↑↓` 在输入框内仍生效、
    `Ctrl+K`/`Ctrl+,`/侧边栏导航的「再点一次回编辑页」toggle 行为。
14. **不出现页面滚动条**：卡片内部滚动（`.card-body`/`.hw-list` 吃剩余高度）+ 编辑页紧凑算法（§6）。
15. **公告区固定 4 行、行高 44px、切断落在行边界、总是滚到最底**（冷启动与热启动）。
16. **ghost 空白行语义**：与已有条目同格式、同列宽；`showGhost = 未用学科 > 0 && (无条目 || 有内容)`；
    公告卡永远保留一行；没选学科直接写内容就用第一个未用学科，成功后学科清零。
17. **Select 的行为契约**：Teleport 到 body + fixed 定位、空间不足向上弹、宽度 `max(触发器,120)`、
    高度 `选项数*28+8`（上限视口高 − 24）、**容器一滚动就关闭**、`editable` 模式优先提交自打文本、
    `↓/↑` 翻选项后回车才选中、`Esc` 关闭且不冒泡。
18. **就地编辑 300ms 防抖**与整份 `set_day` / `set_announcements` 落库（撤销合并 token 语义）。
19. **tooltip 400ms 延迟 + 贴光标 + 输入框聚焦时不弹 + 按下/滚动/移开即消失**，文案来自 `data-tip`。
20. **放映右键菜单**：条目与顺序、6 秒无操作收起、菜单整块夹在屏幕内、子菜单右侧独立一块并会翻转、
    不操作时隐藏鼠标指针（菜单打开才显示）、菜单开着时 Esc 只关菜单。
21. **出图语义分离**（自动 = 今天·全部，手动 = 当前屏幕所见；文件名一律页面日；两张图并存不互删）
    与 `Ctrl+Q` 退出前「落草稿 → 重出今天图 → 退出」的顺序（§5）。
22. **图片写剪贴板 / 文字版作业写剪贴板**两条能力与失败回退路径（§5、§1.4）。
23. **侧边栏缩略图的语义**：预渲染 1920×1080 图；点一下 = 出图；拖出去 = 交给外部程序
    （必须提供真实文件，不能只给 blob URL）。
24. **分辨率无关的编辑页**：`ui_scale ∈ [50%,150%]` 只缩放编辑/设置/课表，放映页恒为 1；
    挂在 body 上的浮层要按 zoom 归一化坐标。
25. **深色 / 浅色 / 跟随系统**三态，且放映画面永远固定浅色。
26. **学科清单 18 项**与图标码点表（§7）——图标与学科名是跨版本一致性的一部分。

### 4.2 可自由重设计（纯视觉）

- 具体色值（`#669ce0`、`--blue #2e5fa3`、`amberTop`、`orange #e07b00`…）与配色关系。
- 圆角、描边宽度、投影参数（`pushShadow` 的层数/模糊/偏移/透明度）与投影算法本身
  （Qt 侧完全可以用真实高斯模糊）。
- 渐变方向与光晕（背景两团径向光、标题带对角渐变、带内白圆/白环装饰）。
- 图标的**具体字形**（Segoe Fluent Icons 是 Windows 字族；Qt 侧可换成自带矢量图标），
  **只需保留「图标 + 名称 + 计数徽标」的信息结构与图标语义**。
- 字号/行距的**绝对值**可以重调，但必须保持相互比例与「一起缩」的约束（§4.1 第 2 条的下限/上限
  属于行为约束，不是纯视觉）。
- 卡片阴影、hover 高亮、过渡动画时长（0.15s/0.18s/0.2s）等观感细节。
- 编辑页的网格比例（`1.45fr : 1fr`）、侧边栏宽度 190px、工具条高度 52px 等具体尺寸
  （但结构顺序与「切页不跳高」的意图要保留）。

---

## 5. 出图与图片

### 5.1 自动出图 vs 手动出图

| | 自动 | 手动 |
|---|---|---|
| 触发 | 后端后台线程发 `auto-image-request` 事件（条件：距上次 > `image_interval_secs` 且 `image_dirty` 且非放映中）；`Ctrl+Q` 完全退出前也跑一次 | `Ctrl+D`、编辑页工具栏「出图」按钮、放映右键 `生成作业图片`、侧边栏缩略图单击 |
| 取的数据 | `get_view_model(date = store.today, submitOnly = false)` —— 固定**今天 · 全部** | 当前屏幕所见：直接用内存里的 `vm`（页面日 + 当前模式，含「仅要交」过滤）；`vm` 为空时按 `view.date/submitOnly` 现取 |
| 文件名日期 | `store.today`（后端定义的「今天」，休息日会落到上一个上学日） | `vm.date` = **页面日**（作业所属的那一天），**不是**画面日 |
| 提示 | 无 toast，静默 | 成功 `已生成：{path}`，失败 `生成图片失败：{e}` |
| 并发 | `generateImage` 内部 `running` 标志：同一时刻只跑一份，重入直接返回 | 同左 |
| 归档 | 两张图并存、互不删除；保留集 = 真实今天 + `effective_today`，其余图按日期/mtime 搬进「家庭作业图片」存档 | 同左 |

### 5.2 渲染与导出

- `renderPng(cmds)`：新建 `1920×1080` 的 canvas（**不做 DPR 放大**）→ `renderCmds` 全量重放 →
  `canvas.toBlob(..., "image/png")` → `Uint8Array`。
  所以**导出图 = 1920×1080 设备像素的 PNG，未写 DPI/pHYs 元数据**（由 Canvas 编码器决定，
  按 1 CSS px = 1 图像 px 使用）。屏幕放映才用 `devicePixelRatio` 放大画布。
- 屏幕与导出**共用同一份 `DrawCmd` 与同一个渲染函数**，严格 WYSIWYG：不许为出图另写一套版式。
- 班徽在渲染前必须 `await loadBadge()`（已缓存的 promise 直接复用；上传新班徽后 `reloadBadge(true)`）。

### 5.3 文件名与落盘

- 文件名规则：`家庭作业_{YYYY-MM-DD}.png`（前缀常量 `IMAGE_PREFIX = "家庭作业_"`），
  写到 **exe 同级目录**（`store::exe_dir()`，由后端拼路径，前端只传日期）。
- 日期非法直接拒绝（前端不参与路径拼接）。
- 自动出的「今天图」与手动出的「今天图」同名会被覆盖；不同日期的图**不互相删除**。
- 清理/归档：`relocate_images`（启动时 + 后台周期）把非保留图搬进 `数据目录/家庭作业图片/`；
  设置页「清理 exe 同级图片」（`clear_images`）无条件搬走所有非保留图，返回搬走的张数。
- `Ctrl+Q` 退出的顺序（行为契约）：`toast 正在生成今天的作业图片…` → `flushDraft()` →
  `generateImage(false)` → `quit_app`（后端落盘后 `app.exit(0)`）。

### 5.4 图片写剪贴板

- `copyPngToClipboard(bytes)`：`navigator.clipboard.write([new ClipboardItem({"image/png": blob})])`；
  环境不支持或无权限时返回 false。
- 放映右键「复制作业图片」：先 `renderCurrentPng()`（1920×1080），写剪贴板成功 →
  `作业图片已复制到剪贴板`；失败 → 回退成 `export_image` 落盘并提示路径 `复制失败，已生成图片：{path}`。
- 文字版：`copyTextToClipboard(slidePlainText(vm))`，成功/失败两条 toast。
- `slidePlainText` 的纯文本结构（与放映信息一致，必须保留）：标题行 `每日作业 {Y.M.D} {星期}` →
  空行 → `【公告】` + `· ` 前缀逐条 → 空行 → `【家庭作业】`（仅要交时 `【要交的作业】`）
  + `N. 学科：内容（要交）`（同学科多条用 `；` 连接，英语A/B 合成 `英语` 占一个编号）→ 空行 →
  `【今日课表】` + 两栏拉平的一行（`1 语文　6 数学`）→ 空行 → `【演讲提醒】`（`学科：学号`；
  `speech_warn` 时整段不输出）→ 去掉尾部空行 + 结尾一个 `\n`。

---

## 6. 编辑页紧凑算法（src/fit.ts）

容器（**作业卡 `.hw-list`、演讲卡 `.card-body`、课表卡 `.card-body`** 三处；公告卡不用）挂
`useFit(containerRef)`：

```
ROW = 34
measure():
  node.classList.remove("compact", "compact2")     // 先摘掉类量基准
  over = node.scrollHeight - node.clientHeight     // 读 scrollHeight 强制布局，量「不紧凑」时的真实溢出
  if (over > 0    && over <= ROW)     → .compact
  else if (over > ROW && over <= ROW*2) → .compact2
  （over <= 0 或 over > 68 → 不挂类，保留滚动条）
```

- 触发时机：`ResizeObserver(容器自身)`（窗口缩放）+ `MutationObserver(childList, subtree)`（增删条目）；
  用 `requestAnimationFrame` 合并（同一帧只量一次）。因为观察的是容器外框与子节点（不是 class），
  且每次先摘类再按纯函数决定，所以不会抖动、不会自激。
- 阈值语义：内容只高出一点点（≤1 行）挂 `.compact`，高出 1~2 行挂 `.compact2`，
  **溢出超过约两行（>68px）才保留滚动条**。

收缩项（style.css，逐条）：

| 选择器 | `.compact` | `.compact2` |
|---|---|---|
| `.card-body` / `.hw-list` 网格列间距 | `gap: 0 8px` | `gap: 0 8px` |
| `.edit-row` | `padding: 0` | `padding: 0; gap: 8px` |
| `.edit-row .sub`（行首标签列） | `font-size: 13px` | `font-size: 12px` |
| `input[type="text"]` | `padding: 3px 8px; font-size: 12px` | `padding: 2px 7px; font-size: 12px; line-height: 1.1` |
| `.warn-banner`（演讲红横幅） | `padding: 3px 8px; font-size: 12px; margin-bottom: 4px` | `padding: 2px 8px; font-size: 11px; margin-bottom: 2px` |

（注意：紧凑只改**行距/内边距/字号**，不改卡片高度、不隐藏内容——内容真正的溢出仍走卡内滚动。）

---

## 7. 设计系统

### 7.1 学科清单（`draft.ts` 的 `ALL_SUBJECTS`，18 项，顺序即下拉顺序）

```
语文 数学 英语A 英语B 物理 化学 生物 历史 地理 政治 体育 美术 音乐 信息 心理 劳技 自习 班会
```

- 「英语分 A/B 班」= `settings.english_split`，**只影响作业/演讲的学科建议**（是否把「英语」拆成 A/B）；
  已有数据里的 英语A/英语B 照旧显示。
- 作业/演讲的学科框可**打字输入列表外的新学科**（Select `editable`）。

### 7.2 图标码点表（`icons.ts` → Segoe Fluent Icons / Segoe MDL2 Assets）

| 名称 | 码点 | 名称 | 码点 |
|---|---|---|---|
| megaphone | `\uE789` | settings | `\uE713` |
| book | `\uE736` | play | `\uE768` |
| calendar | `\uE787` | save | `\uE74E` |
| mic | `\uE720` | trash | `\uE74D` |
| sun | `\uE706` | folder | `\uE8B7` |
| moon | `\uE708` | download | `\uE896` |
| starFill | `\uE735` | copy | `\uE8C8` |
| refresh | `\uE72C` | undo | `\uE7A6` |
| warning | `\uE7BA` | redo | `\uE7A7` |
| checkmark | `\uE73E` | chevronLeft | `\uE76B` |
| edit | `\uE70F` | chevronRight | `\uE76C` |
| chevronDown | `\uE70D` | starOutline | `\uE734` |
| chevronUp | `\uE70E` | hamburger | `\uE700` |
| plus | `\uE710` | close | `\uE711` |
| mapPin | `\uE707` | display | `\uE7F4` |
| lock | `\uE72E` | | |

字族串：`"Segoe Fluent Icons", "Segoe MDL2 Assets"`（Win11 / Win10 同一套常用码点）。
`layout.ts` 内部另有一份**放映专用子集**：`mega E789`、`book E736`、`calendar E787`、`mic E720`、
`sun E706`、`check E73E`、`warning E7BA`。

图标与语义的对应（重写时可换字形，语义保留）：

| 位置 | 图标 |
|---|---|
| 侧边栏 编辑 / 课表总览 / 设置 | edit / calendar / settings |
| 工具栏 汉堡 / 上一天 / 下一天 / 今天 / 放映 / 出图 / 退出 | hamburger / chevronLeft / chevronRight / mapPin / play / download / close |
| 卡片 公告 / 作业 / 课表 / 演讲 | megaphone / book / calendar / mic |
| 放映 空课表 / 空作业 / 演讲未填写 | sun / check / warning |
| 行内 删除 / 加一条 / 完成 | close / plus / checkmark |
| 设置页 温度/外观相关 | sun / moon |

### 7.3 主题 token（style.css）

浅色（`:root`）与深色（`:root[data-theme="dark"]`）：

| 变量 | 浅色 | 深色 |
|---|---|---|
| `--blue` | `#2e5fa3` | `#6f9fe0` |
| `--blue-soft` | `#e8f0fb` | `#23303f` |
| `--red` | `#c0272d` | `#e5737a` |
| `--orange` | `#e07b00` | `#e5a34d` |
| `--text` | `#20242b` | `#e6ebf2` |
| `--gray` | `#8a93a3` | `#9aa4b2` |
| `--bg` | `#eef2f9` | `#161a20` |
| `--card` | `#ffffff` | `#1e232b` |
| `--border` | `#d7deeb` | `#333c48` |
| `--hover` | `#f2f7fe` | `#262d37` |
| `--ring` | `0 0 0 3px rgba(46,95,163,0.14)` | `0 0 0 3px rgba(111,159,224,0.28)` |
| `--sidebar` | `#e7edf7` | `#1a1f26` |
| `--sidebar-top` | `#eff3fb` | `#1d232b` |
| `--head` | `#ffffff` | `#1e232b` |
| `--danger-soft` | `#fdecec` | `#3a2427` |
| `color-scheme` | `light` | `dark` |

另有若干**写死的值**（重写时可归并进 token）：

- `button.primary:hover` 背景/描边 `#254e86`；`input:hover` 描边 `#b9c6dd`；
  占位符 `#aab3c2`；Toggle 关闭底色 `#dfe4ee`；`.warn-banner` 描边 `#f3c1c3`；
  toast 底 `rgba(32,36,43,0.92)` 白字；Select 浮层阴影 `0 10px 28px rgba(20,26,38,0.16)`；
  缩略图底色 `#f8faff`；放映页底色纯黑；对勾（checkbox `::after`）白色 `4×9`、右/下 2px 边框旋转 45°。
- 滚动条：`scrollbar-color: var(--border) transparent`；`::-webkit-scrollbar` 12px；
  thumb `var(--border)` 圆角 6px + `border:3px solid transparent` + `background-clip:content-box`；
  hover 用 `--gray`。
- 全局字族：`"Microsoft YaHei", "SimHei", sans-serif`；`html/body/#app` 高 100%、底色 `--bg`。
- 外观切换实现：`document.documentElement.dataset.theme = "dark"|"light"` +
  `style.colorScheme = 同值`；`system` 时监听 `matchMedia("(prefers-color-scheme: dark)")` 变化。

### 7.4 ui_scale 与 zoom 归一化

- `settings.ui_scale ∈ [0.5, 1.5]`（设置页显示为 50%~150%，step 5%，拖动即时预览、松手落盘）。
- `App.vue` 写 `document.documentElement.style.zoom = String(clamp(ui_scale, 0.5, 1.5))`；
  **放映模式写空串**（`zoom = 1`）。效果与系统显示缩放**相乘叠加**。
- 归一化：挂在 `body` 上的浮层（Select 下拉、tooltip、放映右键菜单）不受 `<html>` 的 zoom 影响，
  而 `getBoundingClientRect()` / `clientX/Y` 返回的是**缩放后的视口坐标**，所以定位前统统
  `÷ uiZoom()`（`uiZoom()` 从 `document.documentElement.style.zoom` 解析，非法值回落到 1）：
  视口宽高、鼠标坐标、触发器 rect、菜单 rect、`innerWidth/innerHeight` 都要除。
  设置页「默认滚到今天」的 `scrollTop` 计算同样要除 zoom。
- 注意：**放映页不受 ui_scale 影响**（canvas 版式本来就与 UI 缩放无关）。

---

## 8. 交互细节清单

### 8.1 时间常量一览

| 常量 | 值 | 位置 |
|---|---|---|
| toast 显示时长 | 2600ms | `api.ts showToast` |
| 就地编辑（公告）落库防抖 | 300ms | `AnnouncementCard.scheduleSave` |
| 草稿整体落库防抖 | 300ms | `draft.ts saveDraft` |
| 撤销合并窗口 | 2.5s（后端，按字段 token 合并） | `AGENTS.md` + `draft.ts diffToken` |
| tooltip 浮出延迟 | 400ms | `tooltip.ts DELAY_MS`（偏移 x+14 / y+20，边距 6） |
| 放映右键菜单无操作自动收起 | **6000ms** | `PresentView MENU_IDLE_MS`（鼠标 move 重新计时） |
| 放映子菜单移出后收起 | 180ms | `PresentView scheduleScreenClose` |
| 自动定位轮询 | 30 000ms | `App.vue tickRelocate` |
| 自动保存（后端） | 1s 防抖 | Rust 后台线程 |

⚠ **与 AGENTS.md 的差异**：AGENTS.md 行 345 写「无操作 `MENU_IDLE_MS`(4s) 自动收起」，
代码是 `6000`（同文件行 60/377 也写 6 秒）。**以代码为准：6 秒。**

### 8.2 Select（`components/ui/Select.vue`）

- 浮层 **`<Teleport to="body">` + `position: fixed`**：留在卡片里会被祖先的 `overflow` 裁掉
  （ghost 行、设置页天数列表都在滚动容器内）。
- 定位（全部坐标先 ÷ zoom）：`gap = 4`；高度 `min(选项数*28 + 8, 视口高 - 24)`；
  宽度 `max(触发器宽, 120)`；下方空间不足且上方够 → **向上弹**（`.up`）；
  水平溢出右边界时左移到 `vw - 8 - width`（最小 8）；纵向 `clamp(8, …, vh-8-height)`。
- 关闭时机：点击外部（`mousedown` 不在 root/popup 内）、**窗口任意滚动（capture:true）立即关闭**
  （fixed 定位会跟错位）、`Esc`（阻止冒泡）、选中后关闭、`editable` 失焦提交后关闭。
  重开时重新 `place()`。
- 交互：`↓/Enter/Space` 打开；`↑/↓` 移动高亮（`li` 但 `mousedown.prevent` 防止抢焦点）；
  鼠标 hover 同步高亮；`Esc` 关闭。
- **`editable` 模式**：触发器换成 `<input class="sel-input">`（外层 `.sel-trigger.editable` 自己补
  按钮同款 1px 边框 + `:focus-within` 高亮 + `--ring`）；输入即过滤选项（`label.includes(q)`）；
  **回车优先提交自己打的文本**（自定义学科），只有先用 ↑/↓ 翻到选项时（`navigated`）回车才选中该选项；
  失焦也提交；提交走 `update:modelValue`。
- 触发器样式：`font-size 13px`、`padding 5px 9px`、圆角 8px；普通模式右侧内嵌 SVG chevron
  （10×6，`stroke-width 1.6`，展开时旋转 180°，0.18s）；组件自带 `min-width: 150px`
  ——卡片里的 ghost 行必须用 `.ghost .ghost-subject` 双类选择器覆盖它。

### 8.3 ghost 空白行（编辑页新增条目的统一入口）

| 卡片 | 显示条件 | 结构 | 输入后 |
|---|---|---|---|
| 作业 | `showGhost = 未用学科 > 0 && (homework.length === 0 \|\| 有任何写了内容的条目)` | Select（editable，占位 `学科`，宽 92px）+ `作业内容…` 输入框 + `要交` 复选框 + 两个 `visibility:hidden` 的删除/＋占位 | 文本非空即落库：学科 = 选中的或**第一个未用学科**，成功后 `ghostSubject` 清零、`ghostText` 清空、`ghostSubmit` 复位，焦点移到新生成的真实行末尾 |
| 演讲 | `showGhost = 未用学科 > 0 && (speeches.length === 0 \|\| 任何有学号的条目)` | 跨满两列；Select（editable，占位 `学科`，固定 **64px**，`padding:0`、文字右对齐）+ `学号` 输入框 | 同上（学科 = 第一个未用学科），焦点接力到该学科行 |
| 公告 | **无条件保留一行** | 灰掉（opacity .45）的禁用复选框（`title="保存后即可勾选完成"`）+ `新公告，回车或点「添加」保存…` + `添加` 按钮 | 点「添加」或回车 → `add_announcement`，行保留 |

- ghost 行统一样式：1px 虚线顶边 + `margin-top: 4px`（演讲还带 `padding-top: 4px`）。
- 学科初始为空（不预填），占位是通用词 `学科` 灰字，点开/点击不会被自动填入。
- 焦点接力靠 `inputRefs`（作业用 `科目:序号` 作 key，演讲用科目）+ `setSelectionRange(末尾)`，
  所以连续输入不用碰鼠标。

### 8.4 其它必须复刻的交互

- **就地编辑 300ms 防抖**：公告文本本地即时回显、300ms 后整体 `set_announcements`（不动 id/done）；
  编辑页四张卡共享一份 `draft`，`saveDraft()` 300ms 后整份 `set_day`，切天前先 `flushDraft()`。
- **tooltip 400ms**：`data-tip` 上停 400ms 才浮出；**焦点在 input/textarea 里一律不弹**；
  移开、按下、滚轮、滚动、窗口失焦都立刻隐藏；位置贴光标（偏移 14/20）、越界时上翻或夹到 6px 边距；
  宽度上限 420px、单行 nowrap、`pointer-events:none`、`z-index 2000`、淡入 0.1s。
  入口文案与 Win32 的 hint 逐字一致（不要改用浏览器原生 `title`）。
- **菜单 6 秒自动收起**（放映右键，见 §1.4 / §8.1）。
- **放映隐藏鼠标指针**：`.present { cursor: none }`，菜单打开时 `.menu-open { cursor: default }`。
- **日期栏滚轮横滚**：只在 `.days` 上监听 `wheel`，`preventDefault()`，`scrollLeft += delta`，
  **不切日期**；滚动条隐藏；切天后当前 chip `scrollIntoView`。
- **卡片内滚动、页面不出滚动条**：`.cards` 是 `grid-template-rows: auto minmax(0,1fr)`；
  `.card-body` / `.hw-list` 用 `flex:1; min-height:0; overflow-y:auto` 吃掉剩余高度；
  右列两张卡 `flex: 1 1 auto` 分摊高度（内容不满时留白），保证「演讲未填写」横幅出现时也不挤掉内容。
  最窄/最矮窗口（真机上限约 1536×905 @200% 缩放）下 `overflow:auto` 只作兜底。
- **公告区滚到最底**：挂载、条目数变化、`annScrollTick`（热启动）三种时机都 `scrollTop = scrollHeight`。
- **删到空学科不清学科行**：作业卡删掉某学科最后一条 → 只清空内容，学科行保留（可继续输入）；
  空条目只在内存里，不落盘。
- **调休显示**：课表卡标题 `课表（周三·调休）`；课表总览里调课格子红字；放映里调课 = 红底白字胶囊 + 红字课名。
- **设置页天数列表**默认滚到「今天」那行；今天不在列表就滚到第一个 ≥ 今天的行。
- **侧边栏「已保存 / 未保存」圆点**：`draftPending` 为真时橙色，否则蓝色；文案只有 tooltip。
- **切页高度稳定**：编辑页 `.toolbar`、设置/课表页 `.page-head` 都是 **52px**，左右内边距都是 16px
  （否则最左侧汉堡按钮会左右偏几像素）。
- **退出确认**：编辑页「完全退出」按钮点击后置 `quitting` 并 disabled（防连点重复出图 + 退出）。
