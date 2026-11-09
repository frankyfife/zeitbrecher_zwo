cmake_minimum_required(VERSION 3.24)
project(zeitbrecher_zwo LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "zeitbrecher_zwo targets 64-bit Windows.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# zlib (raw deflate in the paks)
FetchContent_Declare(zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG v1.3.1
  GIT_SHALLOW TRUE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
target_include_directories(zlibstatic PUBLIC ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})

# Brotli (pak method 11)
FetchContent_Declare(brotli
  GIT_REPOSITORY https://github.com/google/brotli.git
  GIT_TAG v1.1.0
  GIT_SHALLOW TRUE)
set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(BROTLI_BUNDLED_MODE ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(brotli)

add_subdirectory(tools/hfpak)
