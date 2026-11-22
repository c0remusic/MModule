cmake_minimum_required(VERSION 3.15)

# The FM DRUMS family: the Model:Cycles' machines rewritten in float, machines of syntakt::fast's kind (16 lanes, blocks
# of 32 at 48 kHz). Their laws were read at the OS's oracle, outside the repository; nothing of the firmware is read
# here, their parameter curves are fitted constants (cyFastKickCurves.inc, cyFastSnareCurves.inc, cyFastMetalCurves.inc,
# cyFastPercCurves.inc, cyFastToneCurves.inc, cyFastChordCurves.inc). CHORD's waves are the OS's data: its owner reads
# them from the user's own OS at load (cyFastChordWaves)
add_library(cyFast STATIC cyFastFm.h cyFastFm.cpp cyFastKick.h cyFastKick.cpp cyFastKickCurves.inc
	cyFastSnare.h cyFastSnare.cpp cyFastSnareCurves.inc cyFastMetal.h cyFastMetal.cpp cyFastMetalCurves.inc
	cyFastPerc.h cyFastPerc.cpp cyFastPercCurves.inc cyFastTone.h cyFastTone.cpp cyFastToneCurves.inc
	cyFastChord.h cyFastChord.cpp cyFastChordCurves.inc cyFastChordWaves.h cyFastChordWaves.cpp)
target_include_directories(cyFast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cyFast PUBLIC stFast)
set_property(TARGET cyFast PROPERTY FOLDER "Elektron/cycles")

if(BUILD_TESTING)
	# FMD KICK against the OS's KICK (CYCLES_VECTORS, CYCLES_GATE_VECTORS: exit 77 without them), 44.1 against 48 kHz,
	# its lanes and its cost
	add_executable(cyFastKickTest cyFastKickTest.cpp)
	target_link_libraries(cyFastKickTest PRIVATE cyFast)
	set_property(TARGET cyFastKickTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME cyFastKickTest COMMAND cyFastKickTest)
	set_tests_properties(cyFastKickTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)

	# FMD SNARE and FMD METAL against the OS's SNARE and METAL (CYCLES_VECTORS, CYCLES_GATE_SNARE_VECTORS,
	# CYCLES_GATE_METAL_VECTORS: exit 77 without them), 44.1 against 48 kHz, their lanes and their cost
	add_executable(cyFastSnareMetalTest cyFastSnareMetalTest.cpp)
	target_link_libraries(cyFastSnareMetalTest PRIVATE cyFast)
	set_property(TARGET cyFastSnareMetalTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME cyFastSnareMetalTest COMMAND cyFastSnareMetalTest)
	set_tests_properties(cyFastSnareMetalTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)

	# FMD PERC and FMD TONE against the OS's PERC and TONE (CYCLES_VECTORS, CYCLES_GATE_PERC_VECTORS,
	# CYCLES_GATE_TONE_VECTORS: exit 77 without them), 44.1 against 48 kHz, their lanes and their cost
	add_executable(cyFastPercToneTest cyFastPercToneTest.cpp)
	target_link_libraries(cyFastPercToneTest PRIVATE cyFast)
	set_property(TARGET cyFastPercToneTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME cyFastPercToneTest COMMAND cyFastPercToneTest)
	set_tests_properties(cyFastPercToneTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)

	# FMD CHORD against the OS's CHORD, its waves read from the user's OS (CYCLES_SECTION3, CYCLES_VECTORS,
	# CYCLES_GATE_CHORD_VECTORS: exit 77 without them), 44.1 against 48 kHz, its lanes and its cost
	add_executable(cyFastChordTest cyFastChordTest.cpp)
	target_link_libraries(cyFastChordTest PRIVATE cyFast)
	set_property(TARGET cyFastChordTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME cyFastChordTest COMMAND cyFastChordTest)
	set_tests_properties(cyFastChordTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
