cmake_minimum_required(VERSION 3.15)

# The instrument's engine, without JUCE: 16 Tracks of either family, syntakt::fast or md::voice (ticket 11 of
# .scratch/elektron-drums-v1). mdEngine serves its flash image reader alone.
add_library(edDrums STATIC edDrumsChain.h edDrumsChain.cpp edDrumsEngine.h edDrumsEngine.cpp edDrumsLibrary.h
	edDrumsLibrary.cpp edDrumsOversampler.h edDrumsOversampler.cpp)
target_include_directories(edDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(edDrums PUBLIC mdVoice mdFast stFast PRIVATE mdEngine)
set_property(TARGET edDrums PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	# A Hit at its note's sample on every machine, what an off voice costs and changes, Chokes and machine changes
	# (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(edDrumsEngineTest edDrumsEngineTest.cpp)
	target_link_libraries(edDrumsEngineTest PRIVATE edDrums mdEngine mdProtocol)
	target_include_directories(edDrumsEngineTest PRIVATE ../md/mdDrums ../md/mdEngine/engine)
	set_property(TARGET edDrumsEngineTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsEngineTest COMMAND edDrumsEngineTest)
	set_tests_properties(edDrumsEngineTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# A Track's chain on its own: Q5, Q6, Q7, Q9, Q12, the skeleton's order, no firmware
	add_executable(edDrumsChainTest edDrumsChainTest.cpp)
	target_link_libraries(edDrumsChainTest PRIVATE edDrums)
	set_property(TARGET edDrumsChainTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsChainTest COMMAND edDrumsChainTest)
	set_tests_properties(edDrumsChainTest PROPERTIES TIMEOUT 120)

	# What 16 Tracks of both families cost at their worst, by hand, not a test: edDrumsCost [RUNS] [SECONDS]
	add_executable(edDrumsCost edDrumsCost.cpp)
	target_link_libraries(edDrumsCost PRIVATE edDrums)
	set_property(TARGET edDrumsCost PROPERTY FOLDER "Elektron/test")
endif()
