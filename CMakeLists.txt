cmake_minimum_required(VERSION 3.16)

project(AutoClicker
        VERSION 1.0.0
        DESCRIPTION "Windows 自动连点器 (Qt6 / C++ 版)"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 让 moc/uic/rcc 的中间产物集中在 build 目录，不污染源码树
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

qt_standard_project_setup()

# ---------------------------------------------------------------- 源文件清单
set(PROJECT_SOURCES
        main.cpp

        # utils
        utils/app_paths.cpp
        utils/app_paths.h
        utils/config_manager.cpp
        utils/config_manager.h
        utils/logger.cpp
        utils/logger.h

        # core
        core/click_engine.cpp
        core/click_engine.h
        core/hotkey_listener.cpp
        core/hotkey_listener.h

        # ui
        ui/styles.cpp
        ui/styles.h
        ui/action_editor.cpp
        ui/action_editor.h
        ui/process_picker.cpp
        ui/process_picker.h
        ui/main_window.cpp
        ui/main_window.h
)

set(PROJECT_RESOURCES
        resources/resources.qrc
)

# ------------------------------------------------------- exe 图标与版本信息
# 没有 .rc 的话，Windows 会给 exe 套上编译器/Qt 的默认图标（一个蓝色方块），
# 资源管理器里看着像没做完。.qrc 只影响程序自己画的窗口图标，
# 影响不到 exe 文件本身的图标 —— 那个必须由资源脚本嵌进 PE。
if (WIN32)
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/static/Ralsei.ico")
    if (NOT EXISTS "${APP_ICON_PATH}")
        message(WARNING "找不到图标文件: ${APP_ICON_PATH}")
    endif ()

    # .rc 里写不进 Windows 反斜杠路径（会被当转义），统一用正斜杠
    file(TO_CMAKE_PATH "${APP_ICON_PATH}" APP_ICON_PATH_FWD)
    set(APP_ICON_PATH "${APP_ICON_PATH_FWD}")

    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
            @ONLY
    )
    set(APP_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    # .rc 是 UTF-8，但 rc/windres 默认按系统 ANSI 码页（中文机器上是 936）读，
    # 不显式指定就会把「自动连点器」编译成乱码。两个编译器参数写法不同。
    if (MSVC)
        set_source_files_properties("${APP_RC_FILE}" PROPERTIES COMPILE_FLAGS "/c 65001")
    else ()
        set_source_files_properties("${APP_RC_FILE}" PROPERTIES COMPILE_FLAGS "-c 65001")
    endif ()
    list(APPEND PROJECT_SOURCES "${APP_RC_FILE}")
endif ()

qt_add_executable(AutoClicker
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${PROJECT_RESOURCES}
)

target_include_directories(AutoClicker PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

target_link_libraries(AutoClicker PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
)

if (WIN32)
    # 发布版不弹控制台窗口；日志仍然写入 log/ 目录
    set_target_properties(AutoClicker PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(AutoClicker PRIVATE user32)
    target_compile_definitions(AutoClicker PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()

if (MSVC)
    target_compile_options(AutoClicker PRIVATE /utf-8 /W4)
else ()
    target_compile_options(AutoClicker PRIVATE -Wall -Wextra)
endif ()

# ------------------------------------------------- Qt 运行时 DLL 拷贝（Windows）
if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()

    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()

    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET AutoClicker POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:AutoClicker>/platforms/")
        add_custom_command(TARGET AutoClicker POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:AutoClicker>/platforms/")
    endif ()

    foreach (QT_LIB Core Gui Widgets)
        if (EXISTS "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll")
            add_custom_command(TARGET AutoClicker POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                    "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                    "$<TARGET_FILE_DIR:AutoClicker>")
        endif ()
    endforeach ()
endif ()

# ------------------------------------------------------------ 自动打包目标
# 直接把打包接进构建系统，不用再切到终端手敲脚本：
#   cmake --build build/release --target deploy          只收集运行时依赖到 dist/
#   cmake --build build/release --target package         出绿色目录 zip + 单文件 exe
#   cmake --build build/release --target package-green   只出绿色目录 zip
#   cmake --build build/release --target package-single  只出单文件 exe
#
# 之所以转调 script/package.ps1 而不是在这里重写一遍 windeployqt：
# 脚本里已经处理了 dxcompiler 排除、qgif.dll 补全、platforms 校验、EnigmaVB 打包
# 这些细节，两边各写一份迟早会发散。
option(AUTOCLICKER_ENABLE_PACKAGE "注册 deploy / package 自定义构建目标" ON)

if (AUTOCLICKER_ENABLE_PACKAGE)
    if (WIN32)
        find_program(AUTOCLICKER_POWERSHELL NAMES pwsh powershell)
        if (NOT AUTOCLICKER_POWERSHELL)
            message(WARNING "找不到 PowerShell，deploy/package 目标不会注册")
        else ()
            set(_pkg_cmd
                    "${AUTOCLICKER_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/script/package.ps1"
                    -Version "${PROJECT_VERSION}"
                    -BuildDir "${CMAKE_BINARY_DIR}"
                    -Force)

            # deploy：只做 windeployqt + 铺到 dist/，不压缩（调试用，几秒完成）
            add_custom_target(deploy
                    COMMAND ${_pkg_cmd} -SkipBuild -SkipZip -SkipSingle
                    DEPENDS AutoClicker
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    COMMENT "windeployqt：把 exe 与 Qt 运行时依赖铺到 dist/"
                    VERBATIM)

            # package：默认两种形态都出（绿色目录 zip + 单文件 exe，需要 EnigmaVB）
            add_custom_target(package
                    COMMAND ${_pkg_cmd}
                    DEPENDS AutoClicker
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    COMMENT "打包：绿色目录 zip + 单文件 exe"
                    VERBATIM)

            add_custom_target(package-green
                    COMMAND ${_pkg_cmd} -SkipSingle
                    DEPENDS AutoClicker
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    COMMENT "打包：只出绿色目录 zip"
                    VERBATIM)

            add_custom_target(package-single
                    COMMAND ${_pkg_cmd} -SingleExe
                    DEPENDS AutoClicker
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    COMMENT "打包：只出单文件 exe"
                    VERBATIM)
        endif ()
    else ()
        # 非 Windows：没有 windeployqt，只把二进制打成一个 tar.gz。
        # 运行时依赖由系统或 CI 提供（见 .github/workflows/build.yml 的 linux job）。
        set(_linux_pkg "AutoClicker_${PROJECT_VERSION}_linux64.tar.gz")
        add_custom_target(package
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist/stage"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:AutoClicker>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/dist/stage/AutoClicker"
                COMMAND ${CMAKE_COMMAND} -E tar czf
                        "${CMAKE_CURRENT_SOURCE_DIR}/dist/${_linux_pkg}"
                        --format=gnutar
                        AutoClicker
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/stage"
                DEPENDS AutoClicker
                COMMENT "打包：${_linux_pkg}"
                VERBATIM)
        add_custom_target(deploy
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:AutoClicker>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/"
                DEPENDS AutoClicker
                COMMENT "复制可执行文件到 dist/"
                VERBATIM)
    endif ()
endif ()

qt_finalize_executable(AutoClicker)
