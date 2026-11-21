@echo off
rem AutoClicker 打包脚本（Windows 批处理包装）
rem
rem 本脚本只是薄包装：找一个可用的 PowerShell，把参数原样转给 package.ps1。
rem 用法与 package.ps1 完全一致，例如：
rem     script\package.cmd
rem     script\package.cmd -Version 1.2.0
rem     script\package.cmd -CheckOnly
rem     script\package.cmd -SkipBuild -SkipZip
rem     script\package.cmd -SingleExe    (打包成单个 exe)
rem
rem 注意：文件存为 GBK + CRLF。cmd 按当前代码页逐字节读批处理，
rem 中途改 chcp 会让后半段中文按旧码页解释而乱码，所以这里不切码页。

setlocal

set "SCRIPT_DIR=%~dp0"
set "PS1=%SCRIPT_DIR%package.ps1"

if not exist "%PS1%" (
    echo [x] 找不到 package.ps1: %PS1%
    exit /b 1
)

rem 优先用新版的 pwsh（PowerShell 7+），没有则退回系统自带的 powershell
where pwsh >nul 2>nul
if not errorlevel 1 (
    set "PS_EXE=pwsh"
    goto :run
)

where powershell >nul 2>nul
if not errorlevel 1 (
    set "PS_EXE=powershell"
    goto :run
)

echo [x] 找不到 PowerShell，请安装 PowerShell 7 或确认系统自带 powershell 可用
exit /b 1

:run
"%PS_EXE%" -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
exit /b %errorlevel%