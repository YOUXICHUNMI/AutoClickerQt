#!/bin/bash
# AutoClickerQt 打包脚本（Linux / macOS / MSYS2）
#
# 用途：编译 Release 版本并产出可运行的程序。
#       Windows(MSYS2) 下默认产出一份带全部 Qt 依赖的 dist/，可直接运行；
#       非 Windows 平台编译不出可用程序（连点依赖 Win32 API），只会打源码归档。
#
# 用法：
#   ./script/package.sh                     # 默认：编译 + 收集依赖（绿色目录）
#   ./script/package.sh --single-exe        # 额外打包成单文件 exe（需 Enigma Virtual Box，仅 Windows）
#   ./script/package.sh --with-source       # 额外打一份源码归档
#   ./script/package.sh --no-exe            # 只编译，不收集依赖
#   ./script/package.sh --no-build          # 跳过编译，直接用现有产物
#   ./script/package.sh --version 1.2.0     # 指定版本号
#   ./script/package.sh --check-only        # 只检测，不打包（exit 0=最新 / 2=过期）
#   ./script/package.sh --force             # 产物最新也强制重打
#   ./script/package.sh --help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION="1.0.0"
DO_BUILD=1
DO_ZIP=0        # 源码归档只在 --with-source 时产出
CHECK_ONLY=0
FORCE=0
BUILD_EXE=1     # 默认就走编译 + 收集依赖这条主路径
WITH_SOURCE=0
SINGLE_EXE=0    # 单文件 exe 需 --single-exe 显式开启（Enigma Virtual Box）

# ------------------------------------------------------------------ 参数解析
usage() {
    awk 'NR>1 { if ($0 !~ /^#/) exit; print }' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        -v|--version)     VERSION="$2"; shift 2 ;;
        --no-build)       DO_BUILD=0; shift ;;
        --no-zip)         DO_ZIP=0; shift ;;
        --no-exe)         BUILD_EXE=0; shift ;;
        --with-source)    WITH_SOURCE=1; DO_ZIP=1; shift ;;
        --single-exe)     SINGLE_EXE=1; shift ;;
        --check-only)     CHECK_ONLY=1; shift ;;
        -e|--build-exe)   BUILD_EXE=1; shift ;;
        -f|--force)       FORCE=1; shift ;;
        -h|--help)        usage; exit 0 ;;
        *) echo "未知参数: $1（用 --help 查看用法）" >&2; exit 1 ;;
    esac
done

# ------------------------------------------------------------------ 平台判定
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    Darwin)               PLATFORM="darwin" ;;
    Linux)                PLATFORM="linux" ;;
    *)                    PLATFORM="unknown" ;;
esac

# ------------------------------------------------------------------ 颜色
if [ -t 1 ]; then
    C_STEP='\033[36m'; C_OK='\033[32m'; C_WARN='\033[33m'
    C_ERR='\033[31m';  C_DIM='\033[90m'; C_OFF='\033[0m'
else
    C_STEP=''; C_OK=''; C_WARN=''; C_ERR=''; C_DIM=''; C_OFF=''
fi

step()  { printf '\n%s=== %s ===%s\n' "$C_STEP" "$1" "$C_OFF"; }
ok()    { printf '  %s[OK]%s %s\n'   "$C_OK"   "$C_OFF" "$1"; }
info()  { printf '  %s%s%s\n'        "$C_DIM"  "$1"     "$C_OFF"; }
warn()  { printf '  %s[!]%s %s\n'    "$C_WARN" "$C_OFF" "$1"; }
err()   { printf '  %s[x]%s %s\n'    "$C_ERR"  "$C_OFF" "$1"; }

# --------------------------------------------------- 扫描源码最新修改时间
# 只扫源码目录，排除构建产物与运行期数据。
newest_source() {
    find "$PROJECT_DIR" \
        \( -name build -o -name 'build-*' -o -name 'cmake-build-*' \
           -o -name dist -o -name .git -o -name .idea -o -name .vscode \
           -o -name config -o -name log \) -prune -o \
        -type f -print 2>/dev/null |
    grep -Ev '/\.(git|idea|vscode)/' |
    grep -E "^$PROJECT_DIR/(main\.cpp|CMakeLists\.txt|CMakePresets\.json|core/|ui/|utils/|resources/|script/|README\.md$)" |
    while IFS= read -r f; do
        printf '%s\t%s\n' "$(stat -c %Y "$f" 2>/dev/null || stat -f %m "$f" 2>/dev/null || echo 0)" \
                          "${f#$PROJECT_DIR/}"
    done | sort -rn | head -1
}

# --------------------------------------------------------- 产物检测
show_build_status() {
    local dist="$PROJECT_DIR/dist"

    step "打包产物检测"

    if [ ! -d "$dist" ]; then
        warn "还没打过包：dist/ 目录不存在"
        return 1
    fi

    local newest
    newest="$(newest_source || true)"
    if [ -z "$newest" ]; then
        warn "没有找到任何源码文件，跳过检测"
        return 1
    fi

    local newest_epoch newest_file
    newest_epoch="$(printf '%s' "$newest" | cut -f1)"
    newest_file="$(printf '%s' "$newest" | cut -f2)"

    local fmt
    if date -d "@$newest_epoch" '+%Y-%m-%d %H:%M:%S' >/dev/null 2>&1; then
        fmt="$(date -d "@$newest_epoch" '+%Y-%m-%d %H:%M:%S')"
    else
        fmt="$(date -r "$newest_epoch" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "$newest_epoch")"
    fi
    info "源码最新改动: $fmt  ($newest_file)"

    local today_stamp all_fresh=1 today_count=0 found=0 skipped_count=0
    today_stamp="$(date '+%Y%m%d')"

    while IFS= read -r a; do
        [ -z "$a" ] && continue
        local name size_epoch size_bytes age_text size_h
        name="$(basename "$a")"
        size_bytes="$(stat -c %s "$a" 2>/dev/null || stat -f %z "$a" 2>/dev/null || echo 0)"
        size_epoch="$(stat -c %Y "$a" 2>/dev/null || stat -f %m "$a" 2>/dev/null || echo 0)"
        size_h="$(awk -v b="$size_bytes" 'BEGIN { printf "%.1f MB", b/1048576 }')"
        age_text="$(awk -v now="$(date +%s)" -v t="$size_epoch" 'BEGIN {
            d = now - t;
            if (d < 3600) printf "%.0f 分钟前", d/60;
            else if (d < 86400) printf "%.1f 小时前", d/3600;
            else printf "%.0f 天前", d/86400;
        }')"
        found=1

        # 产物归属：判过期的意义是"提醒我该重打了"。不属于自己的产物永远打不出来，
        # 算进来只会让自检每次都报过期，最后就没人看它了。
        #   本脚本产出 *.tar.gz 与 *_src.zip
        #   *.exe 与 *_win64.zip 是 package.ps1 的（Windows 专用）
        # 两个脚本在 Windows 下产出的是同一份东西（dist/AutoClickerQt.exe +
        # 依赖 + _win64.zip），所以不再按「谁产出的」跳过 —— 只有源码归档
        # 是 --with-source 才产，默认跳过。
        case "$name" in
            *_src.tar.gz|*_src.zip)
                if [ "${WITH_SOURCE:-0}" -eq 0 ]; then
                    printf '  %s[跳过]%s %s  (%s)  源码归档，需 --with-source 才会重建\n' \
                        "$C_DIM" "$C_OFF" "$name" "$size_h"
                    skipped_count=$((skipped_count + 1))
                    continue
                fi
                ;;
        esac

        # 文件名日期戳不是今天的视为历史归档，不参与判定
        local stamp
        stamp="$(printf '%s' "$name" | sed -n 's/.*_\([0-9]\{8\}\)[_.].*/\1/p')"
        if [ -n "$stamp" ] && [ "$stamp" != "$today_stamp" ]; then
            printf '  %s[归档]%s %s  (%s)  历史产物，不参与判定\n' "$C_DIM" "$C_OFF" "$name" "$size_h"
            continue
        fi

        today_count=$((today_count + 1))
        if [ "$size_epoch" -lt "$newest_epoch" ]; then
            printf '  %s[过期]%s %s  (%s, %s)\n' "$C_WARN" "$C_OFF" "$name" "$size_h" "$age_text"
            all_fresh=0
        else
            printf '  %s[最新]%s %s  (%s, %s)\n' "$C_OK" "$C_OFF" "$name" "$size_h" "$age_text"
        fi
    done <<EOF
$(find "$dist" -maxdepth 1 -type f \( -name '*.exe' -o -name '*.zip' -o -name '*.tar.gz' \) 2>/dev/null | sort -r)
EOF

    if [ "$found" -eq 0 ]; then
        warn "dist/ 里没有任何产物"
        return 1
    fi

    if [ "$today_count" -eq 0 ]; then
        if [ "$skipped_count" -gt 0 ]; then
            warn "dist/ 里只有别的脚本的产物，本脚本今天还没打过包 —— 需要打包。"
        else
            warn "dist/ 里没有今天（$today_stamp）的产物 —— 需要打包。"
        fi
        return 1
    fi

    echo ""
    if [ "$all_fresh" -eq 1 ]; then
        printf '%s结论：产物已是最新，无需重新打包。%s\n' "$C_OK" "$C_OFF"
        return 0
    else
        printf '%s结论：源码比产物新，建议重新打包。%s\n' "$C_WARN" "$C_OFF"
        return 1
    fi
}

# ------------------------------------------------------------------ 工具链探测

# Cygwin/MSYS 自带的 cmake 会把 D:\xxx 改写成 /cygdrive/d/xxx 再传给下游，
# 结果 CMake 报 "The source directory ... does not exist"。必须挑原生 cmake。
is_native_tool() {
    case "$1" in
        *cygwin*|*Cygwin*|*/msys*|*/MSYS*|*\\msys*|*/git/usr/bin/*) return 1 ;;
        *) return 0 ;;
    esac
}

find_native_cmake() {
    local cand
    # 1. Qt 自带的 CMake（装了 Qt 就一定有，且肯定是原生 Windows 版）
    for cand in "/e/Qt/Tools/CMake_64/bin/cmake.exe" \
                "/c/Qt/Tools/CMake_64/bin/cmake.exe" \
                "/d/Qt/Tools/CMake_64/bin/cmake.exe"; do
        [ -f "$cand" ] && { echo "$cand"; return 0; }
    done
    # 2. CMake 官方安装位置
    for cand in "/c/Program Files/CMake/bin/cmake.exe" \
                "/c/Program Files (x86)/CMake/bin/cmake.exe"; do
        [ -f "$cand" ] && { echo "$cand"; return 0; }
    done
    # 3. PATH 里第一个非 Cygwin/MSYS 的
    local p
    p="$(command -v cmake 2>/dev/null || true)"
    if [ -n "$p" ] && is_native_tool "$p"; then
        echo "$p"; return 0
    fi
    return 1
}

find_native_ninja() {
    local cand
    for cand in "/e/Qt/Tools/Ninja/ninja.exe" \
                "/c/Qt/Tools/Ninja/ninja.exe" \
                "/d/Qt/Tools/Ninja/ninja.exe"; do
        [ -f "$cand" ] && { echo "$cand"; return 0; }
    done
    local p
    p="$(command -v ninja 2>/dev/null || true)"
    if [ -n "$p" ] && is_native_tool "$p"; then
        echo "$p"; return 0
    fi
    return 1
}

# 没设 CMAKE_PREFIX_PATH 时，猜一个 Qt6 安装位置
guess_qt_prefix() {
    local root ver
    for root in /e/Qt /c/Qt /d/Qt "/c/Program Files/Qt" "/e/Program Files/Qt"; do
        [ -d "$root" ] || continue
        for ver in $(ls -1 "$root" 2>/dev/null | grep -E '^6\.[0-9]+(\.[0-9]+)?$' | sort -rV); do
            if [ -f "$root/$ver/mingw_64/lib/cmake/Qt6/Qt6Config.cmake" ]; then
                echo "$root/$ver/mingw_64"; return 0
            fi
            if [ -f "$root/$ver/msvc2022_64/lib/cmake/Qt6/Qt6Config.cmake" ]; then
                echo "$root/$ver/msvc2022_64"; return 0
            fi
        done
    done
    return 1
}

# ------------------------------------------------------------------ 编译
do_build() {
    step "编译 Release"

    local cmake_bin
    cmake_bin="$(find_native_cmake)"
    if [ -z "$cmake_bin" ]; then
        err "找不到原生 cmake（Cygwin/MSYS 里那个会把 D:\\ 路径改写成 /cygdrive/d/，不能用）"
        return 1
    fi
    info "cmake: $cmake_bin"

    local qt_prefix="${QT_PREFIX:-${CMAKE_PREFIX_PATH:-${QTDIR:-}}}"
    local build_dir="$PROJECT_DIR/build/release"
    local args=(-S "$PROJECT_DIR" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release)

    if [ -n "$qt_prefix" ]; then
        args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
        info "Qt: $qt_prefix"
    else
        local guessed
        if guessed="$(guess_qt_prefix)"; then
            qt_prefix="$guessed"
            args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
            info "Qt: $qt_prefix（自动探测）"
        else
            warn "CMAKE_PREFIX_PATH / QTDIR / QT_PREFIX 都没设置，CMake 可能找不到 Qt6"
        fi
    fi

    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        local ninja_bin
        if ninja_bin="$(find_native_ninja)"; then
            args+=(-G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja_bin")
            info "生成器: Ninja ($ninja_bin)"
        fi
    fi

    info "配置: $cmake_bin ${args[*]}"
    if ! "$cmake_bin" "${args[@]}" > /tmp/autoclickerqt_configure.log 2>&1; then
        err "配置失败"
        tail -40 /tmp/autoclickerqt_configure.log | sed 's/^/    /'
        mkdir -p "$PROJECT_DIR/build"
        mv /tmp/autoclickerqt_configure.log "$PROJECT_DIR/build/configure-error.log"
        err "完整日志: build/configure-error.log"
        return 1
    fi
    ok "配置成功"

    info "编译中..."
    if ! "$cmake_bin" --build "$build_dir" --config Release > /tmp/autoclickerqt_build.log 2>&1; then
        err "编译失败"
        tail -60 /tmp/autoclickerqt_build.log | sed 's/^/    /'
        mkdir -p "$PROJECT_DIR/build"
        mv /tmp/autoclickerqt_build.log "$PROJECT_DIR/build/build-error.log"
        err "完整日志: build/build-error.log"
        return 1
    fi
    tail -5 /tmp/autoclickerqt_build.log | sed 's/^/    /'
    ok "编译成功"
}

# ------------------------------------------------------------------ 打包
# 只打包源码，不含构建产物、运行期数据与 .git。
pack_source() {
    step "压缩源码包"

    local stamp dist tar_path
    stamp="$(date '+%Y%m%d')"
    dist="$PROJECT_DIR/dist"
    tar_path="$dist/AutoClickerQt_v${VERSION}_${stamp}_src.tar.gz"
    mkdir -p "$dist"

    local excludes=(
        --exclude=build --exclude='build-*' --exclude='cmake-build-*'
        --exclude=dist --exclude=.git --exclude=.idea --exclude=.vscode
        --exclude=config --exclude=log --exclude='__pycache__'
        --exclude='*.o' --exclude='*.obj' --exclude='*.exe' --exclude='*.dll'
    )

    tar -czf "$tar_path" "${excludes[@]}" -C "$PROJECT_DIR" . 2>/dev/null || {
        err "打包失败"
        return 1
    }

    local size
    size="$(awk -v b="$(stat -c %s "$tar_path" 2>/dev/null || stat -f %z "$tar_path")" 'BEGIN { printf "%.0f KB", b/1024 }')"
    ok "源码包: dist/$(basename "$tar_path") ($size)"

    # 有 zip 命令就额外出一个 zip（Windows 用户解压更方便）
    if command -v zip >/dev/null 2>&1; then
        local zip_path="$dist/AutoClickerQt_v${VERSION}_${stamp}_src.zip"
        rm -f "$zip_path"
        ( cd "$PROJECT_DIR" && zip -qr "$zip_path" . \
            -x 'build/*' 'build-*/*' 'cmake-build-*/*' 'dist/*' '.git/*' \
               '.idea/*' '.vscode/*' 'config/*' 'log/*' '*.o' '*.obj' '*.exe' '*.dll' ) || true
        if [ -f "$zip_path" ]; then
            ok "源码包: dist/$(basename "$zip_path")"
        fi
    fi
}

# --------------------------------------------------- 要求打 exe（仅 Windows）
build_exe() {
    if [ "$PLATFORM" != "windows" ]; then
        err "PyInstaller / windeployqt 都不能跨平台交叉编译。"
        err "当前平台: $PLATFORM。请到 Windows 上运行 script/package.ps1。"
        return 1
    fi

    step "收集 Qt 运行时依赖"

    local build_dir="$PROJECT_DIR/build/release"
    local dist="$PROJECT_DIR/dist"
    local exe=""
    for cand in "$build_dir/AutoClickerQt.exe" "$build_dir/Release/AutoClickerQt.exe"; do
        [ -f "$cand" ] && { exe="$cand"; break; }
    done
    if [ -z "$exe" ]; then
        err "编译产物里找不到 AutoClickerQt.exe"
        return 1
    fi
    info "可执行文件: $exe"

    local qt_prefix="${QT_PREFIX:-${CMAKE_PREFIX_PATH:-${QTDIR:-}}}"
    if [ -z "$qt_prefix" ] || [ ! -d "$qt_prefix" ]; then
        local guessed
        if guessed="$(guess_qt_prefix)"; then qt_prefix="$guessed"; fi
    fi
    local deployqt
    if [ -n "$qt_prefix" ] && [ -f "$qt_prefix/bin/windeployqt.exe" ]; then
        deployqt="$qt_prefix/bin/windeployqt.exe"
    elif command -v windeployqt >/dev/null 2>&1; then
        deployqt="$(command -v windeployqt)"
    else
        err "找不到 windeployqt，请设置 CMAKE_PREFIX_PATH 或 QT_PREFIX 指向 Qt6 安装目录"
        return 1
    fi

    rm -rf "$dist"
    mkdir -p "$dist"
    cp "$exe" "$dist/AutoClickerQt.exe"
    # cp 保留编译时的 mtime，自检会一直判「过期」。写成部署时间。
    touch "$dist/AutoClickerQt.exe"
    ok "已复制 exe 到 dist/"

    "$deployqt" --release --no-translations --no-system-d3d-compiler \
                --no-system-dxc-compiler --no-opengl-sw --compiler-runtime \
                "$dist/AutoClickerQt.exe" >/dev/null 2>&1 || \
        warn "windeployqt 返回非零，请检查依赖是否完整"

    # GIF 播放依赖 qgif.dll
    if [ -n "$qt_prefix" ] && [ -f "$qt_prefix/plugins/imageformats/qgif.dll" ] \
       && [ ! -f "$dist/imageformats/qgif.dll" ]; then
        mkdir -p "$dist/imageformats"
        cp "$qt_prefix/plugins/imageformats/qgif.dll" "$dist/imageformats/"
        info "补充 imageformats/qgif.dll"
    fi

    if [ ! -f "$dist/platforms/qwindows.dll" ]; then
        warn "缺少 platforms/qwindows.dll —— 运行时可能报找不到平台插件"
    fi
    ok "依赖收集完成"
}

# ================================================================ 单文件 exe（Enigma Virtual Box）

# 找 Enigma Virtual Box 的 enigmavbconsole.exe
find_enigmavb() {
    local cand
    if [ -n "${ENIGMAVB_DIR:-}" ] && [ -f "$ENIGMAVB_DIR/enigmavbconsole.exe" ]; then
        echo "$ENIGMAVB_DIR"; return 0
    fi
    for cand in "/e/Tools/EnigmaVB" "/c/Program Files/Enigma Virtual Box" \
                "/c/Program Files (x86)/Enigma Virtual Box" \
                "${LOCALAPPDATA:-}/Enigma Virtual Box" \
                "${PROGRAMFILES:-}/Enigma Virtual Box"; do
        if [ -n "$cand" ] && [ -f "$cand/enigmavbconsole.exe" ]; then
            echo "$cand"; return 0
        fi
    done
    if command -v enigmavbconsole.exe >/dev/null 2>&1; then
        dirname "$(command -v enigmavbconsole.exe)"; return 0
    fi
    return 1
}

# 把 MSYS 路径转成 Windows 原生路径（EVB 只认反斜杠形式）
win_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$1" 2>/dev/null || echo "$1"
    else
        # 没有 cygpath 时手工转换：/e/foo -> E:\foo
        local p="$1"
        if [[ "$p" =~ ^/([a-zA-Z])/(.*)$ ]]; then
            printf '%s:\\%s' "$(echo "${BASH_REMATCH[1]}" | tr '[:lower:]' '[:upper:]')" "${BASH_REMATCH[2]//\//\\}"
        else
            echo "$p"
        fi
    fi
}

# XML 转义（& 必须先换，否则会把后面换出来的 &amp; 再换一次）
xml_escape() {
    printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'
}

# 递归输出目录树节点到 .evb（与 package.ps1 的 New-EvbProjectFile 等价）。
# 关键：EVB 里文件的 <Name> 写成 "platforms\qwindows.dll" 是无效的 ——
# 它不会自动创建中间目录，结果被平铺到根目录，Qt 就找不到平台插件。
# 每一层目录都必须显式写一个 <Type>3</Type> 节点。
evb_emit_dir() {
    local dir="$1" prefix="$2" pad="$3" f name

    # 先输出本层的文件。
    # 主程序自身与归档包必须排除：把 AutoClickerQt.exe 当成虚拟文件再嵌一遍，
    # 会和真实入口冲突，跑起来直接访问违例（0xC0000005）。
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        name="$(basename "$f")"
        case "$name" in
            AutoClickerQt.exe|*.zip|*.tar.gz) continue ;;
        esac
        {
            printf '%s<File>\n' "$pad"
            printf '%s\t<Type>2</Type>\n' "$pad"
            printf '%s\t<Name>%s</Name>\n' "$pad" "$(xml_escape "$name")"
            printf '%s\t<File>%s</File>\n' "$pad" "$(xml_escape "$(win_path "$f")")"
            printf '%s\t<ActiveX>false</ActiveX>\n' "$pad"
            printf '%s\t<ActiveXInstall>false</ActiveXInstall>\n' "$pad"
            printf '%s\t<Action>0</Action>\n' "$pad"
            printf '%s\t<OverwriteDateTime>false</OverwriteDateTime>\n' "$pad"
            printf '%s\t<OverwriteAttributes>false</OverwriteAttributes>\n' "$pad"
            printf '%s\t<PassCommandLine>false</PassCommandLine>\n' "$pad"
            printf '%s</File>\n' "$pad"
        } >> "$EVB_FILE"
    done < <(find "$dir" -maxdepth 1 -type f | sort)

    # 再递归输出本层的子目录
    while IFS= read -r d; do
        [ -z "$d" ] && continue
        name="$(basename "$d")"
        {
            printf '%s<File>\n' "$pad"
            printf '%s\t<Type>3</Type>\n' "$pad"
            printf '%s\t<Name>%s</Name>\n' "$pad" "$(xml_escape "$name")"
            printf '%s\t<Action>0</Action>\n' "$pad"
            printf '%s\t<OverwriteDateTime>false</OverwriteDateTime>\n' "$pad"
            printf '%s\t<OverwriteAttributes>false</OverwriteAttributes>\n' "$pad"
            printf '%s\t<Files>\n' "$pad"
        } >> "$EVB_FILE"
        evb_emit_dir "$d" "$prefix$name/" "$pad\t\t"
        printf '%s\t</Files>\n%s</File>\n' "$pad" "$pad" >> "$EVB_FILE"
    done < <(find "$dir" -maxdepth 1 -mindepth 1 -type d | sort)
}

do_single_exe() {
    step "打包成单文件 exe"

    local dist="$PROJECT_DIR/dist"
    local exe="$dist/AutoClickerQt.exe"
    if [ ! -f "$exe" ]; then
        err "dist/AutoClickerQt.exe 不存在，先跑一次不带 --single-exe 的打包"
        return 1
    fi

    local evb_dir
    if ! evb_dir="$(find_enigmavb)"; then
        err "找不到 Enigma Virtual Box（enigmavbconsole.exe）"
        echo "    下载: https://enigmaprotector.com/en/downloads.html" >&2
        echo "    或用环境变量 ENIGMAVB_DIR 指向安装目录。" >&2
        echo "    不想装它也行 —— 不加 --single-exe 就是默认的绿色目录模式。" >&2
        return 1
    fi
    info "Enigma Virtual Box: $evb_dir"

    local tmp
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/autoclickerqt_evb.XXXXXX")"
    EVB_FILE="$tmp/project.evb"
    local out_exe="$(win_path "$tmp")/boxed.exe"

    # 待嵌入的文件：dist 里除主 exe 与源归档之外的全部内容
    local file_count
    file_count=$(find "$dist" -type f ! -name "AutoClickerQt.exe" ! -name "*.zip" ! -name "*.tar.gz" | wc -l | tr -d ' ')
    info "待嵌入 $file_count 个文件"

    {
        printf '<?xml version="utf-16"?>\n'
        printf '<>\n'
        printf '\t<InputFile>%s</InputFile>\n' "$(xml_escape "$(win_path "$exe")")"
        printf '\t<OutputFile>%s</OutputFile>\n' "$(xml_escape "$out_exe")"
        printf '\t<Files>\n'
        printf '\t\t<Enabled>true</Enabled>\n'
        printf '\t\t<DeleteExtractedOnExit>true</DeleteExtractedOnExit>\n'
        printf '\t\t<CompressFiles>true</CompressFiles>\n'
        printf '\t\t<Files>\n'
        printf '\t\t\t<File>\n'
        printf '\t\t\t\t<Type>3</Type>\n'
        printf '\t\t\t\t<Name>%%DEFAULT FOLDER%%</Name>\n'
        printf '\t\t\t\t<Action>0</Action>\n'
        printf '\t\t\t\t<OverwriteDateTime>false</OverwriteDateTime>\n'
        printf '\t\t\t\t<OverwriteAttributes>false</OverwriteAttributes>\n'
        printf '\t\t\t\t<Files>\n'
    } > "$EVB_FILE"

    evb_emit_dir "$dist" "" "\t\t\t\t\t"

    {
        printf '\t\t\t\t</Files>\n'
        printf '\t\t\t</File>\n'
        printf '\t\t</Files>\n'
        printf '\t</Files>\n'
        printf '\t<Registries><Enabled>false</Enabled><Registries/></Registries>\n'
        printf '\t<Packaging><Enabled>false</Enabled></Packaging>\n'
        printf '\t<Options>\n'
        printf '\t\t<ShareVirtualSystem>true</ShareVirtualSystem>\n'
        printf '\t\t<MapExecutableWithTemporaryFile>true</MapExecutableWithTemporaryFile>\n'
        printf '\t\t<AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles>\n'
        printf '\t</Options>\n'
        printf '</>\n'
    } >> "$EVB_FILE"

    # 必须转成 UTF-16 LE + BOM，否则 enigmavbconsole 会把文件当坏项目
    if command -v iconv >/dev/null 2>&1; then
        iconv -f UTF-8 -t UTF-16 "$EVB_FILE" > "$EVB_FILE.u16" && mv "$EVB_FILE.u16" "$EVB_FILE"
    else
        warn "没有 iconv，.evb 可能不是 UTF-16，Enigma VB 会忽略文件列表"
    fi

    local log="$tmp/evb.log"
    if ! "$evb_dir/enigmavbconsole.exe" "$(win_path "$EVB_FILE")" > "$log" 2>&1; then
        err "Enigma VB 打包失败"
        tail -n 20 "$log" >&2
        rm -rf "$tmp"
        return 1
    fi
    if [ ! -f "$tmp/boxed.exe" ]; then
        err "Enigma VB 没有产出文件"
        tail -n 20 "$log" >&2
        rm -rf "$tmp"
        return 1
    fi

    # 清理 dist 为单文件形态（config/ 与 log/ 保留，那是运行期数据）
    find "$dist" -mindepth 1 -maxdepth 1 \
        ! -name "AutoClickerQt.exe" ! -name "config" ! -name "log" \
        -exec rm -rf {} + 2>/dev/null || true
    cp "$tmp/boxed.exe" "$exe"
    touch "$exe"
    local size
    size=$(du -m "$exe" | cut -f1)
    ok "单文件 exe 生成成功（${size} MB）"
    info "dist/ 已清理为单文件形态（只剩 AutoClickerQt.exe）"

    rm -rf "$tmp"
    return 0
}

# ================================================================ 主流程

echo ""
printf '%sAutoClickerQt 打包脚本%s\n' "$C_STEP" "$C_OFF"
info "项目: $PROJECT_DIR"
info "平台: $PLATFORM"
info "版本: $VERSION"

if show_build_status; then
    BUILD_FRESH=1
else
    BUILD_FRESH=0
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
    if [ "$BUILD_FRESH" -eq 1 ]; then exit 0; else exit 2; fi
fi

if [ "$BUILD_FRESH" -eq 1 ] && [ "$FORCE" -eq 0 ]; then
    if [ -t 0 ]; then
        printf '\n产物已是最新，仍要重新打包吗？[y/N] '
        read -r answer
        case "$answer" in
            [yY]*) : ;;
            *) echo "已跳过打包。"; exit 0 ;;
        esac
    else
        info "非交互环境，继续打包（用 --check-only 可只检测）。"
    fi
fi

if [ "$DO_BUILD" -eq 1 ]; then
    if [ "$PLATFORM" = "windows" ]; then
        do_build || exit 1
    else
        step "跳过编译"
        warn "连点功能依赖 Win32 API，非 Windows 平台不编译（只有界面没有功能）"
    fi
else
    step "跳过编译（--no-build）"
fi

if [ "$BUILD_EXE" -eq 1 ]; then
    if [ "$PLATFORM" = "windows" ]; then
        build_exe || exit 1
    else
        step "跳过收集依赖"
        warn "非 Windows 平台无法产出可运行程序（当前平台: $PLATFORM）"
    fi
else
    step "跳过收集依赖（--no-exe）"
fi

if [ "$SINGLE_EXE" -eq 1 ]; then
    if [ "$PLATFORM" = "windows" ]; then
        do_single_exe || exit 1
    else
        step "跳过单文件打包"
        warn "单文件 exe 依赖 Enigma Virtual Box（仅 Windows），当前平台: $PLATFORM"
    fi
fi

if [ "$WITH_SOURCE" -eq 1 ] && [ "$DO_ZIP" -eq 1 ]; then
    pack_source || exit 1
else
    step "跳过源码归档"
    info "用 --with-source 可额外打一份"
fi

echo ""
printf '%s打包完成。%s\n' "$C_OK" "$C_OFF"
info "dist: $PROJECT_DIR/dist"
if [ -f "$PROJECT_DIR/dist/AutoClickerQt.exe" ]; then
    size="$(awk -v b="$(stat -c %s "$PROJECT_DIR/dist/AutoClickerQt.exe" 2>/dev/null || echo 0)" 'BEGIN { printf "%.1f MB", b/1048576 }')"
    if [ "$SINGLE_EXE" -eq 1 ]; then
        ok "单文件 exe: dist/AutoClickerQt.exe  ($size)"
    else
        ok "可执行文件: dist/AutoClickerQt.exe  ($size)"
    fi
fi
exit 0
