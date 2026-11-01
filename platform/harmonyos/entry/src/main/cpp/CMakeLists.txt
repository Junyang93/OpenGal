cmake_minimum_required(VERSION 3.16)
project(entry CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 仓库根目录(hvigor 通过 externalNativeOptions.arguments 传入; 独立调试时自动推断)
if(NOT DEFINED ENGINE_ROOT)
    get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)
endif()

# stb 头文件(与根 CMakeLists 保持同一锁定提交)
set(STB_COMMIT 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5)
set(STB_DIR ${CMAKE_CURRENT_BINARY_DIR}/stb)
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

add_library(entry SHARED
    napi_init.cpp
    ${ENGINE_ROOT}/engine/core/gal_script.cpp
    ${ENGINE_ROOT}/engine/core/opengal.cpp
    ${ENGINE_ROOT}/engine/render/gl_renderer.cpp
    ${ENGINE_ROOT}/engine/audio/null_audio.cpp
    ${ENGINE_ROOT}/engine/audio/opensl_audio.cpp
)

target_include_directories(entry PRIVATE
    ${ENGINE_ROOT}/engine/include
    ${ENGINE_ROOT}/engine/core
    ${STB_DIR}
)

target_compile_definitions(entry PRIVATE OGAL_GLES2 __OHOS__)

find_library(lib_ace      ace_ndk.z)
find_library(lib_napi     ace_napi.z)
find_library(lib_egl      EGL)
find_library(lib_gles     GLESv2)
find_library(lib_opensl   OpenSLES)
find_library(lib_rawfile  rawfile_manager)

target_link_libraries(entry PRIVATE
    ${lib_ace} ${lib_napi} ${lib_egl} ${lib_gles} ${lib_opensl} ${lib_rawfile}
)
