# ts2.ini-Optionen, gemeinsam für ts2.exe und den Launcher
add_library(ts2options STATIC ts2options.cpp ts2options.h)
target_compile_features(ts2options PUBLIC cxx_std_17)
target_include_directories(ts2options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(ts2options PRIVATE /utf-8)
endif()
