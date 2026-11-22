# AutoClicker

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
.\build\release\AutoClicker.exe
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

### 两种产物形态，默认都产

一次跑完会同时产出下面三份，不需要额外开关：

| 产物 | 说明 | 大小 |
| --- | --- | --- |
| `dist\AutoClicker.exe` | 单文件 exe（DLL 已内嵌，拷一个文件就能跑） | 约 17 MB |
| `dist\AutoClicker_v<版本>_<日期>_single_win64.exe` | 上面那份的带版本号副本，便于归档分发 | 约 17 MB |
| `dist\AutoClicker_v<版本>_<日期>_win64.zip` | 绿色目录包（exe + 21 个 Qt DLL），解压即用、启动更快、便于排障 | 约 16 MB |

只想产其中一种时：

| 想要 | 开关 |
| --- | --- |
| 只产单文件 exe | `-SkipGreen` / `--skip-green` |
| 只产绿色目录 zip | `-SkipSingle` / `--skip-single` |
| 两种都不产（只编译验证） | 传 `-SkipSingle -SkipGreen` |

> 单文件模式用 [Enigma Virtual Box](https://enigmaprotector.com/en/downloads.html) 做
> 文件虚拟化：DLL 不落盘，运行时由 loader 在内存里模拟，所以真的是「一个 exe 拷哪
> 都能跑」。它需要单独安装（免费），脚本会在 `E:\Tools\EnigmaVB`、
> `C:\Program Files\Enigma Virtual Box` 和 `ENIGMAVB_DIR` 环境变量里找；
> 找不到就报错并提示下载地址，不会静默降级。
>
> 单文件里的 `config/` 和 `log/` 仍然落在 exe 同级（那是运行期数据，不在虚拟文件
> 系统里），所以配置和日志不会因为换机器而丢。

| 脚本 | 运行环境 | 产出 |
| --- | --- | --- |
| `script\package.ps1` | Windows PowerShell 5.1 / 7+ | 上面三份全产 |
| `script\package.cmd` | Windows cmd（薄包装，内部转调 ps1） | 同上 |
| `script/package.sh` | MSYS2 / Git Bash（Windows）| 同上；Linux / macOS 上只能出 `*_src.tar.gz` |

```powershell
# --- Windows ---
.\script\package.ps1                              # 默认：单文件 + 绿色目录 + 压缩包，一次全出
.\script\package.ps1 -SkipGreen                   # 只产单文件 exe
.\script\package.ps1 -SkipSingle                  # 只产绿色目录 zip
.\script\package.ps1 -SkipZip                     # 产两种形态但不压 zip
.\script\package.ps1 -EnigmaVBDir "D:\Tools\EVB"  # 手动指定 EVB 安装目录
.\script\package.ps1 -Version 1.2.0               # 指定版本号
.\script\package.ps1 -CheckOnly                   # 只检测（exit 0=最新 / 2=过期）
.\script\package.ps1 -Force                       # 产物最新也强制重打

# 用 cmd 也一样，参数原样透传
script\package.cmd -SkipGreen
script\package.cmd -CheckOnly
```

```bash
# --- MSYS2 / Git Bash（Windows，能出 exe）---
./script/package.sh                  # 默认：单文件 + 绿色目录 + 压缩包
./script/package.sh --skip-green     # 只产单文件 exe
./script/package.sh --skip-single    # 只产绿色目录 zip
./script/package.sh --with-source    # 额外打一份源码归档
./script/package.sh --check-only

# --- Linux / macOS（出不了可运行的程序）---
./script/package.sh                  # 只打源码归档
```

`dist/` 打完之后是这样（`build\green` 是中途暂存区，用完自动删）：

```
dist/
├── AutoClicker.exe                             单文件，17 MB，DLL 已内嵌
├── AutoClicker_v1.0.0_<日期>_single_win64.exe  带版本号的单文件副本
└── AutoClicker_v1.0.0_<日期>_win64.zip         绿色目录包（exe + 21 个 Qt DLL）
```

> **`script/package.ps1` 和 `package.sh` 都会自动避开 Cygwin / MSYS 版的 cmake**。
> 这类 cmake 会把 Windows 路径改写成 `/cygdrive/d/...` 传给 `-S`/`-B`，报
> 「source directory does not exist」。两者都优先用 Qt 自带的
> `E:\Qt\Tools\CMake_64\bin\cmake.exe`，并在没设 `CMAKE_PREFIX_PATH` 时自动探测
> `E:\Qt\6.*\mingw_64`。

### 直接从 cmake 打包（不用记脚本参数）

打包已经接进构建系统了，`CMakeLists.txt` 里注册了四个 target：

| 命令 | 做什么 | 耗时 |
| --- | --- | --- |
| `cmake --build build/release --target deploy` | 只跑 `windeployqt` 把 exe + DLL 铺到 `dist/`，不压缩 | 几秒 |
| `cmake --build build/release --target package` | 默认两种形态都出（绿色目录 zip + 单文件 exe） | 十几秒 |
| `cmake --build build/release --target package-green` | 只出绿色目录 zip | 十几秒 |
| `cmake --build build/release --target package-single` | 只出单文件 exe（需要 Enigma Virtual Box） | 十几秒 |

- 四个 target 都自动 `DEPENDS AutoClicker`，改了源码直接跑就会先增量编译再打包。
- 它们不是重写一套逻辑，而是转调 `script/package.ps1`——`windeployqt` 参数、
  dxcompiler 排除、`qgif.dll` 补全、EnigmaVB 调用这些细节只在脚本里维护一份。
- 版本号从 `project(... VERSION 1.0.0 ...)` 自动带过去，改 CMakeLists 一处即可。
- 不想注册这些 target 时配置 `-DAUTOCLICKER_ENABLE_PACKAGE=OFF`。
- 非 Windows 平台只注册 `deploy`（拷二进制到 `dist/`）和 `package`（打
  `AutoClicker_<版本>_linux64.tar.gz`），运行时依赖由系统或 CI 提供。

```powershell
# 典型用法：改完代码一条命令出全套
cmake --build build/release --target package
```


### exe 的图标与版本信息

两种形态的 exe 都带 **Ralsei 图标**和完整的版本信息，右键「属性 → 详细信息」能看到：

| 字段 | 值 |
|------|-----|
| 文件说明 | `AutoClicker - 自动连点器` |
| 产品名称 | `AutoClicker` |
| 文件版本 / 产品版本 | 跟 `-Version` 参数一致（默认 `1.0.0`） |
| 公司名称 | `YOUXICHUNMI` |
| 版权 | `MIT License` |

图标来自 `resources/static/Ralsei.ico`（七种尺寸），由 `resources/app.rc.in` 经 CMake
`configure_file` 生成的 `app.rc` 编进 PE 资源段。**`.qrc` 管不到文件图标** —— 它只
影响程序自己画的窗口/托盘图标，资源管理器里那个图标必须走 `.rc`。

单文件 exe 经 Enigma Virtual Box 打包后图标与版本信息原样保留（EVB 只往原 exe 后面
挂数据段，不动 PE 头部）。

> 自己改图标时注意三点：`.rc` 里的路径要用正斜杠（反斜杠会被 windres 当转义）；
> `.rc` 里的中文要配 `-c 65001`（否则按 GBK 读成乱码）；图标资源序号要小（第一条
> 才是主图标）。详见 `MEMORY.md` 的 9.13 节。

### 打包前自检

每个脚本启动时都会先看 `dist/` 里有没有今天的产物、是不是比源码新。
五个容易做错的地方都已处理：

1. **历史归档不算过期**。产物文件名带日期戳，换个日期重打一次，旧文件会一直
   显示「过期」。现在名字里日期戳不是今天的，标 `[归档]` 直接跳过判定。
2. **源码归档默认不参与判定**。`*_src.tar.gz` / `*_src.zip` 只有显式
   `--with-source` 才产出，默认标 `[跳过]`，否则自检每次都说「该重打了」，
   然后你就再也不看它了。
3. **今天一个产物都没有也算需要打包**，哪怕 `dist/` 里躺着一堆历史包。
4. **形态齐全检查**。默认产两种形态，所以今天缺 `*_win64.zip` 或缺
   `*_single_win64.exe` 都会判「需要打包」。
5. **切换形态需要 `-Force`**。`-SkipGreen` / `-SkipSingle` 会让某一份今天不再
   产出；如果你刚从一个形态切到另一个，shell 里跑时带上 `-Force` 更省事。

> 之前还按「谁产出的」跳过 `exe` / `_win64.zip`，后来取消了这个规则：`ps1` 和
> Windows 下的 `sh` 产出的是同一份东西，谁跑都能重建，没必要区分来源。
>
> 另外 `Invoke-Deploy` 复制完 exe 会把它的 mtime 设成当前时间。`Copy-Item` /
> `cp` 保留的是编译时的 mtime，不这样处理，刚部署完自检就会判「过期」。

三个脚本的实现各写各的（PowerShell / bash），所以改判定规则时**三个都要改，
并且跑一遍对比退出码**。这是我在上一版项目里真踩过的坑。

---

## 用 GitHub Actions 自动打包

仓库里带了一份 `.github/workflows/build.yml`，推到 GitHub 就会自动编译并打包出
**Windows 和 Linux** 两套产物，不用在自己机器上装 Qt。

| 事件 | 行为 |
| --- | --- |
| push 到 `main` | 编译 + 打包，产物作为 workflow artifact 保留 30 天 |
| `pull_request` | 同上（只构建，不发版） |
| push 形如 `v1.2.0` 的 tag | 额外自动建 GitHub Release，把两平台产物挂上去 |
| 手动 `workflow_dispatch` | 在 Actions 页面点「Run workflow」 |

两个 job 各自在原生 runner 上跑（Qt 不能交叉编译，Windows 产物必须在 Windows
上构建）：

- **windows** —— `jurplel/install-qt-action` 装 Qt 6.8.2 (win64_mingw) + Ninja，
  再尽力装 Enigma Virtual Box（装不上就自动降级成只产绿色目录），然后**直接调用
  仓库自己的 `script/package.ps1 -Force`**，所以 CI 产出的东西和你本机跑出来的
  完全一致；上传 `dist/*.exe` 与 `dist/*.zip`
- **linux** —— `apt-get` 装 `qt6-base-dev` 与 `libgl1-mesa-dev`，用
  `cmake -G Ninja` 编译，产出 `AutoClicker_v<版本>_<日期>_single_linux64` 与
  `AutoClicker_v<版本>_<日期>_linux64.tar.gz`

> Linux 版只有界面可用：连点引擎依赖 Win32 的 `SendInput` / `PostMessage`，
> 在 Linux 上点「开始连点」会明确报错拒绝，热键也不会注册。这是设计如此，
> 不是构建问题。

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
AutoClicker/
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
