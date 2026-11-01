cmake_minimum_required(VERSION 3.16)
project(OpenGal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- stb 头文件依赖(构建时下载, 锁定提交) ----
set(STB_COMMIT 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5)
set(STB_DIR ${CMAKE_BINARY_DIR}/stb)
file(MAKE_DIRECTORY ${STB_DIR})
foreach(h stb_image.h stb_truetype.h)
  if(NOT EXISTS ${STB_DIR}/${h})
    file(DOWNLOAD
      https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/${h}
      ${STB_DIR}/${h} TLS_VERIFY ON STATUS stb_status
      TIMEOUT 60 INACTIVITY_TIMEOUT 20 RETRY_COUNT 3 RETRY_DELAY 3)
    list(GET stb_status 0 stb_code)
    if(NOT stb_code EQUAL 0)
      message(FATAL_ERROR "下载 stb/${h} 失败: ${stb_status}")
    endif()
  endif()
endforeach()

# ============ 引擎核心静态库(平台无关) ============
add_library(opengal STATIC
    engine/core/gal_script.cpp
    engine/core/opengal.cpp
    engine/render/gl_renderer.cpp
    engine/audio/null_audio.cpp
    engine/audio/opensl_audio.cpp
)
target_include_directories(opengal
    PUBLIC  engine/include
    PRIVATE engine/core ${STB_DIR}
)
if(WIN32)
    target_link_libraries(opengal PUBLIC opengl32)
endif()
# MSVC 下源文件为 UTF-8(无 BOM): 统一按 UTF-8 解析, 保证中文字符串正确
if(MSVC)
    target_compile_options(opengal PUBLIC /utf-8)
endif()

# ============ Windows 演示程序 ============
if(WIN32)
    add_executable(galdemo WIN32 platform/win32/main.cpp)
    target_link_libraries(galdemo PRIVATE opengal)
    # 演示程序工作目录设为仓库根, 使其能找到 games/demo
    set_target_properties(galdemo PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# ============ VM 冒烟测试(无渲染, CI 可运行) ============
enable_testing()
add_executable(galtest tests/vm_smoke.cpp engine/core/gal_script.cpp)
target_include_directories(galtest PRIVATE engine/core)
if(MSVC)
    target_compile_options(galtest PRIVATE /utf-8)
endif()
add_test(NAME vm_smoke COMMAND galtest ${CMAKE_SOURCE_DIR}/games/demo/script/main.gal)

# Android 构建见 platform/android/app/src/main/cpp/CMakeLists.txt
# HarmonyOS 构建见 platform/harmonyos/entry/src/main/cpp/CMakeLists.txt
