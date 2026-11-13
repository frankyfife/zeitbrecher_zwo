cmake_minimum_required(VERSION 3.24)
project(zeitbrecher_zwo LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "zeitbrecher_zwo targets 64-bit Windows.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# Abhängigkeiten: Quellarchiv eines festen Commits, geprüft per SHA-256
# (docs/einrichten.md). Archive statt Git-Klon: ein Commit lässt sich nicht
# flach klonen, und der volle Klon von Brotli dauert lange.
set(_gh https://github.com)

# zlib v1.3.1 (raw deflate in the paks)
FetchContent_Declare(zlib
  URL ${_gh}/madler/zlib/archive/51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf.tar.gz
  URL_HASH SHA256=d9e270d46252734aa49770fbc544125391617956266f220bd63216c834f3a522)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
target_include_directories(zlibstatic PUBLIC ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})

# Brotli v1.1.0 (pak method 11)
FetchContent_Declare(brotli
  URL ${_gh}/google/brotli/archive/ed738e842d2fbdf2d6459e39267a633c4a9b2f5d.tar.gz
  URL_HASH SHA256=aaa739962a45b508b2e783b915e6b2b57ed3b12bd4b0feac73acfb144dffa54f)
set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(BROTLI_BUNDLED_MODE ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(brotli)

# stb (nur stb_vorbis.c v1.22, Ogg-Dekoder für Musik und Sprache; Commit vom 2026-10-19)
FetchContent_Declare(stb
  URL ${_gh}/nothings/stb/archive/a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d.tar.gz
  URL_HASH SHA256=ae498e10fadeba0c2e7aa86c71cc9989c4d895201cd26b8b72061c76cfde4edf)
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
