<#
.SYNOPSIS
    AutoClicker 打包脚本（Windows）

.DESCRIPTION
    编译 Release 版本，用 windeployqt 收集 Qt 运行时依赖，最后压成绿色包。
    三个步骤可以分开执行，方便排查问题。

.PARAMETER Version
    版本号，写进产物文件名。默认 1.0.0。

.PARAMETER QtDir
    Qt6 安装目录，例如 C:\Qt\6.8.0\msvc2019_64。
    不传则依次尝试：环境变量 CMAKE_PREFIX_PATH → 环境变量 QTDIR → 常见安装路径。

.PARAMETER BuildDir
    构建目录，默认 build\release。

.PARAMETER SkipBuild
    跳过编译，只用现有产物打包（产物已在 dist 时有用）。

.PARAMETER SingleExe
    只产单文件 exe，不打绿色目录 zip。

    默认（不加任何形态开关）两种都产：
      dist\AutoClicker.exe                          单文件 exe，拷一个文件就能跑
      dist\AutoClicker_v<版本>_<日期>_single_win64.exe   同一个文件，带版本号便于归档
      dist\AutoClicker_v<版本>_<日期>_win64.zip     绿色目录 zip（exe + DLL 文件夹）

    为什么两种都给：单文件适合分发给别人，绿色目录适合自己留着排障
    —— 出问题时能直接换 DLL 试。

.PARAMETER SkipSingle
    只产绿色目录，不打单文件 exe（等价于旧版的默认行为）。

.PARAMETER EnigmaVBDir
    Enigma Virtual Box 的安装目录（只有 -SingleExe 时才用）。
    不传则依次尝试：环境变量 ENIGMAVB_DIR → E:\Tools\EnigmaVB →
    C:\Program Files\Enigma Virtual Box → C:\Program Files (x86)\Enigma Virtual Box →
    %LOCALAPPDATA%\Enigma Virtual Box → PATH 里的 enigmavbconsole.exe。

.PARAMETER SkipZip
    只产出 dist 目录，不压缩。

.PARAMETER CheckOnly
    只检测产物是否最新，不打包。
    退出码：0 = 已是最新，2 = 需要重新打包。

.PARAMETER Force
    即使产物已是最新也强制重新打包。

.PARAMETER Clean
    打包前清空构建目录（改了 CMakeLists 后建议用）。

.EXAMPLE
    .\script\package.ps1

    默认产出绿色目录（exe + DLL 文件夹）。

.EXAMPLE
    .\script\package.ps1

    两种形态都产：单文件 exe 与绿色目录 zip，各自带版本号文件名。

.EXAMPLE
    .\script\package.ps1 -SingleExe

    只产单文件 exe，跳过绿色目录 zip（快一些）。

.EXAMPLE
    .\script\package.ps1 -SkipSingle

    只产绿色目录 zip，不跑 Enigma Virtual Box。

.EXAMPLE
    .\script\package.ps1 -Version 1.2.0 -QtDir C:\Qt\6.8.0\msvc2019_64

.EXAMPLE
    .\script\package.ps1 -CheckOnly
#>

[CmdletBinding()]
param(
    [string]$Version = "1.0.0",
    [string]$QtDir = "",
    [string]$BuildDir = "",
    [switch]$SingleExe,
    [switch]$SkipSingle,
    [string]$EnigmaVBDir = "",
    [switch]$SkipBuild,
    [switch]$SkipZip,
    [switch]$CheckOnly,
    [switch]$Force,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# 输出编码：CLion / VS Code 这类 IDE 通过 cmd.exe 调用 powershell.exe 时，
# 控制台输出编码是系统 OEM 代码页（中文机器 936），而 IDE 按 UTF-8 读，
# 结果所有中文变乱码。这里强制 UTF-8。写进 try 里是防老主机不支持。
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
} catch {
    # 某些非交互宿主没有 Console，忽略即可
}

# ------------------------------------------------------------------ 路径与常量
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = Split-Path -Parent $ScriptDir
$DistDir = Join-Path $ProjectDir "dist"
$DefaultBuildDir = Join-Path $ProjectDir "build\release"
# 绿色目录的暂存区：两种形态都要时先铺在这里，压完 zip 再被 EVB 收走，
# dist\ 最后只留可分发的东西（单文件 exe + 带版本号的 zip）
$GreenStageDir = Join-Path $ProjectDir "build\green"

# 参与"源码是否比产物新"判定的路径。
# config/ 与 log/ 是运行期数据，不参与；build/dist/.git 由 EXCLUDE_DIRS 过滤掉。
$SourceRoots = @('main.cpp', 'CMakeLists.txt', 'CMakePresets.json',
                 'core', 'ui', 'utils', 'resources', 'script', 'README.md')
$ExcludeDirs = @('build', 'build-*', 'cmake-build-*', 'dist', '.git', '.idea',
                 '.vscode', 'config', 'log', '.cache')

# ---------------------------------------------------------------- 输出小工具
function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "=== $Text ===" -ForegroundColor Cyan
}

function Write-Ok([string]$Text) {
    Write-Host "  [OK] $Text" -ForegroundColor Green
}

function Write-Info([string]$Text) {
    Write-Host "  $Text" -ForegroundColor Gray
}

function Write-Warn2([string]$Text) {
    Write-Host "  [!] $Text" -ForegroundColor Yellow
}

function Write-Err([string]$Text) {
    Write-Host "  [x] $Text" -ForegroundColor Red
}

# ------------------------------------------------------------ 查找 Qt 安装位置
# windeployqt 是 Qt 官方提供的依赖收集工具，有它就等于找到了 Qt。
function Find-QtDir {
    if ($QtDir) {
        if (Test-Path (Join-Path $QtDir "bin\windeployqt.exe")) {
            return (Resolve-Path $QtDir).Path
        }
        throw "指定的 QtDir 里没有 bin\windeployqt.exe：$QtDir"
    }

    $candidates = @()
    if ($env:CMAKE_PREFIX_PATH) {
        $candidates += ($env:CMAKE_PREFIX_PATH -split ';')
    }
    if ($env:QTDIR) { $candidates += $env:QTDIR }

    # 常见安装位置：C:\Qt\6.8.0\{msvc2019_64,mingw_64}
    foreach ($base in @("C:\Qt", "D:\Qt", "E:\Qt")) {
        if (-not (Test-Path $base)) { continue }
        foreach ($verDir in (Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
                             Where-Object { $_.Name -match '^6\.' } | Sort-Object Name -Descending)) {
            foreach ($kit in (Get-ChildItem $verDir.FullName -Directory -ErrorAction SilentlyContinue |
                              Where-Object { $_.Name -match '^(msvc|mingw)' })) {
                $candidates += $kit.FullName
            }
        }
    }

    foreach ($c in $candidates) {
        if (-not $c) { continue }
        $c = $c.Trim()
        if (Test-Path (Join-Path $c "bin\windeployqt.exe")) {
            return (Resolve-Path $c).Path
        }
    }

    throw @"
找不到 Qt6 安装。请用 -QtDir 指定，或设置环境变量：
    `$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.0\msvc2019_64"
下载地址：https://www.qt.io/download-qt-installer
"@
}

# ------------------------------------------------ 查找 Enigma Virtual Box 安装位置
# 只有 -SingleExe 时才需要。判定标准是目录里有 enigmavbconsole.exe（命令行版）。
function Find-EnigmaVBDir {
    $probe = {
        param([string]$Dir)
        if (-not $Dir) { return $false }
        return (Test-Path (Join-Path $Dir "enigmavbconsole.exe"))
    }

    if ($EnigmaVBDir) {
        if (& $probe $EnigmaVBDir) { return (Resolve-Path $EnigmaVBDir).Path }
        throw "指定的 EnigmaVBDir 里没有 enigmavbconsole.exe：$EnigmaVBDir"
    }

    $candidates = @()
    if ($env:ENIGMAVB_DIR) { $candidates += $env:ENIGMAVB_DIR }
    $candidates += @(
        "E:\Tools\EnigmaVB",
        "C:\Program Files\Enigma Virtual Box",
        "C:\Program Files (x86)\Enigma Virtual Box",
        (Join-Path $env:LOCALAPPDATA "Enigma Virtual Box"),
        (Join-Path $env:ProgramFiles "Enigma Virtual Box")
    )

    foreach ($c in $candidates) {
        if (& $probe $c) { return (Resolve-Path $c).Path }
    }

    # 最后看 PATH
    $onPath = Get-Command enigmavbconsole.exe -ErrorAction SilentlyContinue
    if ($onPath) { return (Split-Path -Parent $onPath.Source) }

    throw @"
找不到 Enigma Virtual Box。它是把多个 DLL 嵌进单个 exe 的打包工具（免费）。

下载：https://enigmaprotector.com/en/downloads.html
安装后它会落在 C:\Program Files\Enigma Virtual Box，
      或者用 -EnigmaVBDir 指定安装目录，
      或者设置环境变量 `$env:ENIGMAVB_DIR = "E:\Tools\EnigmaVB"。

不想装它也行 —— 不加 -SingleExe 就是默认的绿色目录模式，不需要这个工具。
"@
}

# --------------------------------------------------- 扫描源码最新修改时间
function Get-SourceStamp {
    $newest = $null
    $newestFile = ""

    foreach ($root in $SourceRoots) {
        $full = Join-Path $ProjectDir $root
        if (-not (Test-Path $full)) { continue }

        $items = if (Test-Path $full -PathType Container) {
            Get-ChildItem $full -Recurse -File -ErrorAction SilentlyContinue
        } else {
            Get-Item $full
        }

        foreach ($f in $items) {
            $rel = $f.FullName.Substring($ProjectDir.Length).TrimStart('\', '/')
            $skip = $false
            foreach ($ex in $ExcludeDirs) {
                $pattern = [regex]::Escape($ex) -replace '\\\*', '[^\\/]*'
                if ($rel -match "(^|[\\/])$pattern([\\/]|$)") { $skip = $true; break }
            }
            if ($skip) { continue }
            if ($f.Extension -in @('.pyc', '.o', '.obj', '.exe', '.dll')) { continue }

            if ($null -eq $newest -or $f.LastWriteTime -gt $newest) {
                $newest = $f.LastWriteTime
                $newestFile = $rel
            }
        }
    }

    return [pscustomobject]@{
        Time = $newest
        File = $newestFile
    }
}

# --------------------------------------------------------- 产物检测（可单独跑）
function Show-BuildStatus {
    param([string]$Ver)

    Write-Step "打包产物检测"

    if (-not (Test-Path $DistDir)) {
        Write-Warn2 "还没打过包：dist\ 目录不存在"
        return $false
    }

    $stamp = Get-SourceStamp
    if ($null -eq $stamp.Time) {
        Write-Warn2 "没有找到任何源码文件，跳过检测"
        return $false
    }
    Write-Info ("源码最新改动: {0}  ({1})" -f $stamp.Time.ToString("yyyy-MM-dd HH:mm:ss"), $stamp.File)

    $todayStamp = Get-Date -Format 'yyyyMMdd'
    $artifacts = Get-ChildItem -LiteralPath $DistDir -File -ErrorAction SilentlyContinue |
                 Where-Object { $_.Name -match '\.(exe|zip|tar\.gz)$' } |
                 Sort-Object LastWriteTime -Descending

    if (-not $artifacts) {
        Write-Warn2 "dist\ 里没有任何产物"
        return $false
    }

    $allFresh = $true
    $todayCount = 0
    $skippedCount = 0

    foreach ($a in $artifacts) {
        $age = (Get-Date) - $a.LastWriteTime
        $ageText = if ($age.TotalMinutes -lt 60) {
            "{0:N0} 分钟前" -f $age.TotalMinutes
        } elseif ($age.TotalHours -lt 24) {
            "{0:N1} 小时前" -f $age.TotalHours
        } else {
            "{0:N0} 天前" -f $age.TotalDays
        }
        $sizeMb = "{0:N1} MB" -f ($a.Length / 1MB)

        # 产物归属：判过期的意义是"提醒我该重打了"。不属于自己的产物永远打不出来，
        # 算进来只会让自检每次都报过期，最后就没人看它了。
        #   *_src.tar.gz / *_src.zip → 源码归档，只有 package.sh --with-source 会产
        #   本脚本与 package.sh 在 Windows 下都产出 dist\AutoClicker.exe + 依赖 + *_win64.zip，
        #   所以这两类不再按"谁产出的"跳过 —— 谁跑都能重建它们。
        if ($a.Name -like '*_src.tar.gz' -or $a.Name -like '*_src.zip') {
            Write-Host ("  [跳过] {0}  ({1})  源码归档，package.sh --with-source 的产物" -f $a.Name, $sizeMb) -ForegroundColor DarkGray
            $skippedCount++
            continue
        }

        # 文件名里的日期戳不是今天的，视为历史归档，不参与"是否最新"判定。
        # 否则换个日期重打一次，旧文件会永远显示"过期"，自检就成了噪音。
        $isOldDate = $false
        if ($a.Name -match '_(\d{8})') {
            $isOldDate = ($Matches[1] -ne $todayStamp)
        }

        if ($isOldDate) {
            Write-Host ("  [归档] {0}  ({1})  历史产物，不参与判定" -f $a.Name, $sizeMb) -ForegroundColor DarkGray
            continue
        }

        $todayCount++
        if ($a.LastWriteTime -lt $stamp.Time) {
            Write-Host ("  [过期] {0}  ({1}, {2})" -f $a.Name, $sizeMb, $ageText) -ForegroundColor Yellow
            $allFresh = $false
        } else {
            Write-Host ("  [最新] {0}  ({1}, {2})" -f $a.Name, $sizeMb, $ageText) -ForegroundColor Green
        }
    }

    if ($todayCount -eq 0) {
        if ($skippedCount -gt 0) {
            Write-Warn2 ("dist\ 里只有别的脚本的产物，本脚本今天还没打过包 —— 需要打包。")
        } else {
            Write-Warn2 ("dist\ 里没有今天（{0}）的产物 —— 需要打包。" -f $todayStamp)
        }
        return $false
    }

    # 版本号一致性
    $exePath = Join-Path $DistDir "AutoClicker.exe"
    if (Test-Path $exePath) {
        $zipName = "AutoClicker_v$Ver"
        $matching = $artifacts | Where-Object { $_.Name -like "$zipName*" }
        if (-not $matching) {
            Write-Warn2 "dist\ 里没有版本号 $Ver 的包，可能是别的版本打的"
        }
    }

    # 该有的形态齐全吗？两种形态产出的文件名不一样，
    # 只按 mtime 判「过期」发现不了「这一种形态根本没打过」。
    $todayArtifacts = $artifacts | Where-Object { $_.Name -match "_$todayStamp`." }
    if ($todayArtifacts) {
        if (-not $SingleExe) {
            $hasGreen = $todayArtifacts | Where-Object { $_.Name -like '*_win64.zip' }
            if (-not $hasGreen) {
                Write-Warn2 "今天还没有绿色目录 zip（AutoClicker_v${Ver}_${todayStamp}_win64.zip）。"
                $allFresh = $false
            }
        }
        if (-not $SkipSingle) {
            $hasSingle = $todayArtifacts | Where-Object { $_.Name -like '*_single_win64.exe' }
            if (-not $hasSingle) {
                Write-Warn2 "今天还没有单文件 exe（AutoClicker_v${Ver}_${todayStamp}_single_win64.exe）。"
                $allFresh = $false
            }
        }
    }

    if ($allFresh) {
        Write-Host ""
        Write-Host "结论：产物已是最新，无需重新打包。" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "结论：源码比产物新，建议重新打包。" -ForegroundColor Yellow
    }

    return $allFresh
}

# ------------------------------------------------------------------ 工具链
# 必须避开 Cygwin / MSYS 版 cmake：它们把 Windows 路径改写成 /cygdrive/d/...，
# 传给 -S / -B 会让 CMake 认为源码目录不存在。优先用 Qt 自带的那份。
function Test-NativeTool {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path $Path)) { return $false }
    $norm = $Path.Replace('/', '\').ToLower()
    if ($norm -match '\\cygwin|\\msys|\\git\\usr\\bin') { return $false }
    return $true
}

function Find-CMake {
    param([string]$Qt)

    # 1) Qt 安装目录旁边通常带一份 CMake（E:\Qt\Tools\CMake_64\bin\cmake.exe）
    $qtRoot = Split-Path (Split-Path $Qt -Parent) -Parent
    if ($qtRoot) {
        $candidate = Join-Path $qtRoot "Tools\CMake_64\bin\cmake.exe"
        if (Test-NativeTool $candidate) { return (Resolve-Path $candidate).Path }
    }

    # 2) PATH 里的 cmake，但要排除 Cygwin / MSYS 版本
    foreach ($c in (Get-Command cmake -All -ErrorAction SilentlyContinue)) {
        if (Test-NativeTool $c.Source) { return $c.Source }
    }

    # 3) 常见原生安装位置
    foreach ($p in @("$env:ProgramFiles\CMake\bin\cmake.exe",
                     "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe",
                     "$env:LOCALAPPDATA\Programs\CMake\bin\cmake.exe")) {
        if (Test-NativeTool $p) { return (Resolve-Path $p).Path }
    }

    throw @"
找不到可用的原生 cmake。PATH 里的 cmake 若是 Cygwin / MSYS 版本，会把
Windows 路径改写成 /cygdrive/... 导致配置失败。请装一份 Windows 原生 CMake
（或装 Qt 时勾选 Tools > CMake），也可以把它加进 PATH 后重试。
"@
}

function Find-Ninja {
    param([string]$Qt)
    $qtRoot = Split-Path (Split-Path $Qt -Parent) -Parent
    if ($qtRoot) {
        $candidate = Join-Path $qtRoot "Tools\Ninja\ninja.exe"
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
    $n = Get-Command ninja -ErrorAction SilentlyContinue
    if ($n) { return $n.Source }
    return $null
}

# ------------------------------------------------------------------ 编译
function Invoke-Build {
    param([string]$Qt, [string]$Dir)

    Write-Step "编译 Release"

    $cmake = Find-CMake -Qt $Qt
    Write-Info "cmake: $($cmake)"

    if ($Clean -and (Test-Path $Dir)) {
        Write-Info "清空构建目录: $Dir"
        Remove-Item $Dir -Recurse -Force
    }

    $configureArgs = @(
        '-S', $ProjectDir,
        '-B', $Dir,
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$Qt"
    )
    # 首次配置时自动选生成器；已配置过就沿用缓存里的
    if (-not (Test-Path (Join-Path $Dir "CMakeCache.txt"))) {
        $ninja = Find-Ninja -Qt $Qt
        if ($ninja) {
            $configureArgs += @('-G', 'Ninja')
            $configureArgs += @("-DCMAKE_MAKE_PROGRAM=$ninja")
            Write-Info "生成器: Ninja ($ninja)"
        } else {
            Write-Info "生成器: CMake 默认"
        }
    }

    Write-Info "配置: cmake $($configureArgs -join ' ')"
    $logFile = Join-Path $env:TEMP "autoclicker_configure_$(Get-Random).log"

    # 原生命令写 stderr 在 ErrorActionPreference=Stop 下会变成终止性错误，
    # 即使重定向了也一样 —— 这里临时降级，最后单独判 $LASTEXITCODE。
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $cmake @configureArgs *> $logFile
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }

    if ($code -ne 0) {
        Write-Err "配置失败（退出码 $code）"
        Get-Content $logFile -Tail 40 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
        $keep = Join-Path $ProjectDir "build\configure-error.log"
        New-Item -ItemType Directory -Force -Path (Split-Path $keep) | Out-Null
        Move-Item $logFile $keep -Force
        throw "配置失败，完整日志: $keep"
    }
    Remove-Item $logFile -Force -ErrorAction SilentlyContinue
    Write-Ok "配置成功"

    Write-Info "编译中..."
    $buildLog = Join-Path $env:TEMP "autoclicker_build_$(Get-Random).log"
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $cmake --build $Dir --config Release *> $buildLog
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }

    if ($code -ne 0) {
        Write-Err "编译失败（退出码 $code）"
        Get-Content $buildLog -Tail 60 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
        $keep = Join-Path $ProjectDir "build\build-error.log"
        Move-Item $buildLog $keep -Force
        throw "编译失败，完整日志: $keep"
    }
    Get-Content $buildLog -Tail 5 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    Remove-Item $buildLog -Force -ErrorAction SilentlyContinue
    Write-Ok "编译成功"
}

# --------------------------------------------------- 收集依赖并铺到 dist
function Invoke-Deploy {
    param([string]$Qt, [string]$Dir, [string]$TargetDir = $TargetDir)

    Write-Step "收集 Qt 运行时依赖"

    $candidates = @(
        (Join-Path $Dir "AutoClicker.exe"),
        (Join-Path $Dir "Release\AutoClicker.exe")
    )
    $exePath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exePath) {
        throw "编译产物里找不到 AutoClicker.exe，查找过：`n  $($candidates -join "`n  ")"
    }
    Write-Info "可执行文件: $exePath"

    if (Test-Path $TargetDir) {
        Remove-Item $TargetDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    Copy-Item $exePath (Join-Path $TargetDir "AutoClicker.exe") -Force
    # Copy-Item 保留的是编译时的 mtime，自检拿它跟源码比会一直判「过期」，
    # 可实际上刚部署完。这里把部署时间写成当前时间。
    (Get-Item (Join-Path $TargetDir "AutoClicker.exe")).LastWriteTime = Get-Date
    Write-Ok "已复制 exe 到 dist\"

    $windeployqt = Join-Path $Qt "bin\windeployqt.exe"
    if (-not (Test-Path $windeployqt)) {
        throw "找不到 windeployqt：$windeployqt"
    }

    $target = Join-Path $TargetDir "AutoClicker.exe"
    $logFile = Join-Path $env:TEMP "autoclicker_deploy_$(Get-Random).log"
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
    # --no-system-dxc-compiler 会跳过 dxcompiler.dll(24 MB) + dxil.dll ——
    # 它们是 Qt 给 Direct3D 的着色器编译器，本项目纯 Widgets 用不到。
    # 实测删掉后界面与连点功能都正常，dist 从 65 MB 降到 39 MB。
        & $windeployqt --release --no-translations --no-system-d3d-compiler `
                       --no-system-dxc-compiler --no-opengl-sw --compiler-runtime $target *> $logFile
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }

    if ($code -ne 0) {
        Write-Warn2 "windeployqt 返回 $code，检查日志"
        Get-Content $logFile -Tail 20 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    }

    # GIF 播放依赖 qgif.dll，windeployqt 有时不会自动带上（取决于它怎么分析依赖）
    $imageFormats = Join-Path $TargetDir "imageformats"
    $qgif = Join-Path $Qt "plugins\imageformats\qgif.dll"
    if ((Test-Path $qgif) -and -not (Test-Path (Join-Path $imageFormats "qgif.dll"))) {
        New-Item -ItemType Directory -Force -Path $imageFormats | Out-Null
        Copy-Item $qgif $imageFormats -Force
        Write-Info "补充 imageformats\qgif.dll（Ralsei 动画需要）"
    }

    Remove-Item $logFile -Force -ErrorAction SilentlyContinue

    $dllCount = (Get-ChildItem $TargetDir -Recurse -File -Filter *.dll).Count
    Write-Ok "windeployqt 完成，共 $dllCount 个 DLL"

    # 目录结构自检
    if (-not (Test-Path (Join-Path $TargetDir "platforms\qwindows.dll"))) {
        Write-Warn2 "缺少 platforms\qwindows.dll —— 运行时会报「could not find or load the Qt platform plugin windows」"
    }
}

# ------------------------------------------------------------------ 打包
function Invoke-Package {
    param([string]$Ver, [string]$SourceDir = $DistDir)

    Write-Step "压缩发布包"

    $stamp = Get-Date -Format 'yyyyMMdd'
    $zipName = "AutoClicker_v${Ver}_${stamp}_win64.zip"
    $zipPath = Join-Path $DistDir $zipName

    # 防止把上一轮的旧包也压进去
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    $items = Get-ChildItem -LiteralPath $SourceDir | Where-Object { $_.Extension -ne '.zip' }
    if (-not $items) {
        throw "绿色目录是空的，没什么可打包: $SourceDir"
    }

    Compress-Archive -LiteralPath ($items | ForEach-Object { $_.FullName }) `
                     -DestinationPath $zipPath -CompressionLevel Optimal -Force

    $sizeKb = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
    Write-Ok "发布包: dist\$zipName ($sizeKb KB)"
    return $zipPath
}

# ------------------------------------------------------------ 单文件 exe 打包
# Enigma Virtual Box 是文件虚拟化工具：把所有 DLL / 插件嵌进 exe，
# 运行时由它注入的 loader 在内存里模拟文件系统，
# 所以磁盘上不会出现这些文件，拷一个 exe 到任何地方双击就能跑。
function ConvertTo-EvbXmlText {
    param([string]$Text)
    # XML 只有这三个字符必须转义，& 要先换，否则会把后面换出来的 &amp; 再换一次
    return $Text.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;')
}

function New-EvbProjectFile {
    param(
        [string]$InputExe,
        [string]$OutputExe,
        [object[]]$EmbedFiles,
        [string]$EvbPath,
        [string]$BaseDir
    )

    # 把扁平的文件列表还原成目录树。
    # 为什么要建树：EVB 里文件的 <Name> 写成 "platforms\qwindows.dll" 是无效的 ——
    # 它不会自动创建中间目录，结果 qwindows.dll 被平铺在根目录，
    # Qt 于是找不到平台插件、只弹一个 "no Qt platform plugin could be initialized" 的框。
    # 正确做法是每一层目录都显式写一个 <Type>3</Type> 节点。
    $root = Get-Item -LiteralPath $BaseDir
    $tree = @{ Files = @(); Dirs = @{} }

    foreach ($f in $EmbedFiles) {
        $rel = $f.FullName.Substring($BaseDir.Length + 1)
        $parts = $rel -split '\\'
        $node = $tree
        for ($i = 0; $i -lt $parts.Length - 1; $i++) {
            $name = $parts[$i]
            if (-not $node.Dirs.ContainsKey($name)) {
                $node.Dirs[$name] = @{ Files = @(); Dirs = @{} }
            }
            $node = $node.Dirs[$name]
        }
        $node.Files += , @{ Name = $parts[-1]; Path = $f.FullName }
    }

    $indent = "`t"
    $sb = New-Object System.Text.StringBuilder

    # 递归写一个目录节点的内容（不含外层 <File> 标签）
    $writeDirBody = $null
    $writeFileNode = {
        param($sb2, $item, $pad)
        [void]$sb2.AppendLine("$pad<File>")
        [void]$sb2.AppendLine("$pad`t<Type>2</Type>")   # 2 = 文件
        [void]$sb2.AppendLine("$pad`t<Name>$(ConvertTo-EvbXmlText $item.Name)</Name>")
        [void]$sb2.AppendLine("$pad`t<File>$(ConvertTo-EvbXmlText $item.Path)</File>")
        [void]$sb2.AppendLine("$pad`t<ActiveX>false</ActiveX>")
        [void]$sb2.AppendLine("$pad`t<ActiveXInstall>false</ActiveXInstall>")
        [void]$sb2.AppendLine("$pad`t<Action>0</Action>")
        [void]$sb2.AppendLine("$pad`t<OverwriteDateTime>false</OverwriteDateTime>")
        [void]$sb2.AppendLine("$pad`t<OverwriteAttributes>false</OverwriteAttributes>")
        [void]$sb2.AppendLine("$pad`t<PassCommandLine>false</PassCommandLine>")
        [void]$sb2.AppendLine("$pad</File>")
    }

    $writeDirBody = {
        param($sb2, $node, $pad)
        foreach ($f in $node.Files) {
            & $writeFileNode $sb2 $f $pad
        }
        foreach ($d in $node.Dirs.Keys) {
            [void]$sb2.AppendLine("$pad<File>")
            [void]$sb2.AppendLine("$pad`t<Type>3</Type>")   # 3 = 目录
            [void]$sb2.AppendLine("$pad`t<Name>$(ConvertTo-EvbXmlText $d)</Name>")
            [void]$sb2.AppendLine("$pad`t<Action>0</Action>")
            [void]$sb2.AppendLine("$pad`t<OverwriteDateTime>false</OverwriteDateTime>")
            [void]$sb2.AppendLine("$pad`t<OverwriteAttributes>false</OverwriteAttributes>")
            [void]$sb2.AppendLine("$pad`t<Files>")
            & $writeDirBody $sb2 $node.Dirs[$d] "$pad`t`t"
            [void]$sb2.AppendLine("$pad`t</Files>")
            [void]$sb2.AppendLine("$pad</File>")
        }
    }

    # 注意：Enigma Virtual Box 的 .evb 是 UTF-16 的 XML，
    # 且根节点名是空的（<> ... </>），不是常见的 <Project>。
    # 用别的根节点名它不报错，但 Files 列表会被整体忽略 —— 所以这里不能改。
    [void]$sb.AppendLine('<?xml version="utf-16"?>')
    [void]$sb.AppendLine('<>')
    [void]$sb.AppendLine("`t<InputFile>$(ConvertTo-EvbXmlText $InputExe)</InputFile>")
    [void]$sb.AppendLine("`t<OutputFile>$(ConvertTo-EvbXmlText $OutputExe)</OutputFile>")
    [void]$sb.AppendLine("`t<Files>")
    [void]$sb.AppendLine("`t`t<Enabled>true</Enabled>")
    [void]$sb.AppendLine("`t`t<DeleteExtractedOnExit>true</DeleteExtractedOnExit>")
    [void]$sb.AppendLine("`t`t<CompressFiles>true</CompressFiles>")
    [void]$sb.AppendLine("`t`t<Files>")
    [void]$sb.AppendLine("`t`t`t<File>")
    [void]$sb.AppendLine("`t`t`t`t<Type>3</Type>")   # 3 = 目录
    [void]$sb.AppendLine("`t`t`t`t<Name>%DEFAULT FOLDER%</Name>")
    [void]$sb.AppendLine("`t`t`t`t<Action>0</Action>")
    [void]$sb.AppendLine("`t`t`t`t<OverwriteDateTime>false</OverwriteDateTime>")
    [void]$sb.AppendLine("`t`t`t`t<OverwriteAttributes>false</OverwriteAttributes>")
    [void]$sb.AppendLine("`t`t`t`t<Files>")

    & $writeDirBody $sb $tree "`t`t`t`t`t"

    [void]$sb.AppendLine("`t`t`t`t</Files>")
    [void]$sb.AppendLine("`t`t`t</File>")
    [void]$sb.AppendLine("`t`t</Files>")
    [void]$sb.AppendLine("`t</Files>")
    [void]$sb.AppendLine("`t<Registries><Enabled>false</Enabled><Registries/></Registries>")
    [void]$sb.AppendLine("`t<Packaging><Enabled>false</Enabled></Packaging>")
    [void]$sb.AppendLine("`t<Options>")
    [void]$sb.AppendLine("`t`t<ShareVirtualSystem>true</ShareVirtualSystem>")
    [void]$sb.AppendLine("`t`t<MapExecutableWithTemporaryFile>true</MapExecutableWithTemporaryFile>")
    [void]$sb.AppendLine("`t`t<AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles>")
    [void]$sb.AppendLine("`t</Options>")
    [void]$sb.AppendLine('</>')

    # 必须 UTF-16 LE + BOM，否则 enigmavbconsole 会把文件当坏项目
    [System.IO.File]::WriteAllText($EvbPath, $sb.ToString(),
        (New-Object System.Text.UnicodeEncoding($false, $true)))
}

function Invoke-SingleExe {
    param([string]$EnigmaDir, [string]$Ver)

    Write-Step "打包成单文件 exe"

    $exePath = Join-Path $DistDir "AutoClicker.exe"
    if (-not (Test-Path $exePath)) {
        throw "dist\ 里没有 AutoClicker.exe，没法做单文件打包"
    }

    # 要嵌进去的是 dist 下除「主 exe / 压缩包 / 运行期数据」之外的全部内容
    $embed = Get-ChildItem -LiteralPath $DistDir -Recurse -File | Where-Object {
        $_.Name -ne 'AutoClicker.exe' -and
        $_.Extension -ne '.zip' -and
        $_.FullName -notmatch '\\(config|log)\\'
    }

    if (-not $embed) {
        throw "dist\ 里除了主 exe 没有其他文件，windeployqt 是不是没跑？"
    }

    $embedMb = [math]::Round((($embed | Measure-Object Length -Sum).Sum) / 1MB, 2)
    Write-Info "待嵌入 $($embed.Count) 个文件，共 $embedMb MB"

    $evbDir = Join-Path $env:TEMP "autoclicker_evb_$(Get-Random)"
    New-Item -ItemType Directory -Force -Path $evbDir | Out-Null

    # 三个路径约束（踩过坑，别改）：
    #   1. OutputFile 不能等于 InputFile —— EVB 会报 Cannot create file
    #   2. 路径不能含非 ASCII 字符 —— 同样报 Cannot create file，且报错信息本身是乱码
    #   3. 所以先输出到临时目录的 ASCII 文件名，成功后再搬回 dist 并改名
    $tmpOut = Join-Path $evbDir "boxed.exe"
    $evbPath = Join-Path $evbDir "build.evb"

    try {
        New-EvbProjectFile -InputExe $exePath -OutputExe $tmpOut `
                           -EmbedFiles $embed -EvbPath $evbPath -BaseDir $DistDir

        $console = Join-Path $EnigmaDir "enigmavbconsole.exe"
        $logFile = Join-Path $evbDir "evb.log"

        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & $console $evbPath *> $logFile
            $code = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $prevEap
        }

        if ($code -ne 0 -or -not (Test-Path $tmpOut)) {
            $tail = ""
            if (Test-Path $logFile) {
                $tail = (Get-Content $logFile -Tail 15) -join "`n"
            }
            throw "Enigma Virtual Box 打包失败（退出码 $code）`n$tail"
        }

        $boxedMb = [math]::Round((Get-Item $tmpOut).Length / 1MB, 2)
        Write-Ok "单文件 exe 生成成功（$boxedMb MB）"

        # 单文件模式下 dist 里只留这一个 exe，免得用户不知道该拷哪个
        Get-ChildItem -LiteralPath $DistDir -Force | Where-Object {
            $_.Name -ne 'AutoClicker.exe' -and
            $_.Extension -ne '.zip' -and
            $_.Name -notin @('config', 'log')
        } | Remove-Item -Recurse -Force

        Move-Item $tmpOut $exePath -Force
        (Get-Item $exePath).LastWriteTime = Get-Date

        # 除了固定名 AutoClicker.exe（方便直接双击），再留一份带版本号的，
        # 这样 dist\ 里的东西都能按名字看出是哪一版、哪一天打的。
        # 和绿色目录 zip 的命名规则对齐：<名字>_v<版本>_<日期>_<形态>_<平台>.
        $stamp = Get-Date -Format 'yyyyMMdd'
        $versioned = Join-Path $DistDir "AutoClicker_v${Ver}_${stamp}_single_win64.exe"
        Copy-Item $exePath $versioned -Force
        Write-Ok "带版本号的副本: $(Split-Path -Leaf $versioned)"

        Write-Ok "dist\ 已清理为单文件形态（只留 exe 与压缩包）"
    } finally {
        Remove-Item $evbDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ================================================================ 主流程

Write-Host ""
Write-Host "AutoClicker 打包脚本" -ForegroundColor Cyan
Write-Host "项目: $ProjectDir" -ForegroundColor Gray
Write-Host "版本: $Version" -ForegroundColor Gray
$wantGreen  = -not $SingleExe       # 默认要绿色目录；-SingleExe 时才不要
$wantSingle = -not $SkipSingle      # 默认要单文件；-SkipSingle 时才不要
$forms = @()
if ($wantGreen)  { $forms += "绿色目录 zip" }
if ($wantSingle) { $forms += "单文件 exe" }
Write-Host "形态: $($forms -join ' + ')" -ForegroundColor Gray

if (-not $BuildDir) { $BuildDir = $DefaultBuildDir }

$fresh = Show-BuildStatus -Ver $Version

if ($CheckOnly) {
    if ($fresh) { exit 0 } else { exit 2 }
}

if ($fresh -and -not $Force) {
    if (-not [Console]::IsInputRedirected) {
        $answer = Read-Host "产物已是最新，仍要重新打包吗？[y/N]"
        if ($answer -notmatch '^[yY]') {
            Write-Host ""
            Write-Host "已跳过打包。" -ForegroundColor Gray
            exit 0
        }
    } else {
        Write-Info "非交互环境，继续打包（用 -CheckOnly 可只检测）。"
    }
}

try {
    $qt = Find-QtDir
    Write-Info "Qt: $qt"

    # 要单文件就先确认 Enigma Virtual Box 在，免得白编译一场才失败
    $evb = $null
    if ($wantSingle) {
        $evb = Find-EnigmaVBDir
        Write-Info "Enigma Virtual Box: $evb"
    }

    if (-not $SkipBuild) {
        Invoke-Build -Qt $qt -Dir $BuildDir
    } else {
        Write-Step "跳过编译（-SkipBuild）"
    }

    # 两种形态的要分开铺，否则单文件那步会把绿色目录的 DLL 收走，
    # 就没东西可压 zip 了。
    #   - 只有单文件     → 直接铺到 dist\，EVB 原地收走，dist 剩一个 exe
    #   - 只有绿色目录   → 直接铺到 dist\，就地压 zip
    #   - 两种都要       → 先铺到 build\green\，压完 zip 再铺一份到 dist\ 给 EVB
    $bothForms = $wantGreen -and $wantSingle

    # dist\ 一律先清空重建：上一轮的旧产物（可能是改名前的 AutoClickerQt.exe）
    # 如果留着，会被 Invoke-SingleExe 当成依赖一起嵌进去，体积白白多一个 exe。
    if (Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

    # 收集运行时依赖这一步两种形态都需要：
    #   - 只有绿色目录   → 直接铺到 dist\，就地压 zip
    #   - 只有单文件     → 也要先铺到 dist\，EVB 才有东西可嵌
    #   - 两种都要       → 先铺到 build\green\，压完 zip 再复制一份到 dist\ 给 EVB
    $greenDir = if ($bothForms) { $GreenStageDir } else { $DistDir }
    if (Test-Path $greenDir) { Remove-Item $greenDir -Recurse -Force }
    Invoke-Deploy -Qt $qt -Dir $BuildDir -TargetDir $greenDir

    $zip = $null
    if ($wantGreen -and (-not $SkipZip)) {
        $zip = Invoke-Package -Ver $Version -SourceDir $greenDir
    }

    if ($wantSingle) {
        if ($bothForms) {
            # 把绿色目录整体复制一份到 dist\，EVB 从这里收文件
            foreach ($item in Get-ChildItem -LiteralPath $greenDir -Force) {
                Copy-Item $item.FullName -Destination $DistDir -Recurse -Force
            }
        }
        Invoke-SingleExe -EnigmaDir $evb -Ver $Version
        if ($bothForms -and (Test-Path $GreenStageDir)) {
            Remove-Item $GreenStageDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    Write-Host ""
    Write-Host "打包完成。" -ForegroundColor Green
    Write-Host "  dist: $DistDir" -ForegroundColor Gray
    Get-ChildItem -LiteralPath $DistDir -File | Where-Object {
        $_.Extension -in @('.exe', '.zip', '.tar.gz')
    } | Sort-Object Name | ForEach-Object {
        $mb = [math]::Round($_.Length / 1MB, 2)
        Write-Host ("  {0,-48} {1,8} MB" -f $_.Name, $mb) -ForegroundColor Gray
    }
    exit 0
} catch {
    Write-Host ""
    Write-Err $_.Exception.Message
    exit 1
}
