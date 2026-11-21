# AutoClickerQt

Windows 自动连点器 —— **C++17 / Qt6** 版本。

从 [AutoClicker](https://github.com/YOUXICHUNMI/AutoClicker)（Python + PyQt6 版）完整移植而来，
功能一一对应，行为以原版为准。

作者做这个工具的缘由（原版 README 原文）：

> 在 CookieClickWeb 端时，不喜欢鼠大侠而且想要后台挂机的连点器，突发奇想让 AI 随便写了一个。

---

## 它和别的连点器有什么不同

**后台连点**。不是把鼠标抢过来点，而是直接往目标窗口投递 `PostMessage` 消息，
所以目标窗口**可以最小化、可以不在前台**，照样能被点。

这是它存在的唯一理由，也是移植时任何简化都不能牺牲的部分。

---

## 功能

- **两种点击模式**
  - *前台模式*（`SendInput`）：合成到系统输入队列，全局生效，会抢焦点
  - *后台模式*（`PostMessage`）：直投目标窗口，不抢焦点，可最小化
- **多种动作**：鼠标点击（左/右/中/X1/X2）、键盘按键、组合键
- **单击 / 长按**；长按时长设为 0 表示「一直按」直到停止
- **多配置**：新建 / 重命名 / 复制 / 删除，各配置独立保存
- **目标窗口选择**：窗口列表、进程选择器、关键字搜索、鼠标拾取坐标
- **全局热键**：默认 F6 启动 / F7 停止，后台生效
- **托盘**：可最小化到托盘
- **深浅主题**：Catppuccin Mocha / Latte，可跟随系统
- **日志**：按日期分文件，工作线程崩溃也会被记录

---

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | **Windows 10 / 11**（连点依赖 Win32 API，其它平台无法工作） |
| Qt | **Qt 6.5+**（Core / Gui / Widgets） |
| 编译器 | 支持 C++17：MSVC 2019+ 或 MinGW 11+ |
| CMake | 3.16+ |
| 构建工具 | Ninja（推荐）或 Make / MSBuild |

### 安装 Qt

1. 下载 [Qt Online Installer](https://www.qt.io/download-qt-installer)
2. 勾选 **Qt 6.x → MSVC 2019 64-bit**（或 MinGW 64-bit，与你的编译器一致）
3. 记下安装路径，例如 `C:\Qt\6.8.0\msvc2019_64`

> **避坑**：Qt 的 MSVC 版本必须和编译器匹配。用 MinGW 编译却装了 MSVC 版的 Qt，
> 会在链接阶段报一堆 `undefined reference`。

---

## 构建

```powershell
# 1. 指向 Qt6 安装目录（改成你自己的路径）
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.0\msvc2019_64"

# 2. 配置（首次或改了 CMakeLists 后）
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build build/release

# 4. 运行
.\build\release\AutoClickerQt.exe
```

用 CLion：直接打开项目目录，在 *Settings → CMake* 里把
`CMAKE_PREFIX_PATH` 设成 Qt6 路径即可。

### 用 CMake Presets（更省事）

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.0\msvc2019_64"
cmake --preset release
cmake --build --preset release
```

### 常见构建错误

| 报错 | 原因与解法 |
| --- | --- |
| `Could not find a package configuration file provided by "Qt6"` | `CMAKE_PREFIX_PATH` 没设或设错。指向 `.../msvc2019_64` 这一层，不是 `C:\Qt` |
| `Qt6_DIR-NOTFOUND` | 旧 CMakeCache 残留。**删掉 `build/` 和 `cmake-build-*` 重新配置** |
| 链接期大量 `undefined reference to ...` | Qt 的编译器版本与你的编译器不一致（MSVC 版 Qt + MinGW 编译器） |
| `LNK2038: mismatch detected for 'RuntimeLibrary'` | Debug/Release 混用。清空 build 目录重来 |
| 运行时提示缺 `Qt6Core.dll` | 用 `windeployqt` 拷贝依赖（见下方打包），或把 Qt 的 `bin` 加进 `PATH` |

---

## 打包发布

三个脚本做同一件事，按你的 shell 环境挑一个。**它们的判定结论必须一致**——
同一份 `dist/` 分别跑一遍，退出码和结论行都要相同。

打包有**两种产物形态**，默认是绿色目录，加一个开关就变成单文件 exe：

| 形态 | 开关 | 产出 | 适合 |
| --- | --- | --- | --- |
| 绿色目录（默认） | 不加 | `dist\AutoClickerQt.exe` + 21 个 Qt DLL + `*_win64.zip`（约 39 MB） | 自己用，启动最快 |
| 单文件 exe | `-SingleExe` / `--single-exe` | `dist\AutoClickerQt.exe` 一个文件（约 17 MB） | 拷给别人，DLL 已内嵌 |

> 单文件模式用 [Enigma Virtual Box](https://enigmaprotector.com/en/downloads.html) 做
> 文件虚拟化：DLL 不落盘，运行时由 loader 在内存里模拟，所以真的是「一个 exe 拷哪
> 都能跑」。它需要单独安装（免费），脚本会在 `E:\Tools\EnigmaVB`、
> `C:\Program Files\Enigma Virtual Box` 和 `ENIGMAVB_DIR` 环境变量里找；
> 找不到就报错并提示下载地址，不会静默降级。
>
> 装了之后 `dist/` 会被清理成只剩那一个 exe（`config/` 和 `log/` 保留，那是运行期
> 数据）。想换回绿色目录，去掉开关重跑一遍即可。

| 脚本 | 运行环境 | 产出 |
| --- | --- | --- |
| `script\package.ps1` | Windows PowerShell 5.1 / 7+ | `dist\AutoClickerQt.exe` + Qt 依赖 + `*_win64.zip` |
| `script\package.cmd` | Windows cmd（薄包装，内部转调 ps1） | 同上 |
| `script/package.sh` | MSYS2 / Git Bash（Windows）| 同上；Linux / macOS 上只能出 `*_src.tar.gz` |

```powershell
# --- Windows ---
.\script\package.ps1                 # 编译 + windeployqt + 压缩（绿色目录）
.\script\package.ps1 -SingleExe      # 打成单个 exe（需 Enigma Virtual Box）
.\script\package.ps1 -SingleExe -SkipZip          # 单文件，且不再压缩发布包
.\script\package.ps1 -EnigmaVBDir "D:\Tools\EVB"  # 手动指定 EVB 安装目录
.\script\package.ps1 -SkipZip        # 只编译并铺好 dist，不压缩
.\script\package.ps1 -Version 1.2.0  # 指定版本号
.\script\package.ps1 -CheckOnly      # 只检测（exit 0=最新 / 2=过期）
.\script\package.ps1 -Force          # 产物最新也强制重打

# 用 cmd 也一样，参数原样透传
script\package.cmd -SingleExe
script\package.cmd -CheckOnly
```

```bash
# --- MSYS2 / Git Bash（Windows，能出 exe）---
./script/package.sh                  # 编译 + 收集依赖，产出 dist/AutoClickerQt.exe
./script/package.sh --single-exe     # 打成单个 exe（需 Enigma Virtual Box）
./script/package.sh --no-exe         # 只编译，不收集依赖
./script/package.sh --with-source    # 额外打一份源码归档
./script/package.sh --check-only

# --- Linux / macOS（出不了可运行的程序）---
./script/package.sh                  # 只打源码归档
```

绿色目录形态的产物在 `dist/`：

```
dist/
├── AutoClickerQt.exe            可执行文件
├── Qt6Core.dll 等运行时 DLL      由 windeployqt 拷贝
├── platforms/qwindows.dll
├── imageformats/                 GIF 播放需要 qgif.dll
├── AutoClickerQt_v1.0.0_<日期>_win64.zip   完整绿色包
└── AutoClickerQt_v1.0.0_<日期>_src.tar.gz  源码包（--with-source 才有）
```

单文件形态只有两样：

```
dist/
├── AutoClickerQt.exe            单文件，17 MB，DLL 已内嵌
└── AutoClickerQt_v1.0.0_<日期>_win64.zip   发布包
```

> **`script/package.ps1` 和 `package.sh` 都会自动避开 Cygwin / MSYS 版的 cmake**。
> 这类 cmake 会把 Windows 路径改写成 `/cygdrive/d/...` 传给 `-S`/`-B`，报
> 「source directory does not exist」。两者都优先用 Qt 自带的
> `E:\Qt\Tools\CMake_64\bin\cmake.exe`，并在没设 `CMAKE_PREFIX_PATH` 时自动探测
> `E:\Qt\6.*\mingw_64`。

### 打包前自检

每个脚本启动时都会先看 `dist/` 里有没有今天的产物、是不是比源码新。
四个容易做错的地方都已处理：

1. **历史归档不算过期**。产物文件名带日期戳，换个日期重打一次，旧文件会一直
   显示「过期」。现在名字里日期戳不是今天的，标 `[归档]` 直接跳过判定。
2. **源码归档默认不参与判定**。`*_src.tar.gz` / `*_src.zip` 只有显式
   `--with-source` 才产出，默认标 `[跳过]`，否则自检每次都说「该重打了」，
   然后你就再也不看它了。
3. **今天一个产物都没有也算需要打包**，哪怕 `dist/` 里躺着一堆历史包。
4. **单文件模式与绿色目录模式互相认为对方是「最新」**。两种形态产出的都是
   `dist/AutoClickerQt.exe`，所以从一种切到另一种时，脚本会照常判「已是最新」
   并跳过 —— 这是有意的：**切换形态要带 `-Force`**，否则它不会重新部署。

> 之前还按「谁产出的」跳过 `exe` / `_win64.zip`，后来取消了这个规则：`ps1` 和
> Windows 下的 `sh` 产出的是同一份东西，谁跑都能重建，没必要区分来源。
>
> 另外 `Invoke-Deploy` 复制完 exe 会把它的 mtime 设成当前时间。`Copy-Item` /
> `cp` 保留的是编译时的 mtime，不这样处理，刚部署完自检就会判「过期」。

三个脚本的实现各写各的（PowerShell / bash），所以改判定规则时**三个都要改，
并且跑一遍对比退出码**。这是我在上一版项目里真踩过的坑。

---

## 数据目录

配置与日志写在**数据目录**下，不在源码目录里：

```
<数据目录>/
├── config/config.json     配置
└── log/app_YYYY-MM-DD.log 日志（按日期分文件）
```

数据目录的判定优先级：

1. 环境变量 `AUTOCLICKER_DATA_DIR`（有就用它）
2. 可执行文件在**构建树**里（路径含 `build/`、`build-*`、`cmake-build-*`、`out/`）
   → 回落到项目根，方便开发时看配置
3. 否则 = **可执行文件所在目录**（发布后就是 exe 同级，绿色软件行为）

> 判定 2 只认构建目录前缀，**不能靠「向上找到 CMakeLists.txt」**。`dist/` 就在
> 源码根下一层，向上找一定会命中 — 结果发行版把 config/log 写进了源码树。
> 这个 bug 是实机跑 dist 版时发现的。

这样源码运行和 exe 运行不会互相污染配置。

---

## 使用说明

### 前台模式

1. 「目标窗口」→ 模式选 *前台模式 (SendInput)*
2. 「点击动作序列」添加动作
3. 点「开始连点」，或按 F6

鼠标会被程序接管，直到停止（F7）。

### 后台模式（重点）

1. 模式选 *后台模式 (PostMessage)*
2. 「目标窗口」下拉里选目标；或点「📋 进程」从进程列表选；或直接输入关键字回车
3. 设置点击坐标：可以直接填百分比，也可以点「🎯 拾取坐标」然后用鼠标点一下目标位置
4. 点「开始连点」，或按 F6

目标窗口**不需要在前台**，可以最小化。此时点击是直接投递消息，
所以不会打断你正在做的其它事情。

坐标是**相对客户区的百分比**（0–100%），不是屏幕绝对坐标——
这样目标窗口移动、改变大小时不用重新设置。

### 操作细节

- 表格里选中一行后，用「上移 / 下移」调整动作顺序
- 长按模式下，时长设成 `0` 表示「一直按住」，直到你停止连点
- 热键输入框点进去、直接按下想用的键即可（支持 Ctrl/Alt/Shift/Win 组合）
- 点「应用热键」才生效

---

## 架构

```
AutoClickerQt/
├── main.cpp                    程序入口：日志 → 主题 → 主窗口
├── core/                       纯逻辑，不依赖 UI
│   ├── click_engine.{h,cpp}    Win32 输入模拟 + 窗口枚举 + 循环调度
│   └── hotkey_listener.{h,cpp} 全局热键（RegisterHotKey）
├── ui/
│   ├── main_window.{h,cpp}     主窗口
│   ├── action_editor.{h,cpp}   动作编辑对话框 + KeyCaptureEdit
│   ├── process_picker.{h,cpp}  进程选择对话框
│   └── styles.{h,cpp}          Catppuccin 主题 QSS
├── utils/
│   ├── app_paths.{h,cpp}       数据目录与资源路径
│   ├── config_manager.{h,cpp}  配置读写
│   └── logger.{h,cpp}          日志与全局异常兜底
├── resources/                  qrc + Ralsei 图标
├── script/                     打包脚本（ps1 / sh / cmd）
└── MEMORY.md                   项目记忆：决策、踩坑、移植进度
```

### 为什么这么分层

- `core/` 不 include 任何 UI 头文件。连点引擎可以脱离界面单测，
  也能被其它前端复用。
- `utils/` 只有路径、配置、日志三件事，没有业务逻辑。
- `ui/` 只负责采集参数和显示状态，真正的时序控制全在 `core/click_engine` 的独立线程里。
- 引擎通过回调通知 UI，**回调里用 `QMetaObject::invokeMethod` 排队到 UI 线程**，
  不直接碰控件——跨线程动控件是 Qt 最经典的崩溃原因。

---

## 几个实现上的取舍

### 为什么用 Win32 直调，不用 Qt 的输入模拟

Qt 的事件机制出不了本进程。后台模式必须往**别的进程**投递消息，只能走
`PostMessage`。另外间隔可低到 1ms，Qt 事件循环的调度粒度不够。

### 为什么后台点击前要发 WM_MOUSEMOVE

只发 `WM_LBUTTONDOWN` / `WM_LBUTTONUP`，很多程序（网页、游戏 UI）不认——
它们的 hover 状态没更新过，就当作没发生。所以点击前先补一条 `WM_MOUSEMOVE`。

### 为什么后台键盘**不**发 WM_CHAR

实测过：只发 `WM_KEYDOWN` + `WM_KEYUP`，目标消息循环的 `TranslateMessage`
会自动派生 `WM_CHAR`，字符**已经**产生了。手动再补一条，字符会翻倍。

| 发送的消息 | 输入框实际得到 |
| --- | --- |
| KEYDOWN + KEYUP | `aa` |
| 只发 CHAR | `A` |
| KEYDOWN + CHAR + KEYUP | `Aaa`（重复了） |

### 为什么全局热键不用键盘钩子

原版用 Python 的 `keyboard` 库，需要管理员权限才能工作。普通用户双击打开，
热键静默失效、也没有任何提示。改用 Win32 `RegisterHotKey` 后普通权限即可，
失败还能拿到明确错误码（比如「该组合已被占用」）。

### 为什么配置里不存窗口句柄

`HWND` 每次开机都会变。把它写进 `config.json`，下次启动就会拿到一个
指向别的窗口（或已失效）的陈旧句柄。所以只持久化窗口**标题**，启动时按标题重查。

---

## 日志

`<数据目录>/log/app_YYYY-MM-DD.log`，按天分文件，UTF-8 编码。

- 主线程未捕获的异常由 `std::set_terminate` 兜底记录
- 引擎线程内部有 try/catch，异常写日志并停止循环，不会静默死掉

---

## 许可

个人项目，未指定开源协议。
