#!/bin/bash
# AutoClickerQt 打包脚本（Linux / macOS / MSYS2）
#
# 用途：编译 Release 版本并产出源码压缩包。
#
# 注意：连点功能依赖 Win32 API（SendInput / PostMessage），本脚本在非 Windows
# 平台上只能编译、不能产出可用的可执行文件。传 --build-exe 会被明确拒绝。
#
# 用法：
#   ./script/package.sh                     # 编译并打包
#   ./script/package.sh --no-build          # 跳过编译，只打包源码
#   ./script/package.sh --version 1.2.0     # 指定版本号
#   ./script/package.sh --check-only        # 只检测，不打包（exit 0=最新 / 2=过期）
#   ./script/package.sh --build-exe         # Windows(MSYS2) 下要求打 exe
#   ./script/package.sh --force             # 产物最新也强制重打
#   ./script/package.sh --help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION="1.0.0"
DO_BUILD=1
DO_ZIP=1
CHECK_ONLY=0
FORCE=0
BUILD_EXE=0

# ------------------------------------------------------------------ 参数解析
usage() {
    awk 'NR>1 { if ($0 !~ /^#/) exit; print }' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        -v|--version)     VERSION="$2"; shift 2 ;;
        --no-build)       DO_BUILD=0; shift ;;
        --no-zip)         DO_ZIP=0; shift ;;
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
        case "$name" in
            *.exe|*_win64.zip)
                printf '  %s[跳过]%s %s  (%s)  package.ps1 的产物\n' "$C_DIM" "$C_OFF" "$name" "$size_h"
                skipped_count=$((skipped_count + 1))
                continue
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

# ------------------------------------------------------------------ 编译
do_build() {
    step "编译 Release"

    local cmake_bin
    cmake_bin="$(command -v cmake || true)"
    if [ -z "$cmake_bin" ]; then
        err "找不到 cmake，请先安装"
        return 1
    fi
    info "cmake: $cmake_bin"

    local qt_prefix="${CMAKE_PREFIX_PATH:-${QTDIR:-}}"
    local build_dir="$PROJECT_DIR/build/release"
    local args=(-S "$PROJECT_DIR" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release)

    if [ -n "$qt_prefix" ]; then
        args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
        info "Qt: $qt_prefix"
    else
        warn "CMAKE_PREFIX_PATH / QTDIR 都没设置，CMake 可能找不到 Qt6"
    fi

    if [ ! -f "$build_dir/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
        args+=(-G Ninja)
        info "生成器: Ninja"
    fi

    info "配置: cmake ${args[*]}"
    if ! cmake "${args[@]}" > /tmp/autoclickerqt_configure.log 2>&1; then
        err "配置失败"
        tail -40 /tmp/autoclickerqt_configure.log | sed 's/^/    /'
        mkdir -p "$PROJECT_DIR/build"
        mv /tmp/autoclickerqt_configure.log "$PROJECT_DIR/build/configure-error.log"
        err "完整日志: build/configure-error.log"
        return 1
    fi
    ok "配置成功"

    info "编译中..."
    if ! cmake --build "$build_dir" --config Release > /tmp/autoclickerqt_build.log 2>&1; then
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

    local qt_prefix="${CMAKE_PREFIX_PATH:-${QTDIR:-}}"
    local deployqt
    if command -v windeployqt >/dev/null 2>&1; then
        deployqt="$(command -v windeployqt)"
    elif [ -n "$qt_prefix" ] && [ -f "$qt_prefix/bin/windeployqt.exe" ]; then
        deployqt="$qt_prefix/bin/windeployqt.exe"
    else
        err "找不到 windeployqt，请设置 CMAKE_PREFIX_PATH 指向 Qt6 安装目录"
        return 1
    fi

    rm -rf "$dist"
    mkdir -p "$dist"
    cp "$exe" "$dist/AutoClickerQt.exe"
    ok "已复制 exe 到 dist/"

    "$deployqt" --release --no-translations --no-system-d3d-compiler \
                --no-opengl-sw --compiler-runtime "$dist/AutoClickerQt.exe" >/dev/null 2>&1 || \
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
    build_exe || exit 1
fi

if [ "$DO_ZIP" -eq 1 ]; then
    pack_source || exit 1
fi

echo ""
printf '%s打包完成。%s\n' "$C_OK" "$C_OFF"
info "dist: $PROJECT_DIR/dist"
exit 0
