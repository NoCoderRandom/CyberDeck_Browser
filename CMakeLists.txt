cmake_minimum_required(VERSION 3.24)

project(
    CyberDeckBrowser
    VERSION 0.1.0
    DESCRIPTION "Native C++20 browser shell using CEF and OpenGL Deck Space"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CYBERDECK_REQUIRE_CEF "Fail configure if CEF_ROOT is not provided or invalid." OFF)
set(CEF_ROOT "" CACHE PATH "Path to an extracted CEF binary distribution.")

function(cyberdeck_configure_target target)
    target_include_directories(${target} PRIVATE src)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
        set_property(
            TARGET ${target}
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(CyberDeckPlatform STATIC
    src/common/Platform.h
)

if(WIN32)
    target_sources(CyberDeckPlatform PRIVATE
        src/platform/windows/Platform.cpp
    )
    target_compile_definitions(CyberDeckPlatform
        PUBLIC
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
elseif(UNIX AND NOT APPLE)
    target_sources(CyberDeckPlatform PRIVATE
        src/platform/linux/Platform.cpp
    )
    target_compile_definitions(CyberDeckPlatform
        PUBLIC
            CYBERDECK_PLATFORM_LINUX=1
    )
else()
    message(FATAL_ERROR "Unsupported platform. CyberDeck Browser currently supports Windows and Linux builds.")
endif()

target_include_directories(CyberDeckPlatform PUBLIC src)
cyberdeck_configure_target(CyberDeckPlatform)

add_library(CyberDeckCore STATIC
    src/browser/BrowserTypes.h
    src/browser/UrlNavigation.cpp
    src/browser/UrlNavigation.h
    src/common/AppInfo.cpp
    src/common/AppInfo.h
    src/common/Logger.cpp
    src/common/Logger.h
    src/deck/BookmarkNode.cpp
    src/deck/BookmarkNode.h
    src/deck/BookmarkStore.cpp
    src/deck/BookmarkStore.h
    src/deck/FaviconStore.cpp
    src/deck/FaviconStore.h
    src/history/HistoryStore.cpp
    src/history/HistoryStore.h
    src/render/DeckAnimation.cpp
    src/render/DeckAnimation.h
    src/render/DeckGeometry.cpp
    src/render/DeckGeometry.h
    src/render/DeckLayout.cpp
    src/render/DeckLayout.h
    src/settings/SettingsStore.cpp
    src/settings/SettingsStore.h
)

target_include_directories(CyberDeckCore PUBLIC src)
target_link_libraries(CyberDeckCore PUBLIC CyberDeckPlatform)
cyberdeck_configure_target(CyberDeckCore)

if(WIN32)
    add_executable(CyberDeckBrowser WIN32
        src/app/Application.cpp
        src/app/Application.h
        src/browser/BrowserHost.cpp
        src/browser/BrowserHost.h
        src/common/Platform.h
        src/deck/DeckSpaceController.cpp
        src/deck/DeckSpaceController.h
        src/main/MainWindow.cpp
        src/main/MainWindow.h
        src/main/WinMain.cpp
        src/render/GpuPreference.cpp
        src/render/NeonMaterial.cpp
        src/render/NeonMaterial.h
        src/render/OpenGLDeckView.cpp
        src/render/OpenGLDeckView.h
        src/ui/Theme.cpp
        src/ui/Theme.h
    )

    target_link_libraries(CyberDeckBrowser
        PRIVATE
            CyberDeckCore
            CyberDeckPlatform
            dwmapi
            gdi32
            ole32
            opengl32
            shell32
            user32
    )
    cyberdeck_configure_target(CyberDeckBrowser)

    if(MINGW)
        target_link_options(CyberDeckBrowser PRIVATE -municode)
    endif()

    if(CEF_ROOT)
        cmake_path(ABSOLUTE_PATH CEF_ROOT NORMALIZE OUTPUT_VARIABLE CYBERDECK_CEF_ROOT)
        set(CYBERDECK_CEF_INCLUDE_DIR "${CYBERDECK_CEF_ROOT}/include")
        set(CYBERDECK_CEF_CMAKE_DIR "${CYBERDECK_CEF_ROOT}/cmake")
        set(CYBERDECK_CEF_WRAPPER_DIR "${CYBERDECK_CEF_ROOT}/libcef_dll")
        set(CYBERDECK_CEF_ROOT_VALID FALSE)

        if(IS_DIRECTORY "${CYBERDECK_CEF_ROOT}"
            AND EXISTS "${CYBERDECK_CEF_INCLUDE_DIR}/cef_version.h"
            AND EXISTS "${CYBERDECK_CEF_CMAKE_DIR}/FindCEF.cmake"
            AND EXISTS "${CYBERDECK_CEF_WRAPPER_DIR}/CMakeLists.txt")
            set(CYBERDECK_CEF_ROOT_VALID TRUE)
        endif()

        if(CYBERDECK_CEF_ROOT_VALID)
            if(MINGW)
                if(CYBERDECK_REQUIRE_CEF)
                    message(FATAL_ERROR "Official Windows CEF binary distributions require MSVC-compatible builds. Reconfigure with Visual Studio 2022 or Ninja from an MSVC developer shell.")
                endif()
                message(WARNING "CEF_ROOT is valid, but the current MinGW generator cannot link official Windows CEF binaries. Building placeholder mode.")
            else()
                list(APPEND CMAKE_MODULE_PATH "${CYBERDECK_CEF_CMAKE_DIR}")
                find_package(CEF REQUIRED)

                ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
                add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

                target_include_directories(CyberDeckBrowser SYSTEM PRIVATE "${CYBERDECK_CEF_ROOT}")
                target_compile_definitions(CyberDeckBrowser PRIVATE CYBERDECK_HAS_CEF=1)
                add_dependencies(CyberDeckBrowser libcef_dll_wrapper)
                target_link_libraries(CyberDeckBrowser PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

                add_custom_command(
                    TARGET CyberDeckBrowser
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "$<IF:$<CONFIG:Debug>,${CEF_BINARY_DIR_DEBUG},${CEF_BINARY_DIR_RELEASE}>"
                        "$<TARGET_FILE_DIR:CyberDeckBrowser>"
                    COMMENT "Copying CEF runtime binaries"
                )

                if(EXISTS "${CYBERDECK_CEF_ROOT}/Resources")
                    add_custom_command(
                        TARGET CyberDeckBrowser
                        POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CYBERDECK_CEF_ROOT}/Resources"
                            "$<TARGET_FILE_DIR:CyberDeckBrowser>"
                        COMMENT "Copying CEF resource files"
                    )
                endif()

                message(STATUS "CEF integration enabled from: ${CYBERDECK_CEF_ROOT}")
            endif()
        elseif(CYBERDECK_REQUIRE_CEF)
            message(FATAL_ERROR "CEF_ROOT is set but does not look like a standard CEF binary distribution: ${CYBERDECK_CEF_ROOT}")
        else()
            message(WARNING "CEF_ROOT is set but does not look like a standard CEF binary distribution: ${CYBERDECK_CEF_ROOT}")
        endif()
    elseif(CYBERDECK_REQUIRE_CEF)
        message(FATAL_ERROR "CEF_ROOT is required. Provide -DCEF_ROOT=<path-to-cef-binary>.")
    else()
        message(STATUS "CEF_ROOT not set. Building Windows scaffold without CEF headers for now.")
    endif()
elseif(UNIX AND NOT APPLE)
    add_executable(CyberDeckBrowserLinux
        src/platform/linux/LinuxMain.cpp
    )
    target_link_libraries(CyberDeckBrowserLinux PRIVATE CyberDeckCore)
    set_target_properties(CyberDeckBrowserLinux PROPERTIES OUTPUT_NAME cyberdeck-browser)
    cyberdeck_configure_target(CyberDeckBrowserLinux)

    find_package(OpenGL REQUIRED COMPONENTS EGL OpenGL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl)

    add_executable(CyberDeckWsl2OpenGLProbe
        src/platform/linux/Wsl2OpenGLProbe.cpp
    )
    target_link_libraries(CyberDeckWsl2OpenGLProbe PRIVATE OpenGL::EGL OpenGL::OpenGL PkgConfig::WAYLAND)
    set_target_properties(CyberDeckWsl2OpenGLProbe PROPERTIES OUTPUT_NAME cyberdeck-wsl2-opengl-probe)
    cyberdeck_configure_target(CyberDeckWsl2OpenGLProbe)

    if(CEF_ROOT)
        cmake_path(ABSOLUTE_PATH CEF_ROOT NORMALIZE OUTPUT_VARIABLE CYBERDECK_CEF_ROOT)
        set(CYBERDECK_CEF_INCLUDE_DIR "${CYBERDECK_CEF_ROOT}/include")
        set(CYBERDECK_CEF_CMAKE_DIR "${CYBERDECK_CEF_ROOT}/cmake")
        set(CYBERDECK_CEF_WRAPPER_DIR "${CYBERDECK_CEF_ROOT}/libcef_dll")

        if(EXISTS "${CYBERDECK_CEF_INCLUDE_DIR}/cef_version.h"
            AND EXISTS "${CYBERDECK_CEF_CMAKE_DIR}/FindCEF.cmake"
            AND EXISTS "${CYBERDECK_CEF_WRAPPER_DIR}/CMakeLists.txt")
            list(APPEND CMAKE_MODULE_PATH "${CYBERDECK_CEF_CMAKE_DIR}")
            set(USE_SANDBOX OFF CACHE BOOL "Disable CEF sandbox for the separated Linux host." FORCE)
            find_package(CEF REQUIRED)

            ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
            add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper_linux)

            add_executable(CyberDeckBrowserLinuxCef
                src/platform/linux/LinuxCefMain.cpp
            )
            target_include_directories(CyberDeckBrowserLinuxCef SYSTEM PRIVATE "${CYBERDECK_CEF_ROOT}")
            target_compile_definitions(CyberDeckBrowserLinuxCef PRIVATE CYBERDECK_HAS_CEF=1)
            target_link_libraries(CyberDeckBrowserLinuxCef PRIVATE CyberDeckCore libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})
            set_target_properties(CyberDeckBrowserLinuxCef
                PROPERTIES
                    OUTPUT_NAME cyberdeck-browser-cef
                    BUILD_RPATH "$ORIGIN"
                    INSTALL_RPATH "$ORIGIN"
            )
            cyberdeck_configure_target(CyberDeckBrowserLinuxCef)

            if(CEF_BINARY_DIR_RELEASE)
                add_custom_command(
                    TARGET CyberDeckBrowserLinuxCef
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${CEF_BINARY_DIR_RELEASE}"
                        "$<TARGET_FILE_DIR:CyberDeckBrowserLinuxCef>"
                    COMMENT "Copying Linux CEF runtime binaries"
                )
            endif()

            if(EXISTS "${CYBERDECK_CEF_ROOT}/Resources")
                add_custom_command(
                    TARGET CyberDeckBrowserLinuxCef
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${CYBERDECK_CEF_ROOT}/Resources"
                        "$<TARGET_FILE_DIR:CyberDeckBrowserLinuxCef>"
                    COMMENT "Copying Linux CEF resource files"
                )
            endif()

            message(STATUS "Linux CEF target enabled: CyberDeckBrowserLinuxCef from ${CYBERDECK_CEF_ROOT}")
        elseif(CYBERDECK_REQUIRE_CEF)
            message(FATAL_ERROR "CEF_ROOT is set but does not look like a standard Linux CEF binary distribution: ${CYBERDECK_CEF_ROOT}")
        else()
            message(WARNING "CEF_ROOT is set but does not look like a standard Linux CEF binary distribution: ${CYBERDECK_CEF_ROOT}")
        endif()
    elseif(CYBERDECK_REQUIRE_CEF)
        message(FATAL_ERROR "CEF_ROOT is required. Provide -DCEF_ROOT=<path-to-linux-cef-binary>.")
    endif()

    message(STATUS "Linux platform target enabled: CyberDeckBrowserLinux")
endif()

enable_testing()

set(CYBERDECK_TESTS
    CyberDeckUrlNavigationTests tests/UrlNavigationTests.cpp
    CyberDeckHistoryStoreTests tests/HistoryStoreTests.cpp
    CyberDeckSettingsStoreTests tests/SettingsStoreTests.cpp
    CyberDeckLoggerTests tests/LoggerTests.cpp
    CyberDeckDeckGeometryTests tests/DeckGeometryTests.cpp
    CyberDeckDeckAnimationTests tests/DeckAnimationTests.cpp
    CyberDeckDeckLayoutTests tests/DeckLayoutTests.cpp
    CyberDeckBookmarkNodeTests tests/BookmarkNodeTests.cpp
    CyberDeckBookmarkStoreTests tests/BookmarkStoreTests.cpp
    CyberDeckFaviconStoreTests tests/FaviconStoreTests.cpp
)

list(LENGTH CYBERDECK_TESTS CYBERDECK_TESTS_LENGTH)
math(EXPR CYBERDECK_TESTS_LAST_INDEX "${CYBERDECK_TESTS_LENGTH} - 1")
foreach(index RANGE 0 ${CYBERDECK_TESTS_LAST_INDEX} 2)
    math(EXPR source_index "${index} + 1")
    list(GET CYBERDECK_TESTS ${index} test_target)
    list(GET CYBERDECK_TESTS ${source_index} test_source)

    add_executable(${test_target} ${test_source})
    target_link_libraries(${test_target} PRIVATE CyberDeckCore)
    cyberdeck_configure_target(${test_target})
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()
