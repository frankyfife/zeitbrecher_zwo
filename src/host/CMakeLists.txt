add_executable(ts2 main.cpp ts2bin.cpp ts2bin.h replace.cpp replace.h input.cpp input.h fake.cpp fake.h features.cpp features.h menu.cpp menu.h profile.cpp profile.h fixes.cpp fixes.h fixes.inc shadowfix.cpp shadowfix.h alphafix.cpp alphafix.h effectfix.cpp effectfix.h logofix.cpp logofix.h ctxproxy.h hooks.cpp hooks.h menumouse.cpp menumouse.h subtitles.cpp subtitles.h fontfix.cpp fontfix.h hudfix.cpp hudfix.h timing.cpp timing.h postfx.cpp postfx.h audio.cpp audio.h mixer.cpp mixer.h reverb.cpp reverb.h vorbis.cpp watchdog.cpp watchdog.h levelfix.cpp levelfix.h cheats.cpp cheats.h quit.cpp quit.h multiplayer.cpp multiplayer.h
               log.h hf2.h ts2_manifest.inc ts2.rc)
target_compile_features(ts2 PRIVATE cxx_std_17)
target_compile_definitions(ts2 PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  # Die Wunschadresse 0x140000000 bleibt für den TS2-Code frei.
  target_link_options(ts2 PRIVATE /ENTRY:wmainCRTStartup /BASE:0x7FF700000000 /HIGHENTROPYVA /MAP:${CMAKE_BINARY_DIR}/ts2.map)
endif()

# stb_vorbis (Ogg-Dekoder an Stelle von libvorbisfile)
target_include_directories(ts2 PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(ts2 PRIVATE xaudio2 ole32 ts2options)
