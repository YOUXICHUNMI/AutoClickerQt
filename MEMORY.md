# AutoClickerQt 项目记忆

本文件记录本项目的目标、结构、决策与移植进度，供后续会话（人或 AI）快速接上下文。
**改动代码前先读这里，改完结构性内容后回来更新。**

---

## 1. 这是什么

`AutoClickerQt` 是 [AutoClicker](file:///E:/APP/Clicker/AutoClicker) 的 **C++ / Qt6 重写版**。

原始版本是 Python + PyQt6 写的 Windows 自动连点器，作者自述动机（原文）：

> 在 CookieClickWeb 端时，不喜欢鼠大侠而且想要后台挂机的连点器，突发奇想让 AI 随便写了一个。

核心能力是**后台连点**：不是把鼠标抢过来点，而是直接往目标窗口投递 `PostMessage`，
所以窗口可以最小化、可以不在前台，照样能被点。这是它区别于普通连点器的唯一理由，
移植时任何简化都不能牺牲这一点。

## 2. 两个仓库的关系

| 项目 | 路径 | 技术栈 | 状态 |
| --- | --- | --- | --- |
| AutoClicker（原版） | `E:\APP\Clicker\AutoClicker` | Python 3.12 + PyQt6 | 功能完整，已打包出 exe |
| **AutoClickerQt（本仓库）** | `D:\xxx\Documents\Computer\MyProject\AutoClickerQt` | C++17 + Qt6 | 移植中 |

原版是**功能基准**，不是要维护的依赖：移植以它的行为为准，读它的源码确认语义，
但本仓库不引用它的任何代码，不共享配置目录。

原版规模（供对照）：10 个模块约 3490 行。
`main.py` 92 / `core/click_engine.py` 913 / `core/hotkey_listener.py` 149 /
`ui/main_window.py` 1180 / `ui/action_editor.py` 309 / `ui/process_picker.py` 148 /
`ui/styles.py` 380 / `utils/app_paths.py` 68 / `utils/config_manager.py` 127 / `utils/logger.py` 127。

## 3. 目标结构

```
AutoClickerQt/
├── CMakeLists.txt
├── main.cpp                  程序入口
├── core/                     连点引擎、热键（纯逻辑，不依赖 UI）
│   ├── click_engine.h/.cpp   Win32 输入模拟 + 循环调度
│   └── hotkey_listener.h/.cpp 全局热键
├── ui/                       界面
│   ├── main_window.h/.cpp
│   ├── action_editor.h/.cpp  动作编辑对话框 + 热键捕获输入框
│   ├── process_picker.h/.cpp 进程选择对话框
│   └── styles.h/.cpp         Catppuccin 主题 QSS
├── utils/
│   ├── app_paths.h/.cpp      数据目录与资源路径解析
│   ├── config_manager.h/.cpp 配置读写（JSON）
│   └── logger.h/.cpp         日志与全局异常兜底
├── resources/
│   ├── resources.qrc
│   └── static/               Ralsei.png / Ralsei_Run.gif / Ralsei.ico
├── MEMORY.md                 本文件
└── CMakePresets.json         构建预设
```

## 4. 关键设计决策

### 4.1 数据目录与资源路径分离

沿用原版 `utils/app_paths.py` 的结论：

- **数据目录**（`config/`、`log/`）优先级：环境变量 `AUTOCLICKER_DATA_DIR` →
  可执行文件所在目录 → 源码根（开发期）。
  这样发布后数据落在 exe 同级，源码运行落在项目根，两边互不干扰。
- **资源文件**（图标、GIF）走 Qt 资源系统（`resources.qrc`），编译进二进制。
  不再像 Python 版那样 `resource_path()` 按磁盘相对路径找——那是为了 PyInstaller
  的 onefile 解压目录，C++ 单文件分发没有这个问题。

### 4.2 连点引擎：Win32 直调，不用 Qt 封装

`SendInput` 与 `PostMessage` 都通过 `<windows.h>` 直接调用，不用 `QTest`、
不用 `QInputEvent` 伪造。原因：

- 后台模式必须能往**别的进程**投递消息，Qt 的事件机制出不了本进程；
- 时序精度要求高（间隔可低到 1ms），Qt 事件循环的调度粒度不够。

**前台模式**用 `SendInput`：合成到系统输入队列，全局生效，会抢焦点。
**后台模式**用 `PostMessage`：直接投给目标 HWND，不抢焦点，可最小化。

### 4.3 后台点击必须发 WM_MOUSEMOVE

这一点在原版上踩过坑，结论要带过来：

- 只发 `WM_LBUTTONDOWN` / `WM_LBUTTONUP`，很多程序（网页、游戏 UI）不认，
  因为它们的 hover 状态没更新；
- 所以点击前先发一条 `WM_MOUSEMOVE`，lParam 带目标坐标。
- **X1/X2 键**的 `wParam` 高字必须填 `XBUTTON1`/`XBUTTON2`，
  但**松开（UP）时高字必须是 0**，这是 Windows 的约定，搞反了 VK 映射会错。

### 4.4 后台键盘不发 WM_CHAR

原版实测过：只发 `WM_KEYDOWN` + `WM_KEYUP` 时，目标消息循环的
`TranslateMessage` 会自动派生 `WM_CHAR`，字符**已经**产生了。
如果手动再补一条 `WM_CHAR`，字符会重复（输入 "a" 得到 "aa"）。

实测对照（原版 `wmchar_probe.py`）：

| 发送的消息 | QLineEdit 得到的内容 |
| --- | --- |
| KEYDOWN + KEYUP | `aa`（已经够，且两个） |
| 只发 CHAR | `A` |
| KEYDOWN + CHAR + KEYUP | `Aaa`（重复） |

**所以 `_post_keyboard` 只发 DOWN/UP，坚决不补 CHAR。**

### 4.5 进程列表按 (PID, 进程名) 去重

原版的 bug：按进程名去重，导致 Chrome / Edge / msedgewebview2 这类多进程应用
只剩第一个 PID 的窗口，其余全丢。修复后按 `(PID, 进程名)` 分组，
无窗口的进程仍按进程名去重（否则 Chrome 会凭空多出几十条噪音）。

效果参考：原版修复后从 143 条涨到约 184 条，其中约 77 条有窗口。

### 4.6 配置里绝不写 HWND

`target_hwnd` 是**运行时缓存**，不是配置项。HWND 每次开机都会变，
写进 `config.json` 后下次启动会拿到一个指向别的窗口（或已失效）的陈旧句柄。

原版为此出过 bug：`_on_start()` 里先 `_auto_save_profile()` 再
`prof["target_hwnd"] = hwnd`，而后面的 `closeEvent()` 又会 save 一次，
于是句柄被间接落盘了。

**本项目的规则**：`ConfigManager::save()` 之前必须剔除 `target_hwnd`，
目标窗口只按 `target_title` 持久化，启动时重新按标题查找。

### 4.7 全局热键用 RegisterHotKey 而不是键盘钩子

原版用 Python 的 `keyboard` 库，README 明确写了「需要管理员权限」——
这是个明确的体验缺陷：普通用户双击打开，热键就是没反应，还没有任何提示。

Qt 版在 Windows 上改用 Win32 `RegisterHotKey`：

- **普通权限即可**，不需要提权；
- 零第三方依赖；
- 失败原因能拿到明确的错误码（比如 1409 = 该组合已被别的程序占用），
  可以直接告诉用户「热键被占用了」，而不是静默失效。

代价是热键会被系统独占，且必须是「修饰键 + 主键」或单个 VK 键的组合。
对本项目够用（默认就是 F6 / F7）。

实现上**不依附主窗口**搭建，而是建一个 `HWND_MESSAGE` 消息专用窗口来收
`WM_HOTKEY`。这样主窗口最小化到托盘、甚至 `hide()` 之后，热键依然有效——
热键失效是最容易让人以为"程序挂了"的问题。

## 5. 配置格式（与原版兼容）

`config/config.json` 结构：

```json
{
  "profiles": [
    {
      "name": "默认配置",
      "actions": [
        {
          "type": "mouse",
          "button": "left",
          "mode": "click",
          "hold_duration": 0.05,
          "delay_after": 0.001
        }
      ],
      "interval": 1,
      "repeat": 0,
      "loop_delay": 0,
      "start_hotkey": "F6",
      "stop_hotkey": "F7",
      "enabled": true,
      "target_mode": "foreground",
      "target_title": "",
      "click_x": 0.5,
      "click_y": 0.5
    }
  ],
  "active_profile": 0,
  "settings": {
    "minimize_to_tray": false,
    "start_minimized": false,
    "high_precision": true,
    "theme": "auto"
  }
}
```

- `interval` 单位毫秒，0 = 最快；`repeat` 0 = 无限；`loop_delay` 是每轮额外的延迟。
- `action.type` 三种：`mouse` / `keyboard` / `combo`。
- `mode` 两种：`click`（单击）/ `hold`（长按）。
  `hold` 时 `hold_duration` 秒；**`hold_duration <= 0` 表示"一直按"，直到停止**。
  这是原版一个容易被忽略的语义，务必保留。
- `click_x` / `click_y` 都是 0–1 的相对比例，相对**客户区**，不是屏幕坐标。
- `theme`：`dark` / `light` / `auto`。

## 6. 移植进度

### 已完成

- [x] 目标目录与目录骨架（core / ui / utils / resources）
- [x] 静态资源拷贝（Ralsei.png / Ralsei_Run.gif / Ralsei.ico）与 `resources.qrc`
- [x] `CMakeLists.txt`（Qt6，WIN32 无控制台，自动拷贝运行时 DLL）
- [x] `utils/app_paths` —— 数据目录与资源路径
- [x] `utils/logger` —— 按日期分文件日志 + 全局异常兜底
- [x] `utils/config_manager` —— 配置读写、默认值逐层合并、不写 HWND
- [x] `core/click_engine` —— Win32 输入模拟、窗口枚举、循环调度
- [x] `core/hotkey_listener` —— 全局热键（Win32 `RegisterHotKey`）
- [x] `ui/styles` —— Catppuccin Mocha / Latte 两套 QSS（与 Python 版逐字节一致）
- [x] `ui/action_editor` —— 动作编辑对话框 + `KeyCaptureEdit` 热键捕获框
- [x] `ui/process_picker` —— 进程选择对话框
- [x] `ui/main_window` —— 主窗口（可滚动内容区 + 常驻底部控制栏）
- [x] `main.cpp` —— 真正启动 `MainWindow`，读取配置主题后套用 QSS
- [x] `README.md` —— 面向使用者的文档
- [x] `script/package.ps1` + `package.sh` + `package.cmd` —— 三脚本配套打包
- [x] `.gitignore` / `.gitattributes`

### 待办

- [ ] **编译验证**（等 Qt6 装好）与真机冒烟
- [ ] 首选项里的 `start_minimized` / `high_precision` 目前仍未接线
      （原版也是僵尸字段，保持了行为一致；要接就在 `main.cpp` 里加）

### 验证方式（不依赖 Qt，已做过）

移植过程中写了一批**纯 Win32 验证程序**（放在
`E:\temp\DeepSeekHarness\workspace\.agent-cache\scripts\`），
用 `g++ -std=c++17 -O2 x.cpp -luser32 -lgdi32` 单独编译运行，
把 `core/` 里不依赖 Qt 的那部分逻辑原样复刻过去对照：

| 脚本 | 覆盖内容 | 结果 |
| --- | --- | --- |
| `wintest.cpp` | 键名映射、wParam 构造、lParam 打包、窗口枚举、进程分组 | 42/42 PASS |
| `clienttest.cpp` / `clienttest3.cpp` / `clientfinal.cpp` | 客户区尺寸三种算法对比、最小化反推 | PASS 5 / FAIL 0 |
| `hotkeytest.cpp` | 热键解析、`RegisterHotKey`、冲突检测、注销重注册 | 21/21 PASS |

**这是它比"写完直接等编译"更可靠的地方**：窗口枚举、消息构造、客户区计算
这些最容易写错的部分，在没有 Qt 的环境里就已经跑通了。


## 7. 环境

| 项目 | 值 |
| --- | --- |
| 编译器 | g++ 14.2.0（`D:\w64devkit\bin\g++.exe`）/ CLion 自带 MinGW |
| CMake | 4.4.3（`D:\cygwin64\bin\cmake.exe`） |
| 构建工具 | Ninja 1.13.2（`D:\Path\ninja\ninja.exe`） |
| Qt | **未安装** |
| 平台 | Windows 11 |

## 8. 界面要点（从原版继承）

原版主窗口的结构是踩过多轮坑才定下来的，直接沿用：

- **内容区可滚动**：所有分组塞进 `QScrollArea`，底部控制栏（开始/停止）常驻在滚动区
  外面。这样窗口缩到最小（560×460）时控件不会互相挤压，只会出现滚动条。
- **分组布局用 QGridLayout 分行**，不用一行塞满。原版"目标窗口"组一开始把
  模式+窗口+刷新+进程全放一行，最小宽度顶到 736px，是整个界面缩不下去的元凶；
  拆成多行后降到 416px。
- **每组最小宽度**（原版实测值，供参考）：配置文件 484 / 动作序列 379 /
  连点参数 440 / 目标窗口 416 / 热键 320。
- **初始窗口尺寸自适应**：按内容高度 + 底部栏/状态栏/标题栏高度估算，
  上限取屏幕可用高度 90%。

## 9. 踩坑记录

### 9.1 Qt 未安装时 CMakeCache 会残留 `Qt6_DIR-NOTFOUND`

CLion 生成过 `cmake-build-debug/` 之后，即使后来装好 Qt，旧缓存仍报找不到。
解决：装好 Qt 后**删掉 `cmake-build-*` 目录重新 configure**，
或用 `-DCMAKE_PREFIX_PATH=<Qt路径>` 覆盖。

### 9.2 中文路径与编码

- 所有 `.cpp` / `.h` 存 UTF-8，MSVC 需加 `/utf-8`（已在 CMakeLists 里加）。
- 源码里的中文字符串字面量用 `QStringLiteral` 包裹，避免隐式转换。
- 日志文件写 UTF-8，不用系统代码页。

### 9.3 原版遗留的已知问题（本版应当规避）

| 问题 | 原版表现 | 本版处理 |
| --- | --- | --- |
| `SendInput` 不检查返回值 | 被 UIPI 拦截时静默失败 | 已检查返回值，失败记日志 |
| `get_client_size` 写死边框补偿（-16/-46） | 高 DPI 下坐标偏移 | 已改为 `AdjustWindowRectEx` 反推，实测精确 |
| 全局热键需管理员权限 | 非管理员时静默失效 | 改用 `RegisterHotKey`，普通权限即可 |
| 工作线程异常不进日志 | 线程静默死掉 | 全局异常兜底 + 线程内 try/catch |

### 9.4 客户区尺寸：三种算法的实测反转（重要）

原版写死 `-16 / -46`。移植时想当然以为 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`
最准，**实测证明相反**，过程值得记下来：

一批真实窗口的客户区对比（真实值 vs 各算法推出值）：

| 算法 | 结果 |
| --- | --- |
| 写死 `-16 / -46` | 纵向差 31–38px |
| `DwmGetWindowAttribute` | 带标题栏窗口误差 ≤2px，**看起来最准** |
| `AdjustWindowRectEx` | 差 24–31px |

乍看 DWM 赢了。但 `clientSize()` 只在**客户区为 0（即最小化）**时才走反推分支，
于是专门测最小化场景（自建 830×620 窗口）：

| 算法 | 最小化窗口推出值 | 真实 814×581 |
| --- | --- | --- |
| 写死 `-16 / -46` | 814×574 | 纵向差 7 |
| **`AdjustWindowRectEx`** | **814×581** | **精确** |
| `DwmGetWindowAttribute` | 684×592 | 横向差 130 |

根因：**DWM 对最小化窗口返回的是屏幕外的占位小矩形**
（实测微信最小化后 `DWM 扩展边框: 146x21 (at -31993,-32000)`，坐标是负的几万）。
而这个分支本来就只在最小化时才会执行 —— 所以 DWM 在这里永远拿不到可用值。

**结论：用 `AdjustWindowRectEx`，不要用 DWM。**

### 9.5 最小化窗口的 `GetClientRect` 会返回垃圾值

修完算法顺序后仍有 1 个失败用例，定位到：**微信最小化后
`GetClientRect` 返回 144×20，而不是 0**。垃圾值被当成"有效客户区"直接返回，
把整条反推分支绕过去了。

**修法：`GetClientRect` 之前先 `!::IsIconic(h)` 判断。**
判断顺序比算法本身更容易错，这类"前提条件"要写在代码注释里。

### 9.6 三脚本的产物自检必须按"产物归属"分流

打包脚本启动时会先判断 `dist/` 里有没有最新产物。第一版把 `dist/` 里所有
`*.exe` / `*.zip` / `*.tar.gz` 一视同仁地跟源码比 mtime，结果两个脚本永远报"过期"：

- `package.ps1` 打不出 `*.tar.gz`（那是 `package.sh` 的产物），
- `package.sh` 在非 Windows 上打不出 `*.exe`（那是 `package.ps1` 的产物）。

不属于自己的产物永远比自己"新"不了，所以结论永远是「该重打了」。
自检一旦永远报警，就等于没有自检 —— 没人会再去看它。

**修法**：先按文件名分流归属，别人的产物标 `[跳过]` 且不参与判定；
再判断日期戳是不是今天的，不是则标 `[归档]`。两道过滤之后才算 mtime。

**推论（比修法更重要）**：三个脚本的判定逻辑是各写各的（PowerShell / bash /
cmd 包装），任何一侧改了规则，另外两侧必须同步改，并且**跑一遍对比退出码**。
当前验收方式：`-CheckOnly` / `--check-only` 对同一份 `dist/` 必须返回相同退出码
和相同的结论行。这是我在 Python 版项目里真踩过的坑，那次就花了三轮才对上。

### 9.7 Windows 批处理与 PowerShell 文件的编码

`.ps1` 必须 **UTF-8 带 BOM + CRLF**：没有 BOM 时 PowerShell 5.1 按本地代码页
（中文系统 = GBK）解码，中文注释全变乱码，然后报一堆假语法错误
（`Unexpected token`、`The hash literal was incomplete`），跟真实错误毫无关系。

`.cmd` 必须 **GBK + CRLF**，且**不要在文件里中途 `chcp 65001`**：
cmd 是按当前代码页逐字节读批处理的，中途切码页会让后半段文字按旧码页解释而花掉。

`.sh` 必须 **LF**（`CR` 计数为 0），否则 Linux 上会报 `$'\r': command not found`。

`.gitattributes` 里已经按扩展名钉死了这三类，`git add --renormalize` 之后
就不会再混进来。

---

## 更新约定

- 改了**结构**（增删模块、调整分层）→ 更新第 3 节。
- 做了**设计决策**（换方案、绕坑）→ 更新第 4 节，写清「为什么」而不只是「是什么」。
- 推进了**移植进度** → 更新第 6 节的勾选状态。
- 踩了**新坑** → 加进第 9 节，包含现象、根因、解法三要素。
