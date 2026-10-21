cmake_minimum_required(VERSION 3.15)

# MD Drums' engine side, without JUCE: finds the flash image, drives mdEngine as the plug-in does, and its
# synthLib::Device, which speaks the Machinedrum's MIDI.
project(mdDrums)

add_library(mdDrums STATIC
	mdDrumsBank.cpp mdDrumsBank.h
	mdDrumsDevice.cpp mdDrumsDevice.h
	mdDrumsEngine.cpp mdDrumsEngine.h
	mdDrumsFilterResponse.cpp mdDrumsFilterResponse.h
	mdDrumsHitPreview.cpp mdDrumsHitPreview.h
	mdDrumsKitFiles.cpp mdDrumsKitFiles.h
	mdDrumsLfo.cpp mdDrumsLfo.h
	mdDrumsLibrary.cpp mdDrumsLibrary.h
	mdDrumsSampleAudio.cpp mdDrumsSampleAudio.h
	mdDrumsSampleLibrary.cpp mdDrumsSampleLibrary.h
	mdDrumsSamples.cpp mdDrumsSamples.h
	mdDrumsMessages.cpp mdDrumsMessages.h
	mdDrumsTelemetry.h
)
target_include_directories(mdDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdDrums PUBLIC mdEngine mdProtocol synthLib)
set_property(TARGET mdDrums PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	# An executable of MD Drums' engine side, built from TARGET_NAME.cpp, with what mdDrumsTestSupport.h uses: psapi
	# for the process's memory on Windows
	function(add_md_drums_executable TARGET_NAME)
		add_executable(${TARGET_NAME} ${TARGET_NAME}.cpp)
		target_link_libraries(${TARGET_NAME} PRIVATE mdDrums)
		if(WIN32)
			target_link_libraries(${TARGET_NAME} PRIVATE psapi)
		endif()
		set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Elektron/test")
	endfunction()

	# The same as a test ctest runs: TEST_LABELS a list ("FirmwareTest;AudioIo"), TEST_TIMEOUT in seconds, exit 77 a
	# skip (no flash image)
	function(add_md_drums_test TARGET_NAME TEST_LABELS TEST_TIMEOUT)
		add_md_drums_executable(${TARGET_NAME})
		add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
		set_tests_properties(${TARGET_NAME} PROPERTIES LABELS "${TEST_LABELS}" SKIP_RETURN_CODE 77
			TIMEOUT ${TEST_TIMEOUT})
	endfunction()

	add_md_drums_test(mdDrumsEngineTest "FirmwareTest;AudioIo" 120)

	# The Device driven by MIDI bytes alone, through synthLib::Plugin as the plug-in drives it. With --measure, by hand,
	# it also times the whole Device with and without its Telemetry
	add_md_drums_test(mdDrumsDeviceTest "FirmwareTest;AudioIo;Midi" 120)

	# What the editor's Filter and EQ and LFO screens compute: the LFO's port against the OS's routines, the response
	add_md_drums_test(mdDrumsScreensTest "FirmwareTest" 120)

	# The Kits' Bank: its file, its Slots, two instances on one file
	add_md_drums_test(mdDrumsBankTest "FirmwareTest" 60)

	# The Hit screen's preview (ticket 28): the Hit the Device would play first, the same each time, on a shared thread
	add_md_drums_test(mdDrumsHitPreviewTest "FirmwareTest" 120)

	# A Track's first Hit against the next ones, the OS's code and the voice DSP apart, on a cold engine and on one whose
	# machines were compiled ahead (ticket 31): the times are printed, the two engines' samples must be the same
	add_md_drums_test(mdDrumsFirstHitTest "FirmwareTest" 120)

	# The reader of MDX X.20's OS file: its LZMA decoder, refusals, and the user's own file where MD_X20_OS names it
	add_md_drums_test(mdX20OsTest "FirmwareTest" 60)

	# X.20's built-in Machines on the engine from the user's X.20 OS (MD_X20_OS): GND-SW sounds, the others unchanged
	add_md_drums_test(mdX20BuiltinsTest "FirmwareTest;AudioIo" 180)

	# The port of X.20's coefficient routines against the routines run in a 68k on the user's X.20 OS (MD_X20_OS, exit 77
	# without). Musashi serves one CPU class per executable: the port, the OS reader and the 68k alone, no engine
	set(MD_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mdEngine)
	add_executable(mdX20PortTest mdX20PortTest.cpp ${MD_ENGINE_DIR}/engine/X20Builtins.cpp
		${MD_ENGINE_DIR}/tools/mdfw/X20Os.cpp ${MD_ENGINE_DIR}/tools/mdfw/Lzma.cpp ${MD_ENGINE_DIR}/tools/mdfw/Firmware.cpp)
	target_include_directories(mdX20PortTest PRIVATE ${MD_ENGINE_DIR}/engine ${MD_ENGINE_DIR}/tools/mdfw
		${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${CMAKE_CURRENT_SOURCE_DIR}/../../../mc68k)
	target_link_libraries(mdX20PortTest PRIVATE 68kEmu)
	set_property(TARGET mdX20PortTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdX20PortTest COMMAND mdX20PortTest)
	set_tests_properties(mdX20PortTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)

	# Measurement, not a gate: what a second engine costs the Hit screen and the browser's preview (ticket 12)
	add_md_drums_executable(mdDrumsPreviewTest)
endif()
