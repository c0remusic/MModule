cmake_minimum_required(VERSION 3.15)

# ST Drums' sound without JUCE: the user's OS 1.41 .syx read into sections 3 and 7, syntakt::Syntakt with 16 tracks
# played by notes, its outputs as the plug-in's channels at the host's rate (the stack's resampler)
add_library(stDrums STATIC stDrumsFirmware.h stDrumsFirmware.cpp stDrumsPlayer.h stDrumsPlayer.cpp)
target_include_directories(stDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(stDrums PUBLIC stEngine synthLib)
set_property(TARGET stDrums PROPERTY FOLDER "Elektron/syntakt")

if(BUILD_TESTING)
	# The .syx reader on the user's own file (SYNTAKT_OS_SYX: exit 77 without it)
	add_executable(stDrumsFirmwareTest stDrumsFirmwareTest.cpp)
	target_link_libraries(stDrumsFirmwareTest PRIVATE stDrums)
	set_property(TARGET stDrumsFirmwareTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stDrumsFirmwareTest COMMAND stDrumsFirmwareTest)
	set_tests_properties(stDrumsFirmwareTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
