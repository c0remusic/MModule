cmake_minimum_required(VERSION 3.15)

# The Syntakt's digital engines ported to C++ from OS 1.41's voice CPU, without JUCE or an emulator. It builds alone
# too (cmake -S source/elektron/syntakt/stEngine), for the port's quick loop against the original routines.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(stEngine CXX)
	set(CMAKE_CXX_STANDARD 17)
	enable_testing()
	set(BUILD_TESTING ON)
endif()

add_library(stEngine STATIC stEmac.h stVoice.h stVoice.cpp stStages.h stStages.cpp stBdModern.h stBdModern.cpp
	stEngine.h stEngine.cpp stBoot.h stBoot.cpp stTrack.h stTrack.cpp stTrackFilter.cpp stSection7.h stEngines.h
	stSdBasic.cpp stCyAlloy.cpp stPcCarbon.cpp stSdVintage.cpp stCpVintage.cpp)
target_include_directories(stEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET stEngine PROPERTY FOLDER "Elektron/syntakt")

if(BUILD_TESTING)
	# The port against the original routines' vectors (SYNTAKT_SECTION7, SYNTAKT_VECTORS: exit 77 without them)
	add_executable(stEngineOracleTest stEngineOracleTest.cpp)
	target_link_libraries(stEngineOracleTest PRIVATE stEngine)
	set_property(TARGET stEngineOracleTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stEngineOracleTest COMMAND stEngineOracleTest)
	set_tests_properties(stEngineOracleTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# A machine rendered from the port alone into a WAV, by hand (stRender SECTION7 OUT.wav MACHINE NOTE SECONDS)
	add_executable(stRender stRender.cpp)
	target_link_libraries(stRender PRIVATE stEngine)
	set_property(TARGET stRender PROPERTY FOLDER "Elektron/test")
endif()
