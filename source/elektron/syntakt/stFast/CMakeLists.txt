cmake_minimum_required(VERSION 3.15)

# The Syntakt's machines rewritten in float, SIMD across 16 Tracks, at the host's rate: syntakt::fast. The exact
# integer port (stEngine) is their reference, and their tables come from the user's section 7 through it.
add_library(stFast STATIC stFastBdModern.h stFastBdModern.cpp)
target_include_directories(stFast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(stFast PRIVATE stEngine)
set_property(TARGET stFast PROPERTY FOLDER "Elektron/syntakt")

if(BUILD_TESTING)
	# BD MODERN against the exact port at 48 kHz, 44.1 against 48 kHz, and its cost for 16 Tracks
	# (SYNTAKT_SECTION7: exit 77 without it)
	add_executable(stFastBdModernTest stFastBdModernTest.cpp)
	target_link_libraries(stFastBdModernTest PRIVATE stFast stEngine)
	set_property(TARGET stFastBdModernTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME stFastBdModernTest COMMAND stFastBdModernTest)
	set_tests_properties(stFastBdModernTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
