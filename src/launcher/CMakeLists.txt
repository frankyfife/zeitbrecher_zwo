# ts2launcher.exe (Qt 6 Widgets). Qt ist gegen die DLL-Laufzeit gebaut; die
# gemeinsame Options-Quelle wird hier mit derselben Laufzeit übersetzt statt
# die statische Bibliothek (statische Laufzeit) zu binden.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
add_executable(ts2launcher WIN32
  main.cpp mainwindow.cpp mainwindow.h keyslot.cpp keyslot.h theme.cpp theme.h ini.cpp ini.h
  ${CMAKE_SOURCE_DIR}/src/common/ts2options.cpp ${CMAKE_SOURCE_DIR}/src/common/ts2options.h)
set_target_properties(ts2launcher PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  AUTOMOC ON)
target_include_directories(ts2launcher PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
target_compile_features(ts2launcher PRIVATE cxx_std_17)
target_compile_definitions(ts2launcher PRIVATE UNICODE _UNICODE NOMINMAX)
if(MSVC)
  target_compile_options(ts2launcher PRIVATE /utf-8)
endif()
target_link_libraries(ts2launcher PRIVATE Qt6::Widgets)

# Qt-DLLs neben den Launcher legen
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qtbin "${_qmake}" DIRECTORY)
add_custom_command(TARGET ts2launcher POST_BUILD
  COMMAND "${_qtbin}/windeployqt.exe" --release --no-translations --no-system-d3d-compiler
          --no-opengl-sw --no-compiler-runtime "$<TARGET_FILE:ts2launcher>"
  COMMENT "Qt-DLLs neben ts2launcher.exe")
