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

# stb (nur stb_vorbis.c, Ogg-Dekoder für Musik und Sprache)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d)
FetchContent_MakeAvailable(stb)

# Alle Programme und DLLs nach build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(tools/hfpak)
add_subdirectory(src/common)
add_subdirectory(src/host)

# Launcher (Qt 6), nur wenn Qt gefunden wird: CMAKE_PREFIX_PATH, QT6_DIR
# oder eine Standardinstallation unter C:/Qt/6.*/msvc2022_64
option(TS2_LAUNCHER "Launcher bauen (Qt 6)" ON)
if(TS2_LAUNCHER)
  file(GLOB _qt_roots "C:/Qt/6.*/msvc2022_64")
  list(APPEND CMAKE_PREFIX_PATH ${_qt_roots} $ENV{QT6_DIR})
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    add_subdirectory(src/launcher)
  else()
    message(STATUS "Qt 6 nicht gefunden: Launcher wird nicht gebaut")
  endif()
endif()
