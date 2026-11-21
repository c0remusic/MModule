cmake_minimum_required(VERSION 3.15)

# The FM DRUMS family: the Model:Cycles' machines rewritten in float, machines of syntakt::fast's kind (16 lanes, blocks
# of 32 at 48 kHz). Their laws were read at the OS's oracle, outside the repository; nothing of the firmware is read
# here, their parameter curves are fitted constants (cyFastKickCurves.inc)
add_library(cyFast STATIC cyFastKick.h cyFastKick.cpp cyFastKickCurves.inc)
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
endif()
