<#
.SYNOPSIS
    AutoClickerQt 打包脚本（Windows）

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
    [switch]$SkipBuild,
    [switch]$SkipZip,
    [switch]$CheckOnly,
    [switch]$Force,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ------------------------------------------------------------------ 路径与常量
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = Split-Path -Parent $ScriptDir
$DistDir = Join-Path $ProjectDir "dist"
$DefaultBuildDir = Join-Path $ProjectDir "build\release"

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
        #   本脚本与 package.sh 在 Windows 下都产出 dist\AutoClickerQt.exe + 依赖 + *_win64.zip，
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
    $exePath = Join-Path $DistDir "AutoClickerQt.exe"
    if ((Test-Path $exePath) -and -not $SkipZip) {
        $zipName = "AutoClickerQt_v$Ver"
        $matching = $artifacts | Where-Object { $_.Name -like "$zipName*" }
        if (-not $matching) {
            Write-Warn2 "dist\ 里没有版本号 $Ver 的包，可能是别的版本打的"
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
    $logFile = Join-Path $env:TEMP "autoclickerqt_configure_$(Get-Random).log"

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
    $buildLog = Join-Path $env:TEMP "autoclickerqt_build_$(Get-Random).log"
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
    param([string]$Qt, [string]$Dir)

    Write-Step "收集 Qt 运行时依赖"

    $candidates = @(
        (Join-Path $Dir "AutoClickerQt.exe"),
        (Join-Path $Dir "Release\AutoClickerQt.exe")
    )
    $exePath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exePath) {
        throw "编译产物里找不到 AutoClickerQt.exe，查找过：`n  $($candidates -join "`n  ")"
    }
    Write-Info "可执行文件: $exePath"

    if (Test-Path $DistDir) {
        Remove-Item $DistDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
    Copy-Item $exePath (Join-Path $DistDir "AutoClickerQt.exe") -Force
    # Copy-Item 保留的是编译时的 mtime，自检拿它跟源码比会一直判「过期」，
    # 可实际上刚部署完。这里把部署时间写成当前时间。
    (Get-Item (Join-Path $DistDir "AutoClickerQt.exe")).LastWriteTime = Get-Date
    Write-Ok "已复制 exe 到 dist\"

    $windeployqt = Join-Path $Qt "bin\windeployqt.exe"
    if (-not (Test-Path $windeployqt)) {
        throw "找不到 windeployqt：$windeployqt"
    }

    $target = Join-Path $DistDir "AutoClickerQt.exe"
    $logFile = Join-Path $env:TEMP "autoclickerqt_deploy_$(Get-Random).log"
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $windeployqt --release --no-translations --no-system-d3d-compiler `
                       --no-opengl-sw --compiler-runtime $target *> $logFile
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }

    if ($code -ne 0) {
        Write-Warn2 "windeployqt 返回 $code，检查日志"
        Get-Content $logFile -Tail 20 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    }

    # GIF 播放依赖 qgif.dll，windeployqt 有时不会自动带上（取决于它怎么分析依赖）
    $imageFormats = Join-Path $DistDir "imageformats"
    $qgif = Join-Path $Qt "plugins\imageformats\qgif.dll"
    if ((Test-Path $qgif) -and -not (Test-Path (Join-Path $imageFormats "qgif.dll"))) {
        New-Item -ItemType Directory -Force -Path $imageFormats | Out-Null
        Copy-Item $qgif $imageFormats -Force
        Write-Info "补充 imageformats\qgif.dll（Ralsei 动画需要）"
    }

    Remove-Item $logFile -Force -ErrorAction SilentlyContinue

    $dllCount = (Get-ChildItem $DistDir -Recurse -File -Filter *.dll).Count
    Write-Ok "windeployqt 完成，共 $dllCount 个 DLL"

    # 目录结构自检
    if (-not (Test-Path (Join-Path $DistDir "platforms\qwindows.dll"))) {
        Write-Warn2 "缺少 platforms\qwindows.dll —— 运行时会报「could not find or load the Qt platform plugin windows」"
    }
}

# ------------------------------------------------------------------ 打包
function Invoke-Package {
    param([string]$Ver, [string]$Dir)

    Write-Step "压缩发布包"

    $stamp = Get-Date -Format 'yyyyMMdd'
    $zipName = "AutoClickerQt_v${Ver}_${stamp}_win64.zip"
    $zipPath = Join-Path $DistDir $zipName

    # 防止把上一轮的旧包也压进去
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    $items = Get-ChildItem -LiteralPath $DistDir | Where-Object { $_.Extension -ne '.zip' }
    if (-not $items) {
        throw "dist\ 是空的，没什么可打包"
    }

    Compress-Archive -LiteralPath ($items | ForEach-Object { $_.FullName }) `
                     -DestinationPath $zipPath -CompressionLevel Optimal -Force

    $sizeKb = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
    Write-Ok "发布包: dist\$zipName ($sizeKb KB)"
    return $zipPath
}

# ================================================================ 主流程

Write-Host ""
Write-Host "AutoClickerQt 打包脚本" -ForegroundColor Cyan
Write-Host "项目: $ProjectDir" -ForegroundColor Gray
Write-Host "版本: $Version" -ForegroundColor Gray

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

    if (-not $SkipBuild) {
        Invoke-Build -Qt $qt -Dir $BuildDir
    } else {
        Write-Step "跳过编译（-SkipBuild）"
    }

    Invoke-Deploy -Qt $qt -Dir $BuildDir

    $zip = $null
    if (-not $SkipZip) {
        $zip = Invoke-Package -Ver $Version -Dir $BuildDir
    }

    Write-Host ""
    Write-Host "打包完成。" -ForegroundColor Green
    Write-Host "  dist: $DistDir" -ForegroundColor Gray
    if ($zip) {
        Write-Host "  发布包: $zip" -ForegroundColor Gray
    }
    exit 0
} catch {
    Write-Host ""
    Write-Err $_.Exception.Message
    exit 1
}
