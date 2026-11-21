cmake_minimum_required(VERSION 3.16)

project(AutoClickerQt
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

qt_add_executable(AutoClickerQt
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${PROJECT_RESOURCES}
)

target_include_directories(AutoClickerQt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

target_link_libraries(AutoClickerQt PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
)

if (WIN32)
    # 发布版不弹控制台窗口；日志仍然写入 log/ 目录
    set_target_properties(AutoClickerQt PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(AutoClickerQt PRIVATE user32)
    target_compile_definitions(AutoClickerQt PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()

if (MSVC)
    target_compile_options(AutoClickerQt PRIVATE /utf-8 /W4)
else ()
    target_compile_options(AutoClickerQt PRIVATE -Wall -Wextra)
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
        add_custom_command(TARGET AutoClickerQt POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:AutoClickerQt>/platforms/")
        add_custom_command(TARGET AutoClickerQt POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:AutoClickerQt>/platforms/")
    endif ()

    foreach (QT_LIB Core Gui Widgets)
        if (EXISTS "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll")
            add_custom_command(TARGET AutoClickerQt POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                    "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                    "$<TARGET_FILE_DIR:AutoClickerQt>")
        endif ()
    endforeach ()
endif ()

qt_finalize_executable(AutoClickerQt)
