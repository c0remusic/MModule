cmake_minimum_required(VERSION 3.15)

# The Syntakt's digital engines ported to C++ from OS 1.41's voice CPU, without JUCE or an emulator. It builds alone
# too (cmake -S source/elektron/syntakt/stEngine), for the port's quick loop against the original routines.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(stEngine CXX)
	set(CMAKE_CXX_STANDARD 17)
	enable_testing()
	set(BUILD_TESTING ON)
endif()

add_library(stEngine STATIC stEmac.h stMemoryMap.h stVoice.h stVoice.cpp stStages.h stStages.cpp stBdModern.cpp
	stEngine.h stEngine.cpp stBoot.h stBoot.cpp stTrack.h stTrack.cpp stTrackFilter.cpp stSection7.h stEngines.h
	stSdBasic.cpp stCyAlloy.cpp stPcCarbon.cpp stSdVintage.cpp stCpVintage.cpp
	stSyTone.cpp stSyChord.cpp stSyToy.cpp stSyBits.cpp stSySwarm.cpp stSpTwinshot.cpp stVoiceCpu.h stVoiceCpu.cpp
	stMainMemory.h stMainMix.h stMainMix.cpp stMainControl.h stMainControl.cpp
	stMainModulation.h stMainModulation.cpp stMainLfo.h stMainLfo.cpp
	stMainPitch.h stMainPitch.cpp stMainFilter.h stMainFilter.cpp stMainBlock.h stMainBlock.cpp
	stMainEmac.h stMainBus.h stMainBus.cpp stMainFx.h stMainFx.cpp stMainDelay.h stMainDelay.cpp
	stMainReverb.h stMainReverb.cpp stMainOutput.h stMainOutput.cpp stSyntakt.h stSyntakt.cpp)
target_include_directories(stEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET stEngine PROPERTY FOLDER "Elektron/syntakt")

if(BUILD_TESTING)
	# The port against the original routines' vectors (SYNTAKT_SECTION7, SYNTAKT_VECTORS: exit 77 without them)
	add_executable(stEngineOracleTest stEngineOracleTest.cpp)
	target_link_libraries(stEngineOracleTest PRIVATE stEngine)
	set_property(TARGET stEngineOracleTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stEngineOracleTest COMMAND stEngineOracleTest)
	set_tests_properties(stEngineOracleTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# syntakt::VoiceCpu against the original's boot and output (SYNTAKT_SECTION7, SYNTAKT_REFERENCES: exit 77 without)
	add_executable(stVoiceCpuTest stVoiceCpuTest.cpp)
	target_link_libraries(stVoiceCpuTest PRIVATE stEngine)
	set_property(TARGET stVoiceCpuTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stVoiceCpuTest COMMAND stVoiceCpuTest)
	set_tests_properties(stVoiceCpuTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# CPU #1's ported routines against the original ones (SYNTAKT_SECTION3, SYNTAKT_MAIN_VECTORS: exit 77 without them)
	add_executable(stMainOracleTest stMainOracleTest.cpp)
	target_link_libraries(stMainOracleTest PRIVATE stEngine)
	set_property(TARGET stMainOracleTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stMainOracleTest COMMAND stMainOracleTest)
	set_tests_properties(stMainOracleTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# syntakt::Syntakt end to end against the original firmware (SYNTAKT_SECTION3, SYNTAKT_SECTION7, SYNTAKT_SAMPLES,
	# SYNTAKT_SCENARIO: exit 77 without them)
	add_executable(stSyntaktTest stSyntaktTest.cpp)
	target_link_libraries(stSyntaktTest PRIVATE stEngine)
	set_property(TARGET stSyntaktTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stSyntaktTest COMMAND stSyntaktTest)
	set_tests_properties(stSyntaktTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# A machine rendered through VoiceCpu into a WAV, by hand (stRender SECTION7 OUT.wav MACHINE NOTE SECONDS)
	add_executable(stRender stRender.cpp)
	target_link_libraries(stRender PRIVATE stEngine)
	set_property(TARGET stRender PROPERTY FOLDER "Elektron/test")

	# syntakt::Syntakt's cost per block by stage on a busy pattern, by hand (SYNTAKT_SECTION3, SYNTAKT_SECTION7,
	# SYNTAKT_SAMPLES: exit 77 without them)
	add_executable(stSyntaktBench stSyntaktBench.cpp)
	target_link_libraries(stSyntaktBench PRIVATE stEngine)
	set_property(TARGET stSyntaktBench PROPERTY FOLDER "Elektron/test")
endif()
