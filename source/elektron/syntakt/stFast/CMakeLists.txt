cmake_minimum_required(VERSION 3.15)

# The Syntakt's machines rewritten in float, SIMD across 16 Tracks, at the host's rate: syntakt::fast. The exact
# integer port (stEngine) is their reference, in the tests only: their tables are made by formula (stFastTables), and
# nothing of the firmware is read.
add_library(stFast STATIC stFastStages.h stFastStages.cpp stFastBdModern.h stFastBdModern.cpp stFastSdBasic.h
	stFastSdBasic.cpp stFastTables.h stFastTables.cpp)
target_include_directories(stFast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET stFast PROPERTY FOLDER "Elektron/syntakt")

if(BUILD_TESTING)
	# The tables by formula against section 7, word for word (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastTablesTest stFastTablesTest.cpp)
	target_link_libraries(stFastTablesTest PRIVATE stFast stEngine)
	set_property(TARGET stFastTablesTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastTablesTest COMMAND stFastTablesTest)
	set_tests_properties(stFastTablesTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 60)

	# BD MODERN against the exact port at 48 kHz, 44.1 against 48 kHz, and its cost for 16 Tracks
	# (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastBdModernTest stFastBdModernTest.cpp stFastBench.h)
	target_link_libraries(stFastBdModernTest PRIVATE stFast stEngine)
	set_property(TARGET stFastBdModernTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastBdModernTest COMMAND stFastBdModernTest)
	set_tests_properties(stFastBdModernTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# SD BASIC the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastSdBasicTest stFastSdBasicTest.cpp stFastBench.h)
	target_link_libraries(stFastSdBasicTest PRIVATE stFast stEngine)
	set_property(TARGET stFastSdBasicTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastSdBasicTest COMMAND stFastSdBasicTest)
	set_tests_properties(stFastSdBasicTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# The listening test's WAVs and ABX page (ticket 05), by hand, into a folder outside the repository:
	# stFastAbRender OUTDIR (SYNTAKT_SECTION7)
	add_executable(stFastAbRender stFastAbRender.cpp stFastBench.h stFastAbPage.html)
	target_link_libraries(stFastAbRender PRIVATE stFast stEngine)
	target_compile_definitions(stFastAbRender PRIVATE ST_FAST_AB_PAGE="${CMAKE_CURRENT_SOURCE_DIR}/stFastAbPage.html"
		ST_FAST_REPO="${CMAKE_SOURCE_DIR}")
	set_property(TARGET stFastAbRender PROPERTY FOLDER "Elektron/test")
endif()
