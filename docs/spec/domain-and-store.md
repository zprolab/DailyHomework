# 领域规则与持久化规格（提取自 Rust/Tauri 后端）

> **用途**：在删除现有实现之前，把 Tauri 版的领域语义与持久化行为固化成可照抄的规格，供 C++20 + CMake 的单一跨平台代码库（`core` 静态库 + Qt 6 外壳）1:1 重建。
>
> **来源（只读分析）**
> - `src-tauri/src/model.rs`（132 行，serde 数据模型）
> - `src-tauri/src/schedule.rs`（476 行，领域规则 + 25 个 `#[test]`）
> - `src-tauri/src/store.rs`（627 行，持久化 + 19 个 `#[test]`，其中 1 个 `#[ignore]`）
> - `src-tauri/src/main.rs`（1199 行，命令层 / 后台线程 / 生命周期）
> - `src-tauri/tauri.conf.json`（app version 1.5.5、identifier `com.homework.daily`）
> - `fixtures/data.sample.json`、`fixtures/data.canonical.json`（两版共用的编解码契约样本）
> - `AGENTS.md`（旧中文规格，用于交叉验证；**与代码不一致处以代码为准**，差异见第 6 节）
>
> **体例**：规则一律写成「输入 → 输出」；`JSON` 字段与函数名保持源码原样（snake_case / Rust 名）。凡我在代码里找不到依据的推断，都显式标注「待确认」。

---

## 1. 数据模型

### 1.1 `data.json` 顶层：`AppData`（model.rs:77-104）

`AppData` 带**容器级** `#[serde(default)]`，即：整个对象可以缺字段，缺的字段取 `AppData::default()` 里对应值；整个文件若缺失（或解析失败）由 `store::load_or_recover` 兜底。

| 字段（snake_case） | 类型 | 可选性 | 默认值 | 说明 |
|---|---|---|---|---|
| `version` | `u32` | 可缺省 | `1` | 见 1.5 |
| `weekly_schedule` | `Vec<Vec<String>>` | 可缺省 | `schedule::default_weekly_schedule()`（7 行 × 9 列，周一~周五有内容，周六周日为 9 个空串） | 读入后由 `normalize()` 规范化 |
| `days` | `BTreeMap<String, Day>` | 可缺省 | `{}` | key = `"YYYY-MM-DD"`；**键不做校验、不做删除**；`BTreeMap` 决定序列化顺序 = 日期字符串字典序 |
| `hidden_dates` | `Vec<String>` | 可缺省 | `[]` | 设置页「移出列表」的日期，只对没有条目的日期生效 |
| `pinned_dates` | `Vec<String>` | 可缺省 | `[]` | 设置页「快捷显示」，**不被任何判定函数读取**，只加一个日期栏入口 |
| `announcements` | `Vec<Announcement>` | 可缺省 | `[]` | 全局一列，不分日期 |
| `settings` | `Settings` | 可缺省 | `Settings::default()` | 见 1.4 |

`AppData::default()` 里的 `weekly_schedule` 是硬编码的默认周课表（schedule.rs:4-15，**必须逐字照抄**）：

```text
0(周一): 语文 数学 历史 物理 地理 体育 美术 英语 班会
1(周二): 数学 物理 英语 英语 语文 自习 信息 政治 体育
2(周三): 数学 语文 劳技 英语 英语 心理 数学 历史 生物
3(周四): 政治 生物 数学 语文 语文 英语 英语 物理 体育
4(周五): 语文 英语 英语 数学 体育 信息 物理 地理 音乐
5(周六): （9 个空串）
6(周日): （9 个空串）
```

### 1.2 `days[date]`：`Day`（model.rs:19-44）

`Day` **没有**容器级 `default`，只有逐字段 `#[serde(default)]`（`schedule_override` 用 `default = "default_override"`）。

| 字段 | 类型 | 可选性 | 默认值 | 说明 |
|---|---|---|---|---|
| `schedule_override` | `Vec<Option<String>>` | 可缺省 | `[null × 9]` | 长度语义 9；`null` = 用周模版，字符串 = 当日调课（`changed = true`）；`""` 是合法的 `Some("")`，见 6.2 |
| `homework` | `Vec<HomeworkItem>` | 可缺省 | `[]` | 同一 `subject` 可重复出现，各自带 `submit_next` |
| `speeches` | `Vec<SpeechEntry>` | 可缺省 | `[]` | |
| `copied_from_weekday` | `Option<usize>` | 可缺省 | `null` | 调休来源，`0=周一 … 6=周日`；`null` = 按真实星期几取模版 |

- `Day::default()` 与逐字段默认值等价（`schedule_override` 9 个 `None`、两个 vec 空、`copied_from_weekday` 为 `None`）。
- **条目字段全必填**：`HomeworkItem { subject: String, text: String, submit_next: bool }`、`SpeechEntry { subject: String, students: String }`、`Announcement { id: u64, text: String, done: bool }` 都没有 `#[serde(default)]`，也不接受 `null`。缺任一个字段 → **整份 `data.json` 解析失败** → 走备份恢复流程（见 3.6）。
- `copied_from_weekday` 是 `Option<usize>`：JSON 里可以是 `null` 或缺省；**负数或非整数会解析失败**。
- 已知字段类型不符（如 `"homework": null`、`"submit_next": "yes"`）→ 整份失败，不做「跳过该字段」的宽容处理。

### 1.3 `Announcement` 与公告 id

- `Announcement { id: u64, text: String, done: bool }`；**没有** `created_date` / `done_date` / `last_seen_date`（旧数据里的这些字段被忽略，回归用例 `ignores_legacy_fields_on_load`）。
- `AppData::next_announcement_id()` = `max(已有 id) + 1`，空列表时为 `1`（model.rs:129-131）。**不检查 id 重复**。
- 空 `text` 的公告是合法数据（`add_announcement("")` 能落盘，`normalize()` 不清理）→ 见 6.2。

### 1.4 `Settings`（model.rs:53-75）

容器级 `#[serde(default)]` + `Default` 实现，因此缺字段取下列默认值：

| 字段 | 类型 | 默认值 | 语义 / 使用点 |
|---|---|---|---|
| `image_interval_secs` | `u64` | `60` | 自动出图最小间隔；运行期取 `max(值, 10)` |
| `morning_cutoff` | `String` | `"08:00"` | 早晨分界线 `HH:MM`；解析失败在使用点回退 `08:00` |
| `show_badge` | `bool` | `true` | 放映是否画班徽（还需 `badge_available()`） |
| `english_split` | `bool` | `true` | 英语是否拆成 `英语A` / `英语B`（作业建议与演讲建议） |
| `ui_scale` | `f64` | `1.0` | 内置界面缩放倍率（前端 50%~150%，**Rust 侧不夹取**） |

已删除的旧字段（`data_dir`、`last_seen_date` 等）在加载时被忽略，且**不会再写回**（用例 `decodes_shared_fixture` 断言输出里不含 `created_date` / `data_dir`）。

### 1.5 `version` 字段语义

- 类型 `u32`，默认 `1`。
- **没有任何代码读取、比较或校验它**；没有迁移分支、没有「版本过新则拒绝加载」的逻辑。它只在解析时存下、序列化时原样写回（缺省时写成 `1`）。
- 与 app 版本无关（app 版本在 `tauri.conf.json` / `Cargo.toml`，当前 `1.5.5`）。
- 重写建议：保留该字段并按读到的原值回写，加载时**不因未知版本拒绝**；若将来要加迁移，必须另加判定（现状没有）。
- 交叉验证：native 版 `native/core/model.cpp:364-370` 只要求 `version` 是数字、负数钳成 `0`；Rust 的 `u32` 遇到负数会**整份解析失败**——这是两版的一个真实差异（见 6.3）。

### 1.6 未知字段与序列化行为（serde 细节，写 C++ 时必须对齐）

- 全仓库**没有** `deny_unknown_fields`，也**没有** `rename_all`：未知字段一律静默忽略（这正是旧字段/旧版本能继续读的原因）。
- 字段名就是结构体字段名（snake_case）；序列化顺序 = 声明顺序：
  - `AppData`: `version, weekly_schedule, days, hidden_dates, pinned_dates, announcements, settings`
  - `Day`: `schedule_override, homework, speeches, copied_from_weekday`
  - `HomeworkItem`: `subject, text, submit_next`；`SpeechEntry`: `subject, students`；`Announcement`: `id, text, done`
  - `Settings`: `image_interval_secs, morning_cutoff, show_badge, english_split, ui_scale`
- `days` 是 `BTreeMap` → 输出按 key 字典序；`hidden_dates` / `pinned_dates` 在 `normalize()` 后有序。
- 格式 = `serde_json::to_string_pretty`：**2 空格缩进**、`: ` 分隔、数组/对象逐元素换行、空数组 `[]`、空对象 `{}`、`Option::None` → `null`、非 ASCII 字符原样输出（UTF-8，不转义）。
- **`save()` 写出的文件末尾没有换行符**（`to_string_pretty` 不补）。
  注意契约样本 `fixtures/data.canonical.json` 末尾**有** `\n`：那是测试里 `canonical_text() + "\n"` 比出来的（store.rs:588-589, 617-620），不是运行期 `data.json` 的真实结尾。
- 解析端容忍：首部 BOM 被剥掉（`trim_start_matches('\u{feff}')`，连多个都剥）、尾部空白/换行无所谓，其他位置的非法内容直接失败。
- `parse_date` 用 `NaiveDate::parse_from_str(s, "%Y-%m-%d")`：chrono 对 `%m`/`%d` 允许 1~2 位数字（`"2026-1-1"` 这类可能被接受），越界日期（`2026-02-30`）失败。**待确认**：C++ 侧要决定是否同样接受不补零（`fixtures` 与本仓库数据都是补零的）。

### 1.7 `每日作业本机设置.json`：`LocalSettings`（main.rs:611-689）

- 路径：**数据目录 / `每日作业本机设置.json`**（跟着数据走）。旧位置（exe 同级同名文件）只在读取兜底时读一次，解析成功则**迁移**（写进数据目录 + 删旧文件）。
- 容器级 `#[serde(default)]` + `Default`：

| 字段 | 类型 | 默认值 | 语义 |
|---|---|---|---|
| `keep_awake` | `bool` | `true` | 放映时不熄屏（`SetThreadExecutionState(ES_DISPLAY_REQUIRED)`） |
| `lock_switching` | `bool` | `true` | 放映时吞掉 `Alt+Tab` / `Win` 键（低级键盘钩子；`Ctrl+Shift+Esc` 仍可用） |
| `screen_device` | `String` | `""` | 放映屏；空 = 自动（窗口当前所在那块），非空 = 显示器名 |
| `appearance` | `String` | `"system"` | `light` / `dark` / `system`；Rust 不校验取值、只透传（native 写回时空串归一为 `system`） |
| `sidebar_collapsed` | `bool` | `false` | **只是启动默认值**：运行时 Ctrl+B / 汉堡折叠不落盘 |

- 写入：`to_string_pretty` **+ `"\n"`**（与 `data.json` 不同，本机设置文件**有**尾换行）；先写 `每日作业本机设置.json.tmp`（`with_extension("json.tmp")`）再 rename，**没有 fsync**。
- 读取：数据目录文件 → 解析失败/不存在 → exe 同级旧文件 → 都不行 → 默认值。**类型不符会让整个文件解析失败**，随后的其它字段值也一起丢（native 版是逐字段忽略非法类型，两版不一致，见 6.3）。
- 该文件**不参与** dirty/撤销/`state-changed`（除了 `set_presenting` / `set_local_settings` 会即时应用电源与键盘钩子）。

### 1.8 一个数据目录里会出现的东西（重写时按此布局）

```text
<数据目录>/
  data.json                     主数据（无尾换行）
  data.json.tmp                 原子保存的临时文件（正常情况不残留）
  backups/data_YYYYMMDD_HHMMSS.json   滚动备份（≤30 份，同日只留最新）
  data.json.已迁移_YYYYMMDD_HHMMSS    迁移后旧目录里的留档（旧目录里出现）
  每日作业本机设置.json
  班徽.png                       通用版用户上传的班徽（可选）
  家庭作业图片/                  过期出图的归档目录
    家庭作业_<YYYY-MM-DD>.png
```
exe 同级：`每日作业数据/`（默认数据目录）、`家庭作业_<YYYY-MM-DD>.png`（当前图）、`WebView2数据/`（WebView2 缓存）、`每日作业配置.ini`（旧版遗留，启动即删）、`崩溃日志.txt` / `启动失败日志.txt` / `崩溃转储.dmp`（异常时写，正常启动后清）。

---

## 2. 领域规则

### 2.1 日期基础工具（schedule.rs:25-69）

| 函数 | 输入 → 输出 | 边界 |
|---|---|---|
| `today_string()` | 本地时区 `Local::now()` → `"%Y-%m-%d"` | 用系统本地时区，不是 UTC |
| `parse_date(s)` | 合法 → `Some(NaiveDate)`；否则 `None` | 严格 `%Y-%m-%d` 形状（数值位 chrono 可能允许不补零，待确认） |
| `format_date(d)` | → `"%Y-%m-%d"`（补零） | |
| `add_days(s, n)` | 可解析 → `s+n` 天；**不可解析 → 原样返回 `s`**（不报错） | 这会传染给 `get_week`（见 2.14） |
| `weekday_index(s)` | `Some(0..6)`，`0=周一 … 6=周日`；不可解析 → `None` | |
| `weekday_cn(s)` | `"星期一"…"星期日"`；不可解析 → `""` | |

### 2.2 `effective_schedule(data, date)` → `Vec<Slot>`，长度恒为 9（schedule.rs:78-95）

`Slot { name: String, changed: bool }`。

**输入 → 输出**
1. `base_idx = days[date].copied_from_weekday ?? weekday_index(date) ?? 0`
   - `copied_from_weekday` 是 `Some` 时**直接用**（即使 ≥ 7）。
   - `date` 非法 → `weekday_index = None` → 取 `0`（周一模版）。
2. `base = weekly_schedule.get(base_idx) ?? 空 vec`（`base_idx` 越界 → 9 个空节）。
3. 对 `i = 0..9`：
   - `base_name = base.get(i) ?? ""`
   - `ovr = days[date].schedule_override.get(i).flatten()`（无该天/越界/`None` → 无 override）
   - `Some(v)` 且 `v != base_name` → `{ name: v, changed: true }`
   - 否则 → `{ name: base_name, changed: false }`

**边界情形**
- 周六/周日且无 `copied_from_weekday`、无条目：`weekly_schedule[5]/[6]` 是 9 个空串 → 输出 9 个空 `Slot`（`changed=false`）。
- 调休：`copied_from_weekday = Some(0)` 的周六 → 完全按周一模版（`changed` 仍为 `false`，模版来源不影响 `changed`）。
- `copied_from_weekday = Some(9)`（手改文件）：`weekly_schedule.get(9)` 为 `None` → 全空课表，**不报错**；但该天仍因 `days` 有条目而是上学日（见 2.5）。
- `override == base_name`（包括两侧都是 `""`）→ `changed = false`。
- `override = Some("")` 而模版非空 → `name = ""`、`changed = true`（「空名字的红色调课」，见 6.2）。

### 2.3 `homework_suggestions(slots, english_split)` → `Vec<String>`（schedule.rs:98-115）

**输入 → 输出**：按 `slots` 顺序遍历，首次出现保序去重；
- `name` 为空 → 跳过；
- `name == "英语"` 且 `english_split == true` → 在该位置**依次**追加 `"英语A"`、`"英语B"`（各自在结果中不存在时）；
- 其它 `name` → 不存在则追加。

**边界情形**
- 课表里一节课是英语还是两节英语，结果相同（都是首次出现处一组 A/B）。
- `english_split == false` → 原样保留 `"英语"`；课表里若本来写了 `"英语A"` / `"英语B"`，两种设置下都照原样出现，**不合并**；`"英语A"` 不会触发拆分。
- 传入 `english_split` 的是参数（`speech_suggestions` 传 `data.settings.english_split`；`get_view_model` 传 `data.settings.english_split`）。
- 例（周四模版）→ `["政治","生物","数学","语文","英语A","英语B","物理","体育"]`（回归用例 `suggestions_dedup` 的期望值）。

### 2.4 `speech_suggestions(data, date)`（schedule.rs:118-121）

**输入 → 输出**：`next = next_school_day(data, date)` → `homework_suggestions(effective_schedule(data, next), data.settings.english_split)`。
即「**下一个上学日**的课表去重（含英语拆分）」；`date` 是页面日，与是否「仅要交」无关。

### 2.5 `is_school_day(data, date)` → `bool`（schedule.rs:126-131）

**输入 → 输出**
1. 若 `hidden_dates` 包含 `date` **且** `days` 不含 `date` → `false`（放假）。
2. 否则：`weekday_index(date) < 5`（周一~周五）→ `true`；周末 → 仅当 `days` 含 `date` 才 `true`；`date` 无法解析 → 仅当 `days` 含 `date` 才 `true`。

**边界情形**
- 「被移出列表」= 放假：既不是上学日，也不会被上一/下一个上学日选中；这就是用户标记国庆的办法。
- 移出的日期**后来一旦在 `days` 里有了条目** → 立即恢复成上学日（第 1 步不再生效）。
- 只要 `days` 里有该日期的键，就算是上学日——**哪怕这个 `Day` 完全是空的**（`Day::default()`）。所以「调休」的最小表示就是「建一个空的 Day 条目」。
- `pinned_dates` **完全不参与**本判定（回归用例 `pinned_dates_do_not_affect_rules`）。
- `days` 里若存在非日期形状的 key（手改文件），`date` 无法解析时也会因 `days.contains_key` 而算上学日。

### 2.6 `step_school_day(data, from, step)` / `prev_school_day` / `next_school_day`（schedule.rs:133-153）

**输入 → 输出**
1. `base = parse_date(from) ?? Local::now().date_naive()`（`from` 非法 → 以**系统今天**为起点）。
2. 最多循环 **14 次**：`d += step` 天，若 `is_school_day(data, d)` → 立即返回 `format_date(d)`。
3. 14 次都没命中 → 返回走满 14 步的 `format_date(d)`，**即使它不是上学日**。

`prev_school_day(data, today) = step_school_day(data, today, -1)`；`next_school_day(data, date) = step_school_day(data, date, +1)`。

**边界情形**
- 周五 `next` → 周一；周一 `prev` → 上周五（周末被跳过）。
- 周日在 `days` 里有条目：周五 `next` → 周日（周六仍跳过，除非周六也有条目）；周日 `next` → 周一；周一 `prev` → 周日。
- 整段假期一起跳：10-02/05/06/07 都在 `hidden_dates` 且无条目 → `next_school_day(10-02) = 10-08`（跳过 03/04 周末与 05/06/07 假期；回归用例 `removed_dates_count_as_holidays`）。
- 跨月/跨年由日期加减天然处理。
- **14 天是硬上限**：连续 14 天以上都不是上学日（超长假期）时，会返回一个「休息日」充当上一/下一上学日（不报错、不继续找）。这是 1:1 重建时必须保留的行为（或用等价策略覆盖同一用例）。
- 起点本身是上学日也会先走一步：`prev/next` 永不返回 `from` 自身。

### 2.7 `effective_today(data, today)` → `String`（schedule.rs:157-163）

**输入 → 输出**：`is_school_day(data, today) ? today : prev_school_day(data, today)`。

**边界情形**
- 周六/周日（无调休条目）→ 退到上周五。
- 今天被移出列表且无条目 → 退到上一个上学日。
- 今天被移出但**又有条目** → 今天自己。
- 周末有调休条目 → 今天就是那个周末日期。
- `today` 非法字符串 → 通常 `is_school_day = false` → `prev_school_day` 以系统今天为起点（结果与 `today` 无关）。

### 2.8 `launch_view(data, today, now_time)` → `{ date, submit_only }`（schedule.rs:178-191）

**输入 → 输出**
1. `t = effective_today(data, today)`。
2. 若 `t == today`（字符串相等）**且** `now_time < cutoff_time(settings.morning_cutoff)`：
   → `{ date: prev_school_day(data, today), submit_only: true }`（画面显示今天、作业是上一上学日勾了「要交」的）。
3. 否则 → `{ date: t, submit_only: false }`。

`cutoff_time(s)`：`NaiveTime::parse_from_str(s, "%H:%M")`，失败回退 `08:00`。

**边界情形**
- `now_time` **正好等于** cutoff（08:00:00）→ 不满足「早于」→ 走 else（今天、全部）。用例 `launch_after_cutoff` 就是 08:00 整。
- `morning_cutoff` 是任意字符串（如 `"7:30"` 或 `"八点"`）：前者被 chrono 接受，后者回退 08:00。
- 今天是休息日 / 今天被移出列表 → `t != today` → **无论几点都不进「仅要交」分支**（`date = 昨天那个上学日`、`submit_only = false`）。
- 早晨分支里 `prev_school_day` 会跳过被移出的前一日：10-02 的 07:30 且 10-01 已移出 → 页面日 `2026-09-30`（用例 `before_cutoff_skips_removed_prev_day`）。
- 语义闭环：`submit_only` 下 `display_date = next_school_day(页面日)`，当 `today` 是上学日时恰好等于 `today`。

### 2.9 `hidden_dates` 与 `pinned_dates`

**`hidden_dates`（领域数据）**
- 唯一作用是让 `is_school_day` 对被隐藏**且没有条目**的日期返回 `false`，从而连带影响 `effective_today` / `prev_school_day` / `next_school_day`。
- 不删 `days` 条目、不删图片、不改变 `get_view_model` 的取数逻辑。
- `normalize()` **保留过去的隐藏记录**（只 sort + dedup）；丢掉会让已标成放假的日期重新算成上学日（用例 `keeps_past_hidden_dates_on_load`、`holiday_marks_keep_today_on_last_school_day`）。
- `set_hidden_dates` 会顺手把同一天从 `pinned_dates` 里剔除（「移出列表 = 哪儿都不显示」）。

**`pinned_dates`（纯 UI 提示）**
- **不被任何判定函数读取**（`is_school_day` / `effective_today` / `prev` / `next` / `launch_view` 都不看它）。
- 效果仅是「编辑页日期栏多一格入口」（前端读取 `store.data.pinned_dates`）。
- `normalize()`：sort + dedup + 丢弃 `< today_string()` 的（字符串比较）+ 剔除同时被 `hidden_dates` 的。
- `set_pinned_dates` 命令本身只 sort + dedup（**不丢过去日期**，落到磁盘后要等下次加载才由 `normalize()` 清掉）。

### 2.10 `AppData::normalize()` 的全部清理规则（model.rs:107-127）

调用点：`store::load()`（以及 `load_or_recover` 里两条恢复路径解析成功后）——**每次加载后跑一次**。`save`/`save_now`/任何命令都**不会**调用它。

逐条（输入：任意可解析的 `AppData` → 输出：规范化后的 `AppData`）：
1. `weekly_schedule = normalize_weekly(...)`：行数 resize 到 **7**（多截断、少补空行），每行 resize 到 **9**（多截断、少补 `""`）。**不校验周模版内容**（空串照留）。
2. 对 `days` 的**每个** value：
   - `schedule_override.resize(9, None)`（多截断、少补 `None`）；
   - `homework.retain(|h| !h.text.trim().is_empty())`（只加了学科没写内容的空作业清掉；`subject` 为空但 `text` 非空**保留**）；
   - `speeches.retain(|s| !s.students.trim().is_empty())`（空学号清掉）。
3. `hidden_dates.sort(); hidden_dates.dedup();`（**不删除过去的**）。
4. `today = today_string()`（之后的比较都基于这一时刻的系统日期）。
5. `pinned_dates.sort(); pinned_dates.dedup(); pinned_dates.retain(|d| d.as_str() >= today.as_str());` 再 `retain(|d| !hidden_dates.contains(d))`。

**不做的（重写时别自作主张）**
- 不删空的 `Day`（哪怕 `homework`/`speeches` 都空、`schedule_override` 全 `None`）——空 `Day` 正是「调休上学日」的表示。
- 不校验/删除 `days` 的 key（保留非法日期键）。
- 不清理 `announcements`（空文本、重复 id、乱序都保留）。
- 不夹取 `settings`（`ui_scale` 可为 0 或 100、`image_interval_secs` 可为 0）。
- 不改 `version`。
- `pinned_dates` 用的是**字符串比较**：手改文件里的 `"2026-1-1"` 这类不补零日期，按字典序可能被判成「未来」而保留（`"2026-1-1" > "2026-01-01"`）。

### 2.11 公告可见性（schedule.rs:194-196）

`announcement_visible(a) = !a.done`。
- **全局一列、不分日期**；所有日期看到同一份列表。
- 勾完（`done = true`）放映不再显示，可取消勾选恢复显示。
- **没有「自动完成」机制**：不会因为换天/时间推移自动标完成。
- `get_view_model` 输出的 `announcements` 是过滤后的列表（只含未完成），顺序与 `data.announcements` 一致。
- **待确认/风险**：`text` 为空的未完成公告同样可见（`normalize()` 不清理）。

### 2.12 演讲空检测（schedule.rs:199-201、main.rs:316）

`speeches_empty(day) = day.speeches.iter().all(|s| s.students.trim().is_empty())`。
- 空列表 → `true`；有 `subject` 但 `students` 是空白 → 仍 `true`。
- `get_view_model.speech_warn = days[date] 不存在 ? true : speeches_empty(day)` → 放映页显示「⚠ 演讲提醒未填写」，编辑页顶部红条。

### 2.13 放映作业列表（schedule.rs:204-215）

`present_homework(data, date, submit_only)`：
1. `days[date]` 不存在 → 空 vec。
2. 过滤 `text.trim()` 非空。
3. `submit_only == true` → 再过滤 `submit_next == true`。
4. 保持原顺序与重复项（同一 subject 的多条都在）。

### 2.14 视图模型合成（main.rs:296-324 / 333-343）

`get_view_model(date, submit_only)` → `ViewModel`：

| 输出字段 | 取值 |
|---|---|
| `date` | 入参原样（**页面日 = 作业所属日**） |
| `display_date` | `submit_only ? next_school_day(data, date) : date` |
| `weekday` | `weekday_cn(display_date)` |
| `slots` | `effective_schedule(data, display_date)`（课表按**画面日**） |
| `announcements` | `!done` 的公告 |
| `homework` | `present_homework(data, date, submit_only)`（作业按**页面日**） |
| `homework_date` | `date` |
| `speeches` | `days[date].speeches` 原样（不存在 → 空） |
| `speech_warn` | 见 2.12 |
| `homework_suggestions` | `homework_suggestions(effective_schedule(data, date), settings.english_split)`（**页面日**的课） |
| `speech_suggestions` | `speech_suggestions(data, date)`（页面日的**下一上学日**） |
| `submit_only` | 入参原样 |

- **切换「全部/仅要交」不改变 `date`**（页面日不动，草稿不会被切走）。
- `date` **不做格式校验**：非法字符串会一路传到 `effective_schedule`（→ 周一模版）、`next_school_day`（→ 以系统今天为起点）。
- `get_week(monday)`：`i = 0..6` → `add_days(monday, i)` + `effective_schedule`；`monday` 非法时 `add_days` 原样返回 → **7 行同一个日期**。

### 2.15 规则里用到的常量一览

| 常量 | 值 | 位置 |
|---|---|---|
| `PERIODS` | `9` | model.rs:4 |
| 默认周课表 | 见 1.1 | schedule.rs:4-15 |
| `morning_cutoff` 默认 | `"08:00"` | model.rs:66 |
| `step_school_day` 搜索窗口 | `14` 天 | schedule.rs:135 |
| `image_interval_secs` 默认 / 下限 | `60` / `max(值,10)` 秒 | model.rs:67、main.rs:943 |
| `IMAGE_MAX_AGE` | 24 小时（`24*60*60` 秒） | store.rs:14 |
| `ARCHIVE_CHECK_SECS` | `300` 秒 | main.rs:24 |
| `MAX_BACKUPS` | `30` 份 | store.rs:11 |
| `HISTORY_LIMIT` / `HISTORY_COALESCE` | `60` 步 / `2500` 毫秒 | main.rs:47-48 |
| 自动保存防抖 | `1` 秒（后台线程 500ms 轮询） | main.rs:937 |

---

## 3. 存储行为

### 3.1 默认数据目录与「位置不留档」（store.rs:16-37）

- `exe_dir()` = `std::env::current_exe()` 的父目录；取不到时回退 `"."`。
- `default_data_dir()` = `exe_dir()/每日作业数据`（常量 `DEFAULT_DIR`）。
- **位置不被记住**：不写 ini 指针、不写 `%APPDATA%` / `%LOCALAPPDATA%`、不留指针文件。
- `remove_legacy_ini()`：删 exe 同级的 `每日作业配置.ini`（常量 `LEGACY_INI_NAME`），在 `main()` 里、`setup` 之前调用；失败忽略。
- `has_data(dir)` = `dir/data.json` **存在**（`Path::exists`，目录同名也算「存在」，随后 `load` 会失败并走恢复）。
- `data_file(dir)` = `dir/data.json`；`badge_file(dir)` = `dir/班徽.png`；`backups_dir(dir)` = `dir/backups`。

### 3.2 这次启动用哪个目录：`resolve_data_dir()`（main.rs:1030-1062）

1. `default = default_data_dir()`；若 `has_data(default)` → 直接用，**不弹窗**。
2. 否则先 `create_dir_all(default)`（保证用户反悔时也有可用位置）。
3. 弹原生对话框（`MessageDialogButtons::OkCancelCustom("在默认位置新建", "指定目录…")`）：
   - `Ok` → 用默认目录；
   - 否则 `blocking_pick_folder()`（标题「选择数据目录」）→ 选中则用它（`into_path` 失败回退默认）；**取消 → 也用默认目录**。
4. 随后 `setup` 里：`data_dir = 该目录` → `set_local_settings_dir(该目录)` → `store::load_or_recover(该目录)` → `disk_stamp` → `notice` → `archive_images()` → 建窗。

### 3.3 加载：`load(dir)`（store.rs:73-82）

1. `read_to_string(dir/data.json)`：文件不存在/不是合法 UTF-8 → `Err("读取 <path> 失败：<io>")`。
2. `text.trim_start_matches('\u{feff}')`：剥掉**所有**前导 BOM。
3. `serde_json::from_str::<AppData>(text)`：失败 → `Err("解析 <path> 失败：<e>")`。
4. `data.normalize()`。
5. 返回 `Ok(data)`。

### 3.4 原子保存：`save(dir, data)` 的每一步（store.rs:123-142）

1. `create_dir_all(dir/backups)`（顺带建数据目录）；失败 → `Err`（保存中止）。
2. 若 `dir/data.json` 已存在：`fs::copy` 到 `dir/backups/data_<本地时间 %Y%m%d_%H%M%S>.json`。**备份失败 → 直接 Err，不写主文件。**
   - 时间戳精度 = 秒；同一秒内保存两次会**覆盖同一份备份**。
3. `json = serde_json::to_string_pretty(data)`。
4. `tmp = dir/data.json.tmp`：`File::create`（截断）→ `write_all` → **`sync_all()`（fsync 该文件）**；任一步失败 → `Err`。
5. `fs::rename(tmp, dir/data.json)` 原子替换；失败 → `Err("替换数据文件失败：<e>")`。
   - 注意：**没有 fsync 目录项**（跨崩溃的 rename 持久性交给文件系统），备份复制也没有 fsync。
6. `prune_backups(dir)`（失败忽略）。
7. 返回 `Ok(())`。

### 3.5 备份命名与修剪（store.rs:144-165）

- 命名：`data_YYYYMMDD_HHMMSS.json`（**本地时间**）。
- `list_backups(dir)`：`backups/` 下所有 `extension == "json"` 的文件（**不检查 `data_` 前缀**）。
- `prune_backups`：
  1. 按**完整路径字符串**升序排序（文件名的时间戳格式使字典序 = 时间序）。
  2. 按 `name.get(5..13)` 分组（`data_` 之后的 8 位 = `YYYYMMDD`），每组保留**最后一个**（最新）。
     - 名字短于 14 字节/切片落在非 UTF-8 边界 → key 为 `""`，这些文件会挤进同一组。
     - 手工放进 `backups/` 的其它 `*.json` 会被一起分组、一起修剪、**可能被删**。
  3. `survivors` 再排序，`while len > MAX_BACKUPS(30) { remove(0) }`（丢最旧的日期组）。
  4. 删除所有不在 `survivors` 里的 json（删除失败忽略）。
- 效果：最多 30 份、**同一天只留最新**（回归用例 `backup_created_and_pruned_per_day` 断言同一天只有 1 份）。

### 3.6 损坏恢复：`load_or_recover(dir)`（store.rs:85-106）

1. `load(dir)` 成功 → `(data, None)`。
2. 失败 → `list_backups(dir)` → 按**路径降序**（文件名新 → 旧）逐个尝试：
   - `read_to_string`（**不剥 BOM**）→ `serde_json::from_str::<AppData>` 成功 → `normalize()` → `fs::copy(备份, dir/data.json)` **回写主文件** → 返回 `(data, Some("主数据损坏（<e>），已从备份 <文件名> 恢复"))`。
   - 失败继续下一个。
3. 全部失败 → `(AppData::default(), Some("数据文件不可用（<e>），已创建空数据"))`（**不写文件**，内存里用默认数据）。
4. `notice` 由 `payload()` 用 `Option::take()` 取走，随下一次 `state-changed` 一次性发给前端（toast）。

边界：BOM 文件在 `load` 阶段就被接受，所以不会进恢复流程（用例 `accepts_utf8_bom` 断言 `notice.is_none()`）；空目录 → 默认数据 + notice（用例 `empty_dir_gives_default_with_notice`）。

### 3.7 更换数据目录：`migrate_data_dir(current, new_dir)`（store.rs:184-196）+ `choose_data_dir`（main.rs:536-555）

`store::migrate_data_dir`：
1. `new_dir == current`（`PathBuf` 逐分量相等）→ `Err("新目录与当前目录相同")`。
2. `copy_recursive(current, new_dir)`：递归复制目录树（子目录 + 文件），**不带 fsync、不跳过 `new_dir` 本身**；失败 → `Err("复制数据失败：<e>")`。
3. `load(new_dir)` 校验 → 失败 → `Err("新目录数据校验失败：<e>")`（**此时已复制的内容留在新目录里**）。
4. 旧 `current/data.json` 存在 → rename 成 `current/data.json.已迁移_<本地时间戳>`（`({DATA_FILE}.已迁移_{stamp})`），失败忽略。
5. 返回 `Ok(new_dir)`。

`choose_data_dir` 命令（前端设置页触发）：
1. `blocking_pick_folder()`；取消 → `Ok(None)`（什么都不做）。
2. `migrate_data_dir` 成功后：把旧目录的 `每日作业本机设置.json` 复制到新目录（**仅当旧文件存在且新文件不存在**）；`set_local_settings_dir(新目录)`；把共享的 `data_dir` 改成新目录；刷新 `disk_stamp`；`mark_changed`（→ `dirty = true` → 下一次自动保存会把数据写进新目录；并 emit `state-changed`）。
3. 返回新目录路径字符串。
- **「只对本次运行有效」**：位置不留档，下次启动仍从 exe 同级的默认位置开始（那里已经 `has_data == false`，因为主文件被改名留档了）。
- 迁移**不搬** exe 同级的出图，也**不删除**旧目录的其余内容（backups、班徽、存档、本机设置原件都留在旧目录）。

### 3.8 出图：写入、保留集与归档

**写入 `write_image(dir, date, bytes)`（store.rs:200-208）**
1. `parse_date(date).is_none()` → `Err("日期不合法：<date>")`（**路径永不由前端拼接**）。
2. `create_dir_all(dir)`；`fs::write(dir/家庭作业_<date>.png, bytes)`（**直接覆盖，非原子**）。
3. 返回路径。

调用点 `export_image(bytes, date)`（main.rs:831-841）：`write_image(exe_dir(), date, bytes)` → `image_dirty = false`、`last_image = now` → emit `image-exported`（路径字符串）。
- 自动出图传 `date = 今天`（前端语义），手动出图传 `date = 页面日`。
- **`write_image` 不删其他日期的图**：自动图与手动图并存（用例 `write_image_keeps_other_days`）。

**保留集 `keep_image_dates()`（main.rs:845-854）**：`real_today = today_string()`；`effective = effective_today(data, real_today)`；返回 `[real_today]`，当 `effective != real_today` 时再追加 `effective`（休息日时保留集有 2 个日期）。

**归档/清理 `relocate_images(exe_dir, data_dir, keep_dates, now)`（store.rs:214-280）**
遍历 `exe_dir` 的**直接子项**：
1. 文件名必须形如 `家庭作业_<date>.png`（前缀 `家庭作业_` + 后缀 `.png`），否则跳过（`别的东西.txt` 不动）。
2. `parse_date(date).is_none()` → 跳过；`keep_dates` 里字符串相等的 → 跳过。
3. 取 `metadata().modified()`；取不到 → 跳过。
4. 若 `now` 是 `Some`：
   - `gen_date` = 文件 mtime 转**本地时区**后的 `%Y-%m-%d`；
   - 若 `gen_date != date`：`age = now.duration_since(mtime)`（`now` 比 mtime 早 → 跳过不搬）；`age < 24h` → 跳过。
   - 若 `gen_date == date`：**不检查年龄，立即搬**。
5. 目标目录 = `data_dir/家庭作业图片`（`create_dir_all` 失败 → 跳过）；目标同名文件存在 → 先删；`rename` 成功，或 `copy` + `remove` 双成功 → 记入返回值。
6. 返回被搬走的文件名列表（顺序 = 目录枚举顺序，未定义）。

封装：
- `archive_stale_images(..., now)` = `relocate_images(..., Some(now))`（按过期规则）。
- `clear_images(...)` = `relocate_images(..., None)`（**无条件**搬走所有非保留图，设置页「清理 exe 同级图片」；同样移入存档、**不删除**）。

**调度的真实数值**
- `IMAGE_MAX_AGE = 24 * 60 * 60 = 86400` 秒（store.rs:14）。
- `ARCHIVE_CHECK_SECS = 300` 秒（main.rs:24）→ 后台线程每 300 秒跑一次 `archive_images`。
- 启动时 `setup` 里**先跑一次** `archive_images`（main.rs:1154）。
- 结果含义：「文件名日期 == 文件 mtime 本地日期」的图（自动出的今天图、手动出的今天图）**隔天即搬**，与 24 小时无关；「文件名日期 != mtime 日期」的图（手动出的其他天）**满 24 小时**才搬。
- 保留集里的图永远不动；清理命令也只搬非保留图。

### 3.9 班徽文件（store.rs:44-67，main.rs:873-931）

- `badge_file(dir) = dir/班徽.png`；`has_badge(dir) = is_file()`。
- `read_badge(dir)`：`fs::read` 成功且**非空** → `Some(bytes)`；否则 `None`。
- `write_badge(dir, bytes)`：空 → `Err("图片是空文件")`；`create_dir_all(dir)`；`fs::write`（直接覆盖，非原子）。
- 命令层：
  - `get_badge_image()` → `Option<Vec<u8>>`。
  - `upload_badge()`：对话框过滤 `png/jpg/jpeg/bmp/gif/webp` → 读文件 → **只查魔数**（PNG `89 50 4E 47`、JPEG `FF D8 FF`、GIF `GIF8`、BMP `BM`、WebP `RIFF....WEBP`）→ `write_badge` → 记一步撤销（`"上传班徽"` / `"cmd:上传班徽"`）并把 `settings.show_badge = true` → `mark_changed`。
  - `delete_badge()`：删 `班徽.png`（不存在也算成功，返回值区分）→ 记一步撤销（`"删除班徽"`）并把 `settings.show_badge = false`。

### 3.10 本机设置的读写与迁移（main.rs:638-689）

- 位置解析：静态 `LOCAL_DIR: Mutex<Option<PathBuf>>`；`local_settings_dir()` = `LOCAL_DIR` 或 `default_data_dir()`。`setup` 时设成解析出的数据目录；`choose_data_dir` 时同步更新。
- `load_local_settings()`：
  1. 读数据目录里的文件 → 解析成功 → 用它；
  2. 否则读 exe 同级旧文件 → 解析成功 → **写到数据目录 + 删旧文件** → 用它；
  3. 否则 `LocalSettings::default()`。
  - 读不出来/解析失败**绝不阻塞启动**；数据目录里的坏文件不会被修（下次仍走 2/3）。
- `save_local_settings(s)`：`to_string_pretty` + `"\n"` → 写 `<file>.tmp`（`with_extension("json.tmp")`，即 `每日作业本机设置.json.tmp`）→ rename；无 fsync。
- 何时写：`set_local_settings`（设置页）与 `set_present_monitor`（选放映屏）。
- 何时读：`get_local_settings`、`set_presenting`（决定放映时是否常亮/锁切换）、`set_local_settings`、`set_present_monitor`。

### 3.11 legacy `每日作业配置.ini` 清理

- `store::remove_legacy_ini()` 删 `exe_dir()/每日作业配置.ini`；在 `main()` 里（`TcpListener::bind` 之后、`tauri::Builder` 之前）调用一次，失败静默。
- 与之配套：`Settings` 已无 `data_dir` 字段，所以「上次用的目录」不再被记住。

### 3.12 崩溃/启动失败日志与清理（main.rs:957-969, 1064-1071, 1155-1168）

- 常量：`启动失败日志.txt`、`崩溃日志.txt`、`崩溃转储.dmp`（`WINDOW_LABEL = "main"`）。
- panic hook（`main()` 里装）：把 `format!("{info}\n")` 写进 `exe_dir()/崩溃日志.txt`，再调用原 hook（release 是 `panic = "abort"`）。
- 建窗失败（`setup_main_window` 两次尝试都失败）→ 写 `exe_dir()/启动失败日志.txt`，弹原生错误框，`app.exit(1)`。
- 建窗重试：先 `attempt_main_window(app, Some(exe_dir()/WebView2数据))`；失败 → `remove_dir_all` 该缓存目录 → 再试一次；再失败才报错。
- `frontend_ready()` 里 `clear_stale_logs(data_dir)`：删 **`崩溃日志.txt`、`启动失败日志.txt`、`崩溃转储.dmp`** 三个文件（exe 同级与数据目录**两处都删**）。
  - 注意：**没有** `渲染诊断.txt`（AGENTS.md 提到它，Tauri 版代码里不存在，见 6.1）。

---

## 4. 单元测试清单

`src-tauri` 里共有 **44 个 `#[test]`**：`schedule.rs` 25 个 + `store.rs` 19 个；其中 `regenerate_canonical_fixture` 标了 `#[ignore]`，所以 `cargo test` 默认执行 **43 个**。
（AGENTS.md 写的「41 个（store 16 + schedule 25）」已过期，见 6.1。）

两个测试模块的公共设施（C++ 侧需要一个等价的可写临时目录）：
- `schedule::tests::data()` → `AppData::default()`。
- `store::tests::temp_dir(tag)` → `std::env::temp_dir()/hwclient_test_<tag>_<pid>`（先 `remove_dir_all` 再 `create_dir_all`）；`sample()` → 默认数据 + `2026-10-01` 一条 `{语文, 背书, submit_next: true}`；`list_backups`、`list_migrated`、`image_names` 三个辅助；`fixture_path(name)` = `CARGO_MANIFEST_DIR/../fixtures/<name>`。

### 4.1 `schedule.rs`（25）

| # | 测试名 | 断言的行为 |
|---|---|---|
| 1 | `default_weekly_is_7x9` | 默认周课表 7 行、每行 9 列；`w[0][0] == "语文"`；周六(索引 5)、周日(索引 6) 全为空串 |
| 2 | `weekday_helpers` | `weekday_index("2026-10-01") == Some(3)`（周四）；`weekday_cn` = `"星期四"`；`add_days(+1) == "2026-10-02"`、`add_days(-3) == "2026-09-28"` |
| 3 | `effective_uses_template` | 2026-10-01（周四）第 0 节 `name == "政治"`、`changed == false`（取自周四模版） |
| 4 | `override_marks_changed` | `schedule_override[0] = Some("数学")` → 该节 `name == "数学"`、`changed == true`；未改的 `[1]` `changed == false` |
| 5 | `override_same_as_base_not_changed` | `override[0] = Some("政治")`（与模版相同）→ `changed == false` |
| 6 | `copied_from_weekday` | 2026-10-03（周六）条目里 `copied_from_weekday = Some(0)` → 第 0 节 `name == "语文"`（用周一模版，不是周六空模版） |
| 7 | `suggestions_dedup` | 周四课表 + `english_split=true` → 建议恰好等于 `["政治","生物","数学","语文","英语A","英语B","物理","体育"]`（首次出现保序、去重、英语在首次出现处拆 A/B） |
| 8 | `suggestions_english_split_once` | 2026-09-30（周三，两节英语）→ 以「英语」开头的建议**恰好 2 个**，且同时含 `英语A`、`英语B` |
| 9 | `english_not_split_when_disabled` | `settings.english_split = false`：`homework_suggestions(slots, false)` 含 `"英语"`、不含 `英语A`/`英语B`；`speech_suggestions(data, ...)` 同样（读 `data.settings`） |
| 10 | `speech_uses_tomorrow` | 2026-10-01 的演讲建议 == 2026-10-02 的作业建议，且 **!=** 当天的作业建议 |
| 11 | `prev_school_day_skips_weekend` | `prev_school_day("2026-10-05") == "2026-10-02"`（周一→上周五）；`prev_school_day("2026-10-02") == "2026-10-01"` |
| 12 | `next_school_day_skips_weekend` | `next_school_day("2026-10-02") == "2026-10-05"`（周五→下周一）；`next_school_day("2026-10-01") == "2026-10-02"` |
| 13 | `next_school_day_respects_makeup` | 给 2026-10-04（周日）建空条目 → `next_school_day("2026-10-02") == "2026-10-04"`（周六 10-03 仍跳过）；`next_school_day("2026-10-04") == "2026-10-05"` |
| 14 | `prev_school_day_uses_makeup` | 同上数据：`prev_school_day("2026-10-05") == "2026-10-04"` |
| 15 | `launch_before_cutoff` | 2026-10-02（周五）07:30 → `submit_only == true`、`date == "2026-10-01"`（页面日退到上一上学日） |
| 16 | `launch_after_cutoff` | 同一日期 08:00 **整** → `submit_only == false`、`date == "2026-10-02"`（临界值不算「早于」） |
| 17 | `launch_on_rest_day_uses_prev_school_day` | 2026-10-03（周六）09:00 → `submit_only == false`、`date == "2026-10-02"`；`effective_today(周六) == "2026-10-02"` |
| 18 | `launch_on_makeup_weekend_is_school_day` | 2026-10-04（周日）建条目 → 09:00 时 `date == "2026-10-04"`、`submit_only == false`；`effective_today == "2026-10-04"` |
| 19 | `removed_today_falls_back_to_prev_school_day` | `hidden_dates += 2026-10-01` → `effective_today("2026-10-01") == "2026-09-30"`；09:00 时 `launch_view.date == "2026-09-30"`、`submit_only == false`；再给该日建条目 → `effective_today` 回到 `"2026-10-01"` |
| 20 | `before_cutoff_skips_removed_prev_day` | `hidden_dates += 2026-10-01`，2026-10-02 的 07:30 → `submit_only == true`、`date == "2026-09-30"`（跳过被移出的周四） |
| 21 | `removed_dates_count_as_holidays` | 隐藏 10-02/10-05/10-06/10-07 且无条目：`is_school_day(10-02) == false`；`effective_today(10-02) == "2026-10-01"`；`next_school_day(10-02) == "2026-10-08"`（跳过整段假期）；给 10-06 建条目后 `is_school_day(10-06) == true`、`next_school_day(10-02) == "2026-10-06"` |
| 22 | `pinned_dates_do_not_affect_rules` | `pinned_dates += 2026-10-03(周六) / 2026-10-07(周三)`：周六仍不是上学日、周三仍是；`effective_today(周六) == "2026-10-02"`；`next_school_day(10-01) == "2026-10-02"`；`prev_school_day(10-08) == "2026-10-07"`（快捷显示不改变任何判定） |
| 23 | `announcement_visibility_is_global` | `done=false` → 可见；`done=true` → 不可见（与日期无关） |
| 24 | `speeches_empty_check` | 空 `speeches` → `true`；只有 `subject`、`students=""` → 仍 `true`；补一条 `students="24"` → `false` |
| 25 | `present_homework_filters` | 三条作业（语文 `submit_next=true`、数学 `false`、物理 `text="  "` 且 `true`）：`submit_only=false` → 2 条（空白文本被丢）；`true` → 1 条且 `subject == "语文"`；查不存在的 2026-10-09 → 空 |

### 4.2 `store.rs`（19，含 1 个 `#[ignore]`）

| # | 测试名 | 断言的行为 |
|---|---|---|
| 1 | `save_and_load_roundtrip` | `save` → `load` 得到的 `AppData` 与写入的**完全相等**（`PartialEq` 逐字段，含 `version` / `days` / `settings`） |
| 2 | `backup_created_and_pruned_per_day` | 连续 3 次 `save`（每次 `sleep 1100ms` 跨秒）→ `backups` 里只剩 **1** 份（同一天只留最新） |
| 3 | `recover_from_corrupt_main` | 两次 `save` 后把主文件写成 `"{ 损坏"` → `load_or_recover` 恢复出与样本相等的数据、`notice` 非空、且主文件已被回写（`load` 成功） |
| 4 | `ignores_legacy_fields_on_load` | 含 `announcements[].created_date` / `done_date`、`settings.data_dir` / `last_seen_date` 的 JSON 能解析成功；公告 1 条、`done == true`、`text == "旧公告"`（未知字段被忽略、不报错） |
| 5 | `empty_dir_gives_default_with_notice` | 空目录 → `days` 为空（默认数据）+ `notice` 非空 |
| 6 | `accepts_utf8_bom` | 主文件前面加 `\u{feff}` → `load` 结果与样本相等；`load_or_recover` 也相等且 **`notice == None`**（BOM 不触发恢复流程） |
| 7 | `migrate_copies_and_verifies` | `migrate_data_dir(src, dst)` 成功后 `load(dst)` 与样本相等；`has_data(src) == false`（旧目录不再像「有数据」）；旧目录里恰有 **1** 个 `data.json.已迁移_` 前缀文件 |
| 8 | `has_data_only_when_data_file_exists` | 目录不存在 → `false`；目录存在但无文件 → `false`；`save` 之后 → `true` |
| 9 | `migrate_rejects_same_dir` | `migrate_data_dir(src, src)` 返回 `Err` |
| 10 | `write_image_keeps_other_days` | `write_image(dir,"2026-09-30")` → 文件名恰为 `家庭作业_2026-09-30.png`；再写 `2026-10-01` → 两张并存；非法日期 `"../坏日期"` → `Err` |
| 11 | `stale_images_move_into_data_dir` | exe 级有 09-30、10-08 两张图与一个 `别的东西.txt`；`keep=[2026-09-30]`、`now=当前` → 什么都不搬；`now+25h` → 只搬 `家庭作业_2026-10-08.png`，出现在 `数据目录/家庭作业图片/`，保留图不在存档、仍在 exe 级，`别的东西.txt` 不动；把 `keep` 换成 `[2026-10-01]`、`now+26h` → `2026-09-30` 那张也被搬走 |
| 12 | `todays_image_archives_next_day_within_24h` | 用**真实** `today` / `tomorrow` 各写一张；`keep=[today,tomorrow]` → 0 张被搬；`keep=[tomorrow]`、`now+1h` → `today` 那张被搬（文件名日期 == mtime 日期时，隔天不到 24 小时也即时归档） |
| 13 | `manual_other_day_image_waits_24h` | 写 `today-3` 的图（文件名日期 != mtime 日期）、`keep=[today]`：`now` 时刻不搬；`now+25h` → 搬走该图（必须满 24 小时） |
| 14 | `clear_images_moves_everything_but_kept` | 写 09-30 / 10-08 / 10-09 三张，`clear_images(keep=[2026-10-09])` → 搬走 09-30 与 10-08（排序后断言），exe 级只剩 10-09，10-08 出现在存档 |
| 15 | `keeps_past_hidden_dates_on_load` | 存 `hidden_dates = [10-01, 10-02, 2999-01-01]`、`pinned_dates = [2020-01-01, 2999-02-01]` → `load` 后 `hidden_dates` **三条全保留**（过去的也在）；`pinned_dates` 只剩 `["2999-02-01"]`（过去的被 `normalize` 丢掉） |
| 16 | `holiday_marks_keep_today_on_last_school_day` | `days` 清空 + 隐藏 10-01/10-02/10-05/10-06/10-07 → `load` 后 `effective_today("2026-10-06") == "2026-09-30"`、`next_school_day("2026-10-06") == "2026-10-08"` |
| 17 | `decodes_shared_fixture` | 解析 `fixtures/data.sample.json`：`hidden_dates == ["2026-10-02","2026-10-05"]`、`pinned_dates == ["2026-10-08"]`、`settings.english_split == false`、`ui_scale == 1.25`、`image_interval_secs == 90`、`days.len() == 2`；序列化输出**不含** `created_date` / `data_dir`；再解析回来与原对象相等（round-trip） |
| 18 | `serialization_matches_canonical_fixture` | `serde_json::to_string_pretty(decode_fixture()) + "\n"` 与 `fixtures/data.canonical.json` **逐字节相同**（刻意不跑 `normalize()`，因为它依赖当天日期；这也定义了 C++ 侧必须复现的黄金字节） |
| 19 | `regenerate_canonical_fixture` | `#[ignore]`：把 `canonical_text() + "\n"` 写回 `fixtures/data.canonical.json`（只在有意改契约时手动 `cargo test -- --ignored` 跑） |

---

## 5. 命令层语义（与 UI 无关但必须保留的部分）

### 5.1 命令清单（`invoke_handler` 注册的 30 条，main.rs:1109-1140）

**只读**：`get_state`、`get_launch_view`、`get_view_model(date, submit_only)`、`get_week(monday)`、`get_local_settings`、`get_badge_image`。
**改数据（都会 `mark_changed` → `dirty = true` + `image_dirty = true` + 广播 `state-changed`）**：`set_day(date, day, token?)`、`add_day(date, copied_from_weekday?)`、`delete_day(date)`、`set_announcements(list)`、`add_announcement(text)`、`set_weekly_schedule(rows)`、`update_settings(settings)`、`set_hidden_dates(list)`、`set_pinned_dates(list)`、`undo`、`redo`、`upload_badge`、`delete_badge`、`choose_data_dir`（也改目录）。
**持久化/生命周期**：`save_now`、`frontend_ready`、`quit_app`、`export_image(bytes, date)`、`clear_images`。
**本机行为**：`set_presenting(on)`、`set_local_settings(settings)`、`set_present_monitor(name?)`、`open_data_dir`、`break_history_coalescing`。

命令级要点（写 C++ 时容易漏的）：
- `set_day`：先 `schedule_override.resize(9, None)`，再 `homework.retain(text.trim() 非空)`、`speeches.retain(students.trim() 非空)`（**空条目落盘前就丢**），然后记一步撤销、`days.insert(date, day)`；**`date` 不做格式校验**。
- `add_day`：校验 `date` 格式（`Err("日期格式不正确")`）；已存在 → `Err("该日期已存在")`；`copied_from_weekday` **≥7 时归一为 `None`**；插入 `Day::default()`。
- `delete_day`：不校验、直接 `days.remove(date)`。
- `set_hidden_dates` / `set_pinned_dates`：入参先 sort + dedup；设 `hidden_dates` 时**顺手从 `pinned_dates` 删掉同一天的**；两者都**不**做「过去日期」过滤（那要等下次加载时的 `normalize`）。
- `set_announcements`：整列替换（前端 300ms 防抖，token 固定 `"cmd:公告"` → 连续编辑并入一步）。
- `add_announcement`：`id = max+1`、`done = false`、**不校验 `text` 非空**。
- `set_weekly_schedule`：`normalize_weekly(rows)`（7×9）。
- `update_settings`：整份 `Settings` 替换，不做范围校验。
- `save_now`：`store::save` 成功后 `dirty = false`；**不刷新 `disk_stamp`**（见 6.2）。
- `export_image`：`write_image` → `image_dirty = false`、`last_image = now`；emit `image-exported`。
- `clear_images`：返回被搬走的数量（`usize`）。
- `open_data_dir`：`create_dir_all` + 硬编码 `explorer <dir>`（Windows 专用）。
- `frontend_ready`：清上一轮日志（3.12）+ `show_main`（重载判定 + unminimize/show/focus + emit `activated`）。

### 5.2 事件

| 事件 | 载荷 | 触发 |
|---|---|---|
| `state-changed` | 完整 `StatePayload`（见 5.4） | 每次 `mark_changed`（所有改数据命令）、`reload_if_externally_changed` 成功/失败路径、`choose_data_dir` |
| `activated` | `null`（单元类型） | 每次 `show_main`：即 `frontend_ready` 与单实例唤出（后启动进程发 `SHOW`）两条路径。**3 秒兜底线程只调 `win.show()`，不发这个事件** |
| `auto-image-request` | `null` | 后台线程判定「该自动出图」时（5.5） |
| `image-exported` | 路径字符串 | `export_image` 成功后 |

### 5.3 撤销/重做（快照栈，main.rs:42-130, 145-149, 382-428）

结构与常量：`HISTORY_LIMIT = 60`、`HISTORY_COALESCE = 2500ms`。
`HistStep { data: AppData, date: String, action: String, token: String, at: Instant }`。
`History { undo: Vec<HistStep>, redo: Vec<HistStep>, suppress: bool }`。
**快照是「修改之前的整份 `AppData` + 当时的页面日」**（`before.clone()`），不是 diff。

- `record(before, date, action, token)`：
  1. 若 `!suppress && token 非空` 且栈顶 `token` 相同且 `now - 栈顶.at <= 2500ms` → **并入上一步**：只把栈顶 `at` 往后推、清空 `redo`、**不压新步、不覆盖那一步的状态**（所以「连续打字」撤销一次回到打字前）。
  2. 否则：`suppress = false`；压入新步；`undo.len() > 60` 时丢最旧；清空 `redo`。
- `undo(current, date)`：无步 → `None`；弹出一步，把**当前**状态以 `action = 被撤销那步的 action`、`token = ""` 压进 `redo`，`suppress = true`；返回那一步。
- `redo(current, date)`：对称（把当前状态压回 `undo`，`suppress = true`）。
- `apply_history_step`：`data = step.data`、`view_date = step.date` → `mark_changed`（因此撤销也会触发自动保存与自动出图）→ 返回 `{ date, action }`。
- **`undo` / `redo` 在 `presenting == true` 时直接返回 `None`（放映中不生效）**，也不改变 `suppress`。
- `break_history_coalescing()`：只置 `suppress = true`（前端在切天/重载后调用，保证下一次编辑必须开新步）。
- `History::reset()`（清空两栈 + `suppress = true`）**在代码里从未被调用**（死代码）。
- 记录点与 token：
  | 命令 | action | token |
  |---|---|---|
  | `set_day` | `编辑当天内容` | 入参 `token`，缺省 `"day"`（前端用按日期/字段区分的 token 让连续输入合并） |
  | `add_day` | `新增天数` | `cmd:新增天数#<date>` |
  | `delete_day` | `删除天数` | `cmd:删除天数#<date>` |
  | `set_hidden_dates` | `移出列表` | `cmd:隐藏日期` |
  | `set_pinned_dates` | `切换快捷显示` | `cmd:快捷显示` |
  | `set_announcements` | `编辑公告` | `cmd:公告` |
  | `add_announcement` | `新增公告` | `cmd:新增公告` |
  | `set_weekly_schedule` | `修改周课表模版` | `cmd:周模版` |
  | `update_settings` | `修改设置` | `cmd:设置` |
  | `upload_badge` / `delete_badge` | `上传班徽` / `删除班徽` | `cmd:上传班徽` / `cmd:删除班徽` |
- **`view_date` 的来源**：`Inner.view_date` 只在 `apply_history_step`（undo/redo）里被写，前端**没有**任何命令能设置它。`set_day` 用「`view_date` 为空 ? 入参 date : `view_date`」，`record_history` 用 `view_date` 原样。因此第一次 `set_day` 记的页面日是那天的日期；此后只有 undo/redo 会改写它（见 6.2 的风险）。

### 5.4 `state-changed` 快照（`StatePayload`，main.rs:151-180）

| 字段 | 含义与算法 |
|---|---|
| `data` | 当前 `AppData` 的克隆（前端唯一数据源） |
| `data_dir` | 当前数据目录的显示字符串 |
| `real_today` | `schedule::today_string()` = **系统日期**；设置页天数列表的固定锚点（删一天不会让上一天冒出来） |
| `today` | `effective_today(data, today_string())` = 后端定义的「今天」（休息日/被移出列表的今天就退到上一上学日） |
| `next_school_day` | `next_school_day(data, today_string())` = **系统日期**的下一个上学日（前端天数导航用它代替「明天」；被移出列表的日期不算） |
| `launch` | `launch_view(data, today_string(), Local::now().time())`（完整判定链见 2.8） |
| `notice` | `Mutex<Option<String>>` 里 `take()` 出来的一次性提示（恢复/外部重载/外部读失败） |
| `badge_uploaded` | `store::has_badge(data_dir)`（通用版据此决定能否开「放映显示班徽」） |

- 三个日期字段在**每次** `state-changed` 时重算，前端只读不重算。
- 实现细节：`payload()` 里 `today` 用的是**第二次** `today_string()` 调用（`launch` / `next_school_day` 用第一次），跨午夜的一瞬间理论上可能不一致（见 6.2）。

### 5.5 dirty 标记、防抖自动保存、自动出图、归档（后台线程）

`mark_changed(app, shared)`：`dirty = true`、`image_dirty = true`、`last_edit = now`，然后广播 `state-changed`。
`save_if_dirty(shared)`：`dirty == false` → 返回 `false`；否则 `store::save(dir, data)` 成功 → `dirty = false`、`disk_stamp = disk_stamp(dir)`（**只有这条路径刷新 disk_stamp**）；失败 → 返回 `false`、保持 `dirty`（**不提示、不写日志**）。

`background_loop`（每 **500ms** 一轮，main.rs:933-955）：
1. **自动保存**：`dirty && last_edit.elapsed() >= 1s` → `save_if_dirty`。即「最后一次改动后约 1.0~1.5 秒」落盘。
2. **自动出图触发**：`image_dirty && !presenting` → `interval = max(settings.image_interval_secs, 10)` 秒；`last_image.elapsed() >= interval` → `last_image = now` 并把 `auto-image-request` 发给前端（前端渲染「今天·全部」回传字节 → `export_image` 写文件）。`last_image` 在进程启动时初始化为当时，所以第一张自动图至少在启动一个间隔之后。
3. **归档**：`last_archive.elapsed() >= ARCHIVE_CHECK_SECS(300s)` → `archive_images()`（见 3.8）。`last_archive` 初始为线程启动时刻。

另有两个后台/异步行为：
- `serve_activate` 线程：单实例 IPC（5.6）。
- 3 秒兜底 show 线程：`frontend_ready` 没按时到达也能让窗口出现。

### 5.6 单实例驻留、唤出与「外部改动」重载判定

**IPC**：`IPC_ADDR = "127.0.0.1:49731"`（回环 TCP，只用标准库）。
- 启动时 `TcpListener::bind(IPC_ADDR)`：
  - 成功 → 成为驻留实例，`setup` 里起 `serve_activate(listener, app)` 线程，循环 `accept`：
    读最多 8 字节（读超时 800ms），若以 `"SHOW"` 开头 → 回写 `"OK\n"` → `run_on_main_thread(show_main)`。
  - 失败 → `activate_running_instance()`：`connect_timeout(600ms)` → 写 `"SHOW\n"` → 读超时 800ms、读最多 8 字节，若以 `"OK"` 开头 → 返回 `true` → **本次启动直接 `return`（不建 WebView、不加载数据）**。
  - 若 bind 失败且 `activate_running_instance()` 也失败（例如端口被别的程序占用）→ **照常启动第二套实例**（`listener = None`，不起服务线程）。

**唤出窗口 `show_main(app)`**（main.rs:235-243）：
1. `reload_if_externally_changed(app, shared)`；
2. `unminimize` → `show` → `set_focus`；
3. emit `activated`（前端收到后强制按当前日期/时间重新定位，等价 Ctrl+T）。

**`reload_if_externally_changed`**（main.rs:211-233）：
1. `on_disk = disk_stamp(dir)` = `(len(data.json), mtime)`；文件不存在 → `None`。
2. `inner.dirty == true` → 直接返回（不重载，避免冲掉未保存的输入）。
3. `on_disk == inner.disk_stamp` → 直接返回（没有变化）。
4. `store::load(dir)` 成功 → `data = 新数据`、`dirty = false`、`image_dirty = true`、`disk_stamp` 刷新、notice = `"数据文件已在外部修改，已重新载入"`。
5. 失败 → **保留内存数据**、notice = `"数据文件现在读不出来（<e>），仍用当前数据"`。
6. 设 notice、广播 `state-changed`。
- 注意：重载**不重置撤销栈、不改 `view_date`**（见 6.2）。

### 5.7 关窗 vs 完全退出

- **关窗（X / Alt+F4）= 隐藏**：`on_window_event` 收到 `CloseRequested` → 读 `quitting` → `save_if_dirty(&shared)`（不管怎样都先落盘）→ 若 `!quitting`：`api.prevent_close()` + `window.hide()`；进程留在内存里（热启动秒开）。
- **完全退出**（`Ctrl+Q` / 工具栏「退出」）：前端先 `flushDraft()` → `generateImage(false)`（重出今天的图）→ 调 `quit_app()`：
  1. `inner.quitting = true`；
  2. `save_if_dirty(&shared)`；
  3. `app.exit(0)` → 此时若还有 `CloseRequested`，因为 `quitting == true` 不再 `prevent_close`，进程真正结束。
- `set_presenting(on)`：`presenting = on` + `set_fullscreen(on)`；放映时按本机设置决定常亮（`SetThreadExecutionState`）与吞键（低级键盘钩子），关掉即恢复。
- 冷启动窗口参数：label `"main"`、标题「每日作业」、还原尺寸 1280×800、最小 1024×700、`maximized(true)`、`visible(false)`、底色 `#eef2f9`、`disable_drag_drop_handler`、`--proxy-server=direct://`、WebView2 数据目录 = `exe同级/WebView2数据`。

---

## 6. 矛盾与风险清单

### 6.1 旧文档（AGENTS.md）与代码不一致

1. **测试数量过期**：AGENTS.md 写「`cargo test` 41 个单元测试（store 16 + schedule 25）」；代码实际是 `schedule.rs` 25 + `store.rs` 19 = **44 个 `#[test]`**（其中 `regenerate_canonical_fixture` 标 `#[ignore]`，默认跑 43）。补上的 3 个是两版编解码契约用例（`decodes_shared_fixture`、`serialization_matches_canonical_fixture`、`regenerate_canonical_fixture`）。
2. **日志清理清单不符**：AGENTS 说正常启动后清「崩溃日志.txt、启动失败日志.txt、渲染诊断.txt、崩溃转储.dmp」；Rust 的 `clear_stale_logs` 只清 **`崩溃日志.txt`、`启动失败日志.txt`、`崩溃转储.dmp`** 三个，`渲染诊断.txt` 在 Tauri 版代码里**完全不存在**（那是 Win32 版的文件）。
3. **`normalize()` 的时机被说得像「随时」**：AGENTS 说空作业/空学号由「`set_day` 过滤 + `AppData::normalize()` 在重启/重载时清掉」——准确；但要明确 `normalize()` **只在 `store::load` 里跑**，保存路径不跑。所以「过去的 `pinned_dates`、未排序的 `hidden_dates`」在运行期写入磁盘后，要等下次启动加载才会被 `normalize` 收拾。
4. **`image_interval_secs` 有 10 秒暗下限**：AGENTS 只说「距上次 > image_interval_secs」，代码是 `max(值, 10)`；设成 0/1/5 实际都是 10 秒。
5. **`backups` 修剪不只针对 `data_*.json`**：AGENTS 说「修剪为最近 30 份且同一天只留最新」，实现是对 `backups/` 下**所有 `*.json`** 分组（用 `name[5..13]` 当日期键），手工放进去的其它 json 会被一起计入甚至删除。
6. **`next_school_day` 的口径**：AGENTS 说「`next_school_day` 是「今天」的下一个上学日」，而 `payload()` 实现的是 `next_school_day(data, today_string())`（基于**系统日期**而非 `effective_today`）。多数情况下两者结果相同（系统日期不是上学日时，「系统日期的下一个上学日」与「上一上学日的下一个上学日」重合），但在 14 天上限边界上可能不同；重写时建议**统一用 `effective_today` 出发**并补一条回归用例固化所选语义。**待确认**（需产品确认要哪一种）。
7. **「早晨分界线是任意 HH:MM 输入」**：解析失败静默回退 08:00，UI/文档都没有提示（设置页输入非法值不会有反馈）。

### 6.2 代码里可疑 / 待确认的行为（建议在 C++ 侧显式决策）

1. **`Inner.view_date` 没有任何命令写入**（只有 undo/redo 会写）。`record_history`（公告、天数、设置、班徽…）记下的页面日是 `view_date`——初始为 `""`，之后可能永远是「上一次撤销还原到的日期」。→ 撤销这些操作时 `UndoResult.date` 可能为空串或过期日期，前端会被切到错误的一天。C++ 侧应改为「由前端在每次变更时显式带上当前页面日」或统一由 `set_day` 之外的一个 `set_view_date` 维护。
2. **`save_now` 不刷新 `disk_stamp`**（只有 `save_if_dirty` 刷新）。Ctrl+S 之后唤出窗口（`show_main` → `reload_if_externally_changed`）会看到 `on_disk != inner.disk_stamp` → 误判「数据文件已在外部修改」→ 重新载入并弹一次无意义的 toast（数据内容其实一样）。修法：`save_now` 成功后同样刷新 `disk_stamp`。
3. **`reload_if_externally_changed` 重载后不重置撤销栈**：`undo` 里的 `HistStep.data` 还是旧数据，撤销一次会把「外部改过的新数据」整份覆盖回旧快照（可能丢掉外部改动）。同时 `view_date` 保持不变。C++ 侧建议重载时 `history.reset()` / 断开合并窗口。
4. **`History::reset()` 是死代码**（从未调用）；`break_history_coalescing` 是唯一被前端使用的「断点」入口。
5. **`override = Some("")` 被视为「调课」**：`effective_schedule` 里 `Some(v) if v != base_name` → `name = ""`、`changed = true`，会产生一个「空名字的红色节次」。数据文件若被手改成 `[""]`，或 UI 某条路径写入了空串调课，就会留下这种节次；`normalize()` 不清理。重写时要么保持同样判定（契约），要么在载入时把 `Some("")` 归一为 `None`（会改变字节契约，需同步改 native）。
6. **只写「有内容的」空条目，但空 `Day` 会制造上学日**：`set_day` 会给**任何** `date`（含周末、含 `pinned_dates` 里的周六）插入 `Day` 条目；而 `is_school_day` 只要求 `days` 有键。→ 在周末页面上编辑一次（或设置页「新增日期」/调休下拉建空白天）就会把这个周末变成「调休上学日」，进而可能被 `prev_school_day` / `next_school_day` / `effective_today` 选中。这是**真实领域后果**，AGENTS 只描述了「仅为调休而建的空白天」这一种预期用途。C++ 侧需要明确：周末的编辑/空白天是否应该建条目（若要保留语义，就得靠 UI 约束；领域层目前无保护）。
7. **`add_announcement("")` 允许空公告**，`normalize()` 不清理 `announcements`，`announcement_visible` 只看 `done` → 一条空的未完成公告会永久占住放映画面的一行。建议在命令层拒绝空文本（会与现状不同，需产品确认）。
8. **`payload()` 里 `today` 用了第二次 `today_string()`**：跨午夜的一瞬间，`launch` / `next_school_day` / `today` 三个字段可能基于两个不同的「今天」。建议一次取定。
9. **`save` 失败静默**：自动保存路径没有日志、没有 toast；`save_if_dirty` 的返回值除 `quit_app` / 关窗路径外没人看。磁盘满/权限不足时用户完全无感（重启才发现丢改动）。
10. **原子性未覆盖目录项**：`save` 只对 tmp 文件 `sync_all`，rename 后没有 fsync 父目录；备份 `fs::copy` 也没有 fsync。断电场景下「rename 已返回但目录项未落盘」仍可能丢主文件（此时 backup 还在，能恢复，前提是 backup 本身也落了盘）。**待确认**：新实现是否要加强（fsync 目录 / 用 `ReplaceFile` 等）。
11. **`write_image` 非原子**，且自动图与手动图**同名时互相覆盖**（同一天先自动后手动 → 手动版胜出；反之亦然）。AGENTS 的「两张图并存」只在「自动图 = 今天 / 手动图 = 其他天」时成立。
12. **`relocate_images` 在「文件名日期 == mtime 本地日期」时无条件立即搬**（不看年龄）。正常情况下今天那张总在保留集里所以不会误搬；但一旦保留集算错（例如系统时间被改、`effective_today` 与 `real_today` 的组合没覆盖到某张图），刚出的图会被 300 秒后的归档线程立刻搬走，表现为「刚出的图不见了」。另外 `now.duration_since(mtime)` 失败（now 早于 mtime，时钟回拨）时该图**永远不搬**（每次检查都跳过）。
13. **`migrate_data_dir` 的「同一目录」判定太弱**：只比 `PathBuf` 字面相等（native 用 `fs::equivalent`）。带尾随斜杠、相对路径、符号链接、或 `new_dir` 位于 `current` 内部（如 `current/子目录`）时都不会被拒绝——后一种情况 `copy_recursive` 会在复制过程中把新建的目标目录也扫进去，可能无限递归/填满磁盘。C++ 侧应做 `equivalent()` + 「目标不在源内部」双重校验。
14. **`step_school_day` 的 14 天上限**会在超长假期时返回一个非上学日（静默）。重写要么保留（并补用例），要么改成「最多找 N 天、找不到就返回起点」——必须显式决策，否则行为漂移。
15. **`get_view_model` / `set_day` / `add_days` 都不校验日期参数**：非法日期会得到「周一模版 + 以系统今天为起点的 next_school_day + 可能新建一个奇怪的 `days` 键」。`get_week(monday)` 非法时会返回 7 行同一天。C++ 侧建议在命令边界统一校验并返回错误。
16. **`prune_backups` 的 `name.get(5..13)` 用字节切片**：非 ASCII 文件名（本机设置文件名等混进 `backups/`）可能切在字符边界外 → `None` → 归入 `""` 组，行为不确定。
17. **`open_data_dir` 硬编码 `explorer`**：Windows 专用，macOS/Linux 会失败。跨平台重写必须抽象（`QDesktopServices::openUrl` / `xdg-open` / `open`）。
18. **`exe_dir()` 在 macOS `.app` 里指向 `Contents/MacOS`**：「exe 同级」这套目录约定（数据目录、出图、WebView 缓存、日志）在新平台上需要重新定义；`data.json` 的**兼容性要求**说的是文件格式，不是路径。**待确认**：新版的默认数据目录用什么位置（若沿用「可执行文件同级」，`.app` 场景会很别扭）。

### 6.3 与 native（C++/Win32）版的差异（重写时的对齐依据）

1. **`data.json` 解析严格度**：Rust 对已知字段类型不符是「整份失败 → 走备份恢复」；native `model.cpp` 是逐字段校验并给出中文错误信息（`version 应为数字` 等），是否也会整份拒绝需在重构时二选一并固化。**待确认**。
2. **`version` 的边界值**：native 只要求是数字、负数钳 `0`；Rust `u32` 遇到负数**整份解析失败**。`version` 目前无任何语义，建议统一为「数字、不做迁移、原值回写」。
3. **本机设置的类型容错**：native 对 `keep_awake`/`lock_switching`/`screen_device`/`appearance`/`sidebar_collapsed` 逐个判类型（非法就忽略该字段）；Rust 是整份解析失败并回退到 legacy/默认。旧文件里 `appearance` 为空串时，native 写回会归一成 `system`，Rust 原样透传。
4. **`migrate_data_dir` 的同一目录判定**：native 用 `fs::equivalent || current == new_dir`，Rust 只比 `PathBuf`（见 6.2 第 13 条）。
5. **常量一致（已核对）**：`kLimit = 60`、`kCoalesceMs = 2500`、`kMaxBackups = 30`、`kImageMaxAgeSecs = 24h`，与 Rust 侧一致；归档的「文件名日期 == mtime 日期则不等 24h」规则也一致。
6. **契约样本**：`fixtures/data.canonical.json` 是两版**逐字节**的黄金文件，但它的末尾 `\n` 只在测试里附加（Rust `canonical_text() + "\n"`、`store::save` 不写尾换行）。C++ 侧复现时要注意：**契约比对含尾换行，真实 `data.json` 不含**。建议在 C++ 测试里照抄这条口径（`pretty + "\n"` 与 fixture 比，写盘时 `pretty`）。
