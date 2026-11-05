cmake_minimum_required(VERSION 3.15)

# The Machinedrum's per-Track chain, SIMD across 16 Tracks, and its Echo and Gate Box: md::fast. mdEngine's exact integer
# port is their reference, in the tests only: the tables are made by formula, but for the one spec 11.4 lets its owner
# import, and nothing of the firmware is read.
add_library(mdFast STATIC mdFastDsp.h mdFastTables.h mdFastTables.cpp mdFastTrackFx.h mdFastTrackFx.cpp mdFastEcho.h
	mdFastEcho.cpp mdFastGateBox.h mdFastGateBox.cpp mdFastMasterWords.h mdFastMasterWords.cpp)
target_include_directories(mdFast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdFast PUBLIC mdVoice stFast)
# The chain's floor to a word rests on additions that fast math would cancel (base.cmake's -Ofast, outside MSVC)
if(NOT MSVC)
	target_compile_options(mdFast PRIVATE -fno-fast-math)
endif()
set_property(TARGET mdFast PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	# Each stage against TrackFx, the tables against the OS's, and the chain's cost for 16 Tracks
	# (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(mdFastTrackFxTest mdFastTrackFxTest.cpp)
	target_link_libraries(mdFastTrackFxTest PRIVATE mdFast mdVoice mdEngine)
	target_include_directories(mdFastTrackFxTest PRIVATE ../mdDrums)
	set_property(TARGET mdFastTrackFxTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdFastTrackFxTest COMMAND mdFastTrackFxTest)
	set_tests_properties(mdFastTrackFxTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# The Echo and the Gate Box against the emulated master, and the words of their parameters against the OS's
	# (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(mdFastSendsTest mdFastSendsTest.cpp)
	target_link_libraries(mdFastSendsTest PRIVATE mdFast mdVoice mdEngine)
	target_include_directories(mdFastSendsTest PRIVATE ../mdDrums)
	set_property(TARGET mdFastSendsTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdFastSendsTest COMMAND mdFastSendsTest)
	set_tests_properties(mdFastSendsTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
