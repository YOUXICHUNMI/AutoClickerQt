# AutoClicker 项目记忆

本文件记录本项目的目标、结构、决策与移植进度，供后续会话（人或 AI）快速接上下文。
**改动代码前先读这里，改完结构性内容后回来更新。**

---

## 1. 这是什么

`AutoClicker` 是 [AutoClicker](file:///E:/APP/Clicker/AutoClicker) 的 **C++ / Qt6 重写版**。

原始版本是 Python + PyQt6 写的 Windows 自动连点器，作者自述动机（原文）：

> 在 CookieClickWeb 端时，不喜欢鼠大侠而且想要后台挂机的连点器，突发奇想让 AI 随便写了一个。

核心能力是**后台连点**：不是把鼠标抢过来点，而是直接往目标窗口投递 `PostMessage`，
所以窗口可以最小化、可以不在前台，照样能被点。这是它区别于普通连点器的唯一理由，
移植时任何简化都不能牺牲这一点。

## 2. 两个仓库的关系

| 项目 | 路径 | 技术栈 | 状态 |
| --- | --- | --- | --- |
| AutoClicker（原版） | `E:\APP\Clicker\AutoClicker` | Python 3.12 + PyQt6 | 功能完整，已打包出 exe |
| **AutoClicker（本仓库）** | `D:\xxx\Documents\Computer\MyProject\AutoClicker` | C++17 + Qt6 | 移植中 |

原版是**功能基准**，不是要维护的依赖：移植以它的行为为准，读它的源码确认语义，
但本仓库不引用它的任何代码，不共享配置目录。

原版规模（供对照）：10 个模块约 3490 行。
`main.py` 92 / `core/click_engine.py` 913 / `core/hotkey_listener.py` 149 /
`ui/main_window.py` 1180 / `ui/action_editor.py` 309 / `ui/process_picker.py` 148 /
`ui/styles.py` 380 / `utils/app_paths.py` 68 / `utils/config_manager.py` 127 / `utils/logger.py` 127。

## 3. 目标结构

```
AutoClicker/
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

- [ ] 首选项里的 `start_minimized` / `high_precision` 目前仍未接线
      （原版也是僵尸字段，保持了行为一致；要接就在 `main.cpp` 里加）

### 实机验证（2026-11-21，Qt 6.12.0 装好后）

Qt 装在这个机器的 **`E:\Qt`**（不是 C:\Qt），kit 是 `E:\Qt\6.12.0\mingw_64`，
编译器 `E:\Qt\Tools\mingw1310_64\bin\g++.exe`（GNU 13.1.0），
CMake `E:\Qt\Tools\CMake_64\bin\cmake.exe`（3.30.5），Ninja `E:\Qt\Tools\Ninja\ninja.exe`。
**环境变量 `CMAKE_PREFIX_PATH` / QTDIR / Qt6_DIR 全是空的**，必须显式传。

| 项 | 结果 |
| --- | --- |
| 首次 configure + build | exit 0，15 步全成功，**零错误零警告，一次通过** |
| 产物 | `build/release/AutoClicker.exe` 0.63 MB + 自动拷入的 Qt6*.dll |
| 界面 | 915×967 默认 / 576×499 最小 / 1400×900 宽屏 三档截图验证，控件不重叠不裁切 |
| 打包 | `package.ps1 -Force` exit 0，`dist/` 23 文件 90.9 MB，windeployqt 21 个 DLL |
| dist 版运行 | 进程起得来、窗口标题正常、`CloseMainWindow` 后剩余 0，config/log 都落在 `dist/` |
| 三脚本自检 | ps1 / cmd / sh 对同一份 dist 结论与退出码完全一致（0=最新 / 2=过期） |

### 核心 API 自测（`E:\temp\DeepSeekHarness\workspace\.agent-cache\scripts\qt-selftest\`）

独立 CMake 工程，直接链接项目真实源码（`core/click_engine.cpp`、
`utils/config_manager.cpp`、`utils/app_paths.cpp`、`utils/logger.cpp`），
不是复刻实现 —— 测的就是发行代码本身。

**29 PASS / 0 FAIL**，覆盖：

- 键名映射 12 项（`F6`=0x75 / `F7`=0x76 / `F12`=0x7B / `a`=0x41 / `Z`=0x5A /
  `0`=0x30 / `Num0`=0x60 / `Enter`=0x0D / `Space`=0x20 / `ctrl`=0x11 /
  未知键=0 / `vkToKeyName(0x75)`=="F6"）
- 窗口枚举 **286 个**；进程分组 **100 组，93 组有窗口**；
  **同名多进程组 13 个**（证明按 `(pid, name)` 分组、没按名字去重）
- `isWindow(0)`=false / `windowTitle(0)` 空 / `clientSize(0)` 有效
- 配置读写：至少 1 个 profile 且含动作、默认热键 F6/F7、
  **`config.json` 里没有 `target_hwnd`**
- 引擎生命周期：初始未运行 → start → waitForStop → 未运行
- **后台模式端到端**（`RUN_BG_TEST=1`）：自建 400×200 窗口挂 `QPushButton`，
  `ClickEngine::start(profile, &err, hwnd)` 以 background 模式跑 3 轮，
  主线程持续 `processEvents()` → **按钮实际收到 3 次 `clicked`**。
  这条才真正证明 `PostMessage` 链路（含 `WM_MOUSEMOVE`）是通的。

自测过程中暴露的两个签名认知错误已修正（不是代码 bug，是我记错了 API）：
`waitForStop(int)` 返回 **void**；`clientSize()` 返回 **`std::pair<int,int>`**
（用 `.first` / `.second`，不是 `.width()` / `.height()`）。

### 验证方式（不依赖 Qt，移植阶段做过）

移植时 Qt 还没装，先写了一批**纯 Win32 验证程序**（放在
`E:\temp\DeepSeekHarness\workspace\.agent-cache\scripts\`），
用 `g++ -std=c++17 -O2 x.cpp -luser32 -lgdi32` 单独编译运行，
把 `core/` 里不依赖 Qt 的那部分逻辑原样复刻过去对照：

| 脚本 | 覆盖内容 | 结果 |
| --- | --- | --- |
| `wintest.cpp` | 键名映射、wParam 构造、lParam 打包、窗口枚举、进程分组 | 42/42 PASS |
| `clienttest.cpp` / `clienttest3.cpp` / `clientfinal.cpp` | 客户区尺寸三种算法对比、最小化反推 | PASS 5 / FAIL 0 |
| `hotkeytest.cpp` | 热键解析、`RegisterHotKey`、冲突检测、注销重注册 | 21/21 PASS |

**这是它比"写完直接等编译"更可靠的地方**：窗口枚举、消息构造、客户区计算
这些最容易写错的部分，在没有 Qt 的环境里就已经跑通了。后来 Qt 装好，
编译一次就过也印证了这点。

### 单文件 exe 验证（Enigma Virtual Box）

默认流程（两种形态都产）里的 17 MB 单文件 exe，已在**干净目录
（目录里只有它一个文件）**验证过：窗口标题正确、`config/` 与 `log/` 在 exe
同级生成、日志正常、关闭后进程归零。两个脚本（ps1 / sh）产出的都是这个结果，且两边文件数、大小完全一致（19 个文件 / 17.21 MB）。

验证方式必须是**干净目录**。放在有真实 Qt DLL 的 `dist/` 里跑，看不出虚拟化
有没有生效 —— 它会用真 DLL 兜底，看起来一切正常。

## 7. 环境

| 项目 | 值 |
| --- | --- |
| 编译器 | **g++ 13.1.0（`E:\Qt\Tools\mingw1310_64\bin\g++.exe`）** |
| CMake | **3.30.5（`E:\Qt\Tools\CMake_64\bin\cmake.exe`）** — 打包脚本强制用它，避开 Cygwin 版 |
| 构建工具 | **Ninja 1.12.1（`E:\Qt\Tools\Ninja\ninja.exe`）** |
| Qt | **6.12.0，`E:\Qt\6.12.0\mingw_64`**（环境变量全空，需 `-DCMAKE_PREFIX_PATH`） |
| 平台 | Windows 11 |

> 机器上还有 `D:\cygwin64\bin\cmake.exe`（4.4.3）和 `D:\w64devkit\bin\g++.exe`（14.2.0），
> 但**不能混用**：Cygwin 的 cmake 会把 Windows 路径改写成 `/cygdrive/d/...`，
> 传给 `-S` / `-B` 直接报 source directory 不存在。打包脚本里 `Find-CMake`
> 专门排除了 cygwin / msys / git\usr\bin 路径。

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

### 9.6 三脚本的产物自检（经过两次修正才稳定）

打包脚本启动时会先判断 `dist/` 里有没有最新产物。这块前前后后改了两次：

**第一版：全部一视同仁比 mtime → 永远报"过期"。**
`package.ps1` 打不出 `*.tar.gz`（`package.sh` 的产物），`package.sh` 在非 Windows
打不出 `*.exe`。不属于自己的产物永远比自己"新"不了，结论永远是「该重打了」。
自检一旦永远报警，就等于没有自检 —— 没人会再去看它。

**第二版：按归属标 `[跳过]` → 又矫枉过正。**
Qt 版改完之后，`ps1` 和 Windows 下的 `sh` 产出的是**同一份东西**
（`dist/AutoClicker.exe` + 依赖 + `_win64.zip`），谁跑都能重建。
还按归属跳过就变成了「ps1 说 dist 里只有 sh 的产物、sh 说只有 ps1 的产物」，
两边都说没打过包。

**最终版**：
- 只有 `*_src.tar.gz` / `*_src.zip` 标 `[跳过]`（源码归档只有 `--with-source` 才产）；
- `exe` 与 `_win64.zip` 不区分来源，都参与判定；
- 文件名日期戳不是今天的标 `[归档]`，不参与判定；
- `dist/` 里今天一个产物都没有 → 直接判需要打包。

**还有一个必须处理的细节**：`Copy-Item` / `cp` **保留源文件的 mtime**。
构建产物 `build/release/AutoClicker.exe` 是编译时的时间戳，复制进 `dist/`
之后如果源码又动过（比如刚好改了打包脚本自己），自检会立刻判「过期」，
可实际上刚刚部署完。修法是部署后把 mtime 设成当前时间
（PowerShell `(Get-Item ...).LastWriteTime = Get-Date`，bash `touch`）。

**推论（比修法更重要）**：三个脚本各写各的（PowerShell / bash / cmd 包装），
任何一侧改了规则，另外两侧必须同步改，并且**跑一遍对比退出码**。
当前验收方式：`-CheckOnly` / `--check-only` 对同一份 `dist/` 必须返回相同退出码
和相同的结论行。这是 Python 版项目里真踩过的坑，那次就花了三轮才对上。

### 9.7 Cygwin 的 cmake 会把路径改写成 /cygdrive/

机器上 `PATH` 里的 `cmake` 是 `D:\cygwin64\bin\cmake.exe`（4.4.3）。
把它和 Windows 路径一起用，它会先把 `-S D:\xxx\...` 转成
`/cygdrive/d/xxx/...`，再拼上一次自己算的路径，报：

```
CMake Error: The source directory "/cygdrive/d/xxx/.../D:/xxx/..." does not exist.
```

**修法**：`package.ps1` 里的 `Find-CMake` 按顺序找原生 cmake，并显式排除
`cygwin` / `msys` / `git\usr\bin` 路径：① Qt 安装目录旁的 `Tools\CMake_64`；
② `PATH` 里第一个非 Cygwin/MSYS 的 cmake；③ 常见原生安装位置。
Ninja 同理（`Find-Ninja` 优先用 `E:\Qt\Tools\Ninja\ninja.exe`）。
找到的 ninja 路径还要用 `-DCMAKE_MAKE_PROGRAM=` 显式传给 CMake，
否则 CMake 仍可能去 PATH 里挑到别的。

### 9.8 数据目录判定：不能靠"向上找 CMakeLists.txt"

`dist/` 就位于源码根下一层。最初的 `AppPaths::sourceRoot()` 逻辑是
「从 exe 所在目录逐级向上，找到第一个带 `CMakeLists.txt` 的目录就当源码根」——
这个判定对 `build/release/` 下的开发版是对的，但 **`dist/` 也会命中**，
结果发行版的 config / log 全写进了源码树，而不是 exe 旁边。

发现方式：跑 `dist\AutoClicker.exe`，项目根凭空出现 `log/app_2026-11-21.log`，
而 `dist/log` 不存在。

**修法**：先判断 exe 路径里有没有构建目录段
（`build` / `build-*` / `cmake-build-*` / `out`），有才向上找源码根；
否则直接用 exe 所在目录。

### 9.9 Windows 批处理与 PowerShell 文件的编码

`.ps1` 必须 **UTF-8 带 BOM + CRLF**：没有 BOM 时 PowerShell 5.1 按本地代码页
（中文系统 = GBK）解码，中文注释全变乱码，然后报一堆假语法错误
（`Unexpected token`、`The hash literal was incomplete`），跟真实错误毫无关系。
**`edit` / `write` 工具每次保存都会剥掉 BOM，改完必须重新补**：

```powershell
[System.IO.File]::WriteAllBytes($p, ([byte[]](0xEF,0xBB,0xBF) + $enc.GetBytes($text)))
```

`.cmd` 必须 **GBK + CRLF**，且**不要在文件里中途 `chcp 65001`**：
cmd 是按当前代码页逐字节读批处理的，中途切码页会让后半段文字按旧码页解释而花掉。

`.sh` 必须 **LF**（`CR` 计数为 0），否则 Linux 上会报 `$'\r': command not found`。

`.gitattributes` 里已经按扩展名钉死了这三类，`git add --renormalize` 之后
就不会再混进来。

### 9.10 QDebug 输出 QString 会带引号

`qCInfo(lcX) << QStringLiteral("热键已注册")` 打印出来是
`[hotkey] "热键已注册"` —— QDebug 默认给 QString 加引号。日志里看着很别扭。
修法：`qCInfo(lcX).noquote() << QStringLiteral(...)`。

### 9.11 单文件 exe：Enigma Virtual Box 的 .evb 有四个坑

想产出「一个 exe 拷哪都能跑」，用的是 **Enigma Virtual Box**（EVB，免费）。
它把文件嵌进 exe，运行时由 loader 在内存里虚拟化，**不往磁盘解压**。
坑都在 `.evb` 项目文件里：

1. **根节点名必须是空的** `< > ... < />`，不是 `<Project>`。写别的名字 EVB
   **不报错**，但整个 `<Files>` 列表被静默忽略 —— 打出来的 exe 里一个文件都
   没有，看着「成功」其实白干。
2. **编码必须是 UTF-16 LE + BOM**。UTF-8 一样不报错、一样忽略。
3. **子目录必须一层层写成独立的 `<Type>3</Type>` 节点**。文件的 `<Name>` 写成
   `platforms\qwindows.dll` 是**无效**的 —— 它不会自动建中间目录，qwindows.dll
   被平铺到根目录，Qt 于是找不到平台插件，只弹一个
   「This application failed to start because no Qt platform plugin could be
   initialized.」。**这个最有欺骗性**：进程不会退出（错误框挂着），
   如果只检查「进程是否存活 + 有窗口」，会误判成成功。必须截图或看窗口标题。
4. **`OutputFile` 不能等于 `InputFile`**，会报 `Cannot create file`；而且
   **路径不能含非 ASCII 字符**，`单文件版.exe` 报同样的错，错误信息本身还是乱码。

另外：**不能把主程序自己再嵌一遍**。`dist/` 里除了 `AutoClicker.exe`
什么都不排除的话，等于把入口 exe 当成虚拟文件又嵌一次，运行直接访问违例
（0xC0000005）。`package.sh` 的 `evb_emit_dir` 里按文件名显式排除它。

调试办法：把打出来的 exe 拷到一个**干净目录**（只有它一个文件）再跑。
在有 DLL 兜底的 `dist/` 里跑，永远看不出问题 —— 它用的是真 DLL，虚拟化有没有
生效根本分辨不出来。

EVB CLI：
```
enigmavbconsole.exe project.evb                              # 正常
enigmavbconsole.exe project.evb -input a.exe -output b.exe   # 覆盖输入输出
```

### 9.12 Python 版单文件 exe 在 DSH 工作区里起不来（Low Integrity 陷阱）

**现象**：把 Python 版的 onefile exe 拷到 `E:\temp\DeepSeekHarness\workspace\`
下的任意目录启动，弹出 **`Could not create temporary directory!`** 的错误框；
拷到 `E:\` 根、`D:\`、`%TEMP%`、或同级的 `workspaceX\` 下就完全正常。

**排查过程**（走了不少弯路，记下来免得重来）：

1. 先怀疑路径长度 —— **排除**：`E:\len\d1\d2\...\d12`（45 字符、12 层）全部正常。
2. 先怀疑路径深度 —— **排除**：`E:\temp\DeepSeekHarness\t_dsh`（在 workspace 外、
   同样深度）正常，`E:\temp\DeepSeekHarness\workspace\t_ws`（在 workspace 内）失败。
3. 先怀疑 ACL 的 `Everyone:(CI)(DENY)(DC)` —— **不是它**：在 `E:\temp\t_denytest`
   上手工加同样的 Deny 规则，照样能跑。
4. 先怀疑断开继承 + 显式授权 —— **不是**：`icacls /inheritance:r /grant`
   之后仍然失败。
5. **决定性实验**：把 exe 和 cwd 分开 —— exe 在 workspace 外 + cwd 在 workspace 内
   正常；exe 在 workspace 内 + cwd 在 workspace 外失败 → **问题跟着 exe 文件走，
   不是 cwd，也不是目录本身**。
6. **`icacls` 对比两份拷贝才发现**：从 workspace 拷出来的 exe 多了一条
   `Mandatory Label\Low Mandatory Level:(I)(NW)` —— 它**继承了工作区目录的完整性
   级别**。DSH 沙箱把工作区设成 Low Integrity，Low 进程再往 `%TEMP%`（Medium）
   写东西会被 MIC（Mandatory Integrity Control）拦下来，PyInstaller 建不了
   `_MEIxxxx` 解压目录。
7. **正反验证**：`icacls <exe> /setintegritylevel Medium` → 立即可跑；
   `/setintegritylevel Low` → 立即失败；来回切换四次，结果每次都跟着变。

**结论**：不是产物缺陷，是**文件落在 Low Integrity 目录里继承了低完整性标签**。
绿色目录版（onedir）不需要解压，因此不受影响 —— 这也解释了为什么只有单文件版
在这台机器的工作区里翻车。

**修法**（任选）：

```powershell
# 对单个 exe 提权完整性级别（最省事，只影响这一个文件）
icacls .\AutoClicker.exe /setintegritylevel Medium

# 或者给整个目录提（会继承给以后拷贝进来的文件）
icacls .\某目录 /setintegritylevel Medium

# 或者运行前换个 TEMP（治标不治本，但能验证是不是这个原因）
$env:TEMP = "$env:LOCALAPPDATA\Temp"
```

给用户的分发版本不受影响 —— 用户自己下载到 Downloads / 桌面都是 Medium。

---

## 用 GitHub Actions 自动打包

`.github/workflows/build.yml` 定义了三个 job：

| job | runner | 产出 |
| --- | --- | --- |
| `windows` | `windows-latest` | `dist/*.exe`、`dist/*.zip`（走项目自己的 `package.ps1`） |
| `linux` | `ubuntu-latest` | `AutoClicker_v<ver>_<date>_single_linux64`、`AutoClicker_v<ver>_<date>_linux64.tar.gz` |
| `release` | `ubuntu-latest` | tag 触发时把前两个 job 的产物挂到 GitHub Release |

触发：push 到 `main`、PR、push `v*` tag、手动 `workflow_dispatch`。

设计要点：

- **windows job 不重写打包逻辑**，直接 `./script/package.ps1 -Force -SkipReadme`，
  保证 CI 与本地产出一致；装了 Enigma Virtual Box 才产单文件，装不上就
  `-SkipSingle` 只产绿色目录（`continue-on-error: true`）。
- **Qt 版本固定在 action 里**（6.8.2 / win64_mingw），不用 runner 自带的。
- **linux job 必须装齐 PyQt6 的 xcb 依赖**（`libgl1 libegl1 libxkbcommon-x11-0
  libdbus-1-3 libxcb-*`），否则 PyInstaller 能打出包但运行时找不到平台插件。
- **不能交叉编译**：Windows 产物必须在 windows runner 上构建，Linux 同理。
- 版本号从 tag 取（`${GITHUB_REF_NAME#v}`），非 tag 构建退化成 `0.0.0`。

---

## 更新约定

- 改了**结构**（增删模块、调整分层）→ 更新第 3 节。
- 做了**设计决策**（换方案、绕坑）→ 更新第 4 节，写清「为什么」而不只是「是什么」。
- 推进了**移植进度** → 更新第 6 节的勾选状态。
- 踩了**新坑** → 加进第 9 节，包含现象、根因、解法三要素。
