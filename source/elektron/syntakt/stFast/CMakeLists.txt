cmake_minimum_required(VERSION 3.15)

# The Syntakt's machines rewritten in float, SIMD across 16 Tracks, at their own 48 kHz (a 44.1 kHz host through a
# resampler): syntakt::fast. The exact
# integer port (stEngine) is their reference, in the tests only: their tables are made by formula (stFastTables), and
# nothing of the firmware is read.
add_library(stFast STATIC stFastStages.h stFastStages.cpp stFastBdModern.h stFastBdModern.cpp stFastSdBasic.h
	stFastSdBasic.cpp stFastCyAlloy.h stFastCyAlloy.cpp stFastPcCarbon.h stFastPcCarbon.cpp stFastSdVintage.h
	stFastSdVintage.cpp stFastCpVintage.h stFastCpVintage.cpp stFastSyTone.h stFastSyTone.cpp stFastSyChord.h
	stFastSyChord.cpp stFastResample.h stFastResample.cpp stFastTables.h stFastTables.cpp)
target_include_directories(stFast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET stFast PROPERTY FOLDER "Elektron/syntakt")

if(BUILD_TESTING)
	# The tables by formula against section 7, word for word (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastTablesTest stFastTablesTest.cpp)
	target_link_libraries(stFastTablesTest PRIVATE stFast stEngine)
	set_property(TARGET stFastTablesTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastTablesTest COMMAND stFastTablesTest)
	set_tests_properties(stFastTablesTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 10)

	# BD MODERN against the exact port at 48 kHz, 44.1 against 48 kHz, and its cost for 16 Tracks
	# (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastBdModernTest stFastBdModernTest.cpp stFastBdModernHits.h stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastBdModernTest PRIVATE stFast stEngine)
	set_property(TARGET stFastBdModernTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastBdModernTest COMMAND stFastBdModernTest)
	set_tests_properties(stFastBdModernTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# SD BASIC the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastSdBasicTest stFastSdBasicTest.cpp stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastSdBasicTest PRIVATE stFast stEngine)
	set_property(TARGET stFastSdBasicTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastSdBasicTest COMMAND stFastSdBasicTest)
	set_tests_properties(stFastSdBasicTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# CY ALLOY the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastCyAlloyTest stFastCyAlloyTest.cpp stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastCyAlloyTest PRIVATE stFast stEngine)
	set_property(TARGET stFastCyAlloyTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastCyAlloyTest COMMAND stFastCyAlloyTest)
	set_tests_properties(stFastCyAlloyTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# PC CARBON the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastPcCarbonTest stFastPcCarbonTest.cpp stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastPcCarbonTest PRIVATE stFast stEngine)
	set_property(TARGET stFastPcCarbonTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastPcCarbonTest COMMAND stFastPcCarbonTest)
	set_tests_properties(stFastPcCarbonTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# SD VINTAGE the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastSdVintageTest stFastSdVintageTest.cpp stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastSdVintageTest PRIVATE stFast stEngine)
	set_property(TARGET stFastSdVintageTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastSdVintageTest COMMAND stFastSdVintageTest)
	set_tests_properties(stFastSdVintageTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# CP VINTAGE the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastCpVintageTest stFastCpVintageTest.cpp stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastCpVintageTest PRIVATE stFast stEngine)
	set_property(TARGET stFastCpVintageTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastCpVintageTest COMMAND stFastCpVintageTest)
	set_tests_properties(stFastCpVintageTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# SY TONE the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastSyToneTest stFastSyToneTest.cpp stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastSyToneTest PRIVATE stFast stEngine)
	set_property(TARGET stFastSyToneTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastSyToneTest COMMAND stFastSyToneTest)
	set_tests_properties(stFastSyToneTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# SY CHORD the same (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastSyChordTest stFastSyChordTest.cpp stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastSyChordTest PRIVATE stFast stEngine)
	set_property(TARGET stFastSyChordTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastSyChordTest COMMAND stFastSyChordTest)
	set_tests_properties(stFastSyChordTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# The resampler that brings the fast Syntakt's 48 kHz to a 44.1 kHz host (Q11): no firmware
	add_executable(stFastResampleTest stFastResampleTest.cpp stFastBdModernHits.h stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastResampleTest PRIVATE stFast stEngine)
	set_property(TARGET stFastResampleTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastResampleTest COMMAND stFastResampleTest)
	set_tests_properties(stFastResampleTest PROPERTIES TIMEOUT 10)

	# The listening test's WAVs and ABX page (ticket 05), by hand, into a folder outside the repository:
	# stFastAbRender OUTDIR (SYNTAKT_SECTION7)
	add_executable(stFastAbRender stFastAbRender.cpp stFastBdModernHits.h stFastBench.h stFastMetrics.h
		stFastAbPage.html)
	target_link_libraries(stFastAbRender PRIVATE stFast stEngine)
	target_compile_definitions(stFastAbRender PRIVATE ST_FAST_AB_PAGE="${CMAKE_CURRENT_SOURCE_DIR}/stFastAbPage.html"
		ST_FAST_REPO="${CMAKE_SOURCE_DIR}")
	set_property(TARGET stFastAbRender PROPERTY FOLDER "Elektron/test")

	# Every value of every sound parameter, fast against exact, and A/B WAVs of chosen ones, by hand, outside the
	# repository: stFastSweep sweep MACHINE OUT.csv | ab OUTDIR CASES.txt (SYNTAKT_SECTION7)
	add_executable(stFastSweep stFastSweep.cpp stFastBdModernHits.h stFastBench.h stFastMetrics.h)
	target_link_libraries(stFastSweep PRIVATE stFast stEngine)
	target_compile_definitions(stFastSweep PRIVATE ST_FAST_AB_PAGE="${CMAKE_CURRENT_SOURCE_DIR}/stFastAbPage.html"
		ST_FAST_REPO="${CMAKE_SOURCE_DIR}")
	set_property(TARGET stFastSweep PROPERTY FOLDER "Elektron/test")
endif()
