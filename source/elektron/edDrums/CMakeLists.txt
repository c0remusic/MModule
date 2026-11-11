cmake_minimum_required(VERSION 3.15)

# The instrument's engine, without JUCE: 16 Tracks of either family, syntakt::fast or md::voice (ticket 11 of
# .scratch/elektron-drums-v1). mdEngine serves its flash image reader alone.
add_library(edDrums STATIC edDrumsChain.h edDrumsChain.cpp
	edDrumsEngine.h edDrumsEngine.cpp edDrumsHouseWaves.h edDrumsHouseWaves.cpp
	edDrumsLfo.h edDrumsLfo.cpp edDrumsLibrary.h edDrumsLibrary.cpp edDrumsOversampler.h edDrumsOversampler.cpp
	edDrumsMaster.h edDrumsMaster.cpp edDrumsSamples.h edDrumsSamples.cpp
	edDrumsSendFx.h edDrumsSendFx.cpp edDrumsSends.h edDrumsSends.cpp
	edDrumsSequencer.h edDrumsSequencer.cpp)
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

	# A Track's chain on its own: Q5, Q6, Q7, Q9, Q12, the skeleton's order; Q3 against the exact port (SYNTAKT_SECTION7,
	# SYNTAKT_SAMPLES: exit 77 without them)
	add_executable(edDrumsChainTest edDrumsChainTest.cpp)
	target_link_libraries(edDrumsChainTest PRIVATE edDrums stEngine)
	set_property(TARGET edDrumsChainTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsChainTest COMMAND edDrumsChainTest)
	set_tests_properties(edDrumsChainTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)

	# Two LFOs a Track on the transport, aimed at any Track; their laws against syntakt::main's and mdDrums::Lfo
	# (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(edDrumsLfoTest edDrumsLfoTest.cpp ../md/mdDrums/mdDrumsLfo.cpp)
	target_link_libraries(edDrumsLfoTest PRIVATE edDrums stEngine)
	target_include_directories(edDrumsLfoTest PRIVATE ../md/mdDrums)
	set_property(TARGET edDrumsLfoTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsLfoTest COMMAND edDrumsLfoTest)
	set_tests_properties(edDrumsLfoTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# The internal sequencer's core: steps at their sample whatever the block size, conditions, locks, jumps (ticket 17)
	add_executable(edDrumsSequencerTest edDrumsSequencerTest.cpp)
	target_link_libraries(edDrumsSequencerTest PRIVATE edDrums)
	set_property(TARGET edDrumsSequencerTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsSequencerTest COMMAND edDrumsSequencerTest)
	set_tests_properties(edDrumsSequencerTest PROPERTIES TIMEOUT 120)

	# What 16 Tracks of both families cost at their worst, by hand, not a test: edDrumsCost [RUNS] [SECONDS]
	add_executable(edDrumsCost edDrumsCost.cpp)
	target_link_libraries(edDrumsCost PRIVATE edDrums)
	set_property(TARGET edDrumsCost PROPERTY FOLDER "Elektron/test")

	# The machines made here against the flash image's, for listening, by hand, not a test
	add_executable(edDrumsHouseListen edDrumsHouseListen.cpp)
	target_link_libraries(edDrumsHouseListen PRIVATE edDrums)
	set_property(TARGET edDrumsHouseListen PROPERTY FOLDER "Elektron/test")

	# A beat through our chorus, phaser and flanger, for listening, by hand, not a test
	add_executable(edDrumsSendListen edDrumsSendListen.cpp)
	target_link_libraries(edDrumsSendListen PRIVATE edDrums)
	set_property(TARGET edDrumsSendListen PROPERTY FOLDER "Elektron/test")
endif()
