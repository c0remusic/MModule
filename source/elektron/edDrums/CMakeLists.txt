cmake_minimum_required(VERSION 3.15)

# The instrument's engine, without JUCE: 16 Tracks of either family, syntakt::fast or md::voice (ticket 11 of
# .scratch/elektron-drums-v1). mdEngine serves its flash image reader alone.
add_library(edDrums STATIC edDrumsAnalog.h edDrumsAnalogPort.h edDrumsAnalogShared.cpp edDrumsAnalogVoice.h edDrumsAnalogVoice.cpp
	edDrumsAnalogKick.cpp edDrumsAnalogKickVoice.cpp edDrumsAnalogSnare.cpp edDrumsAnalogSnareVoice.cpp
	edDrumsAnalogSynth.cpp edDrumsAnalogSynthVoice.cpp edDrumsAnalogUtility.cpp edDrumsAnalogUtilityVoice.cpp
	edDrumsAnalogRates.h edDrumsAnalogRates.cpp edDrumsAnalogMetal.cpp edDrumsAnalogMetalVoice.cpp
	edDrumsChain.h edDrumsChain.cpp
	edDrumsEngine.h edDrumsEngine.cpp edDrumsHouseWaves.h edDrumsHouseWaves.cpp
	edDrumsLfo.h edDrumsLfo.cpp edDrumsLibrary.h edDrumsLibrary.cpp edDrumsOversampler.h edDrumsOversampler.cpp
	edDrumsMaster.h edDrumsMaster.cpp edDrumsRate.h edDrumsRate.cpp edDrumsRateAvx2.cpp edDrumsSamples.h edDrumsSamples.cpp
	edDrumsSendFx.h edDrumsSendFx.cpp edDrumsSends.h edDrumsSends.cpp
	edDrumsSequencer.h edDrumsSequencer.cpp edDrumsVintage.h edDrumsVintage.cpp)
target_include_directories(edDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The rates' FIRs in AVX2 and FMA, which run only where rate::fastFir() finds the CPU has them: their file alone built
# for it, as stEngine's stSimdAvx2.cpp
if(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "x64")
	set_source_files_properties(edDrumsRateAvx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
	set_source_files_properties(edDrumsRateAvx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
endif()
target_link_libraries(edDrums PUBLIC mdVoice mdFast stFast sophie PRIVATE mdEngine opendrum)
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

	# The engine at 44.1, 48, 88.2, 96 and 192 kHz: 44.1 unchanged, Q15, pitch, HitDelay and its jitter, the cost
	# (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(edDrumsRateTest edDrumsRateTest.cpp)
	target_link_libraries(edDrumsRateTest PRIVATE edDrums)
	set_property(TARGET edDrumsRateTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsRateTest COMMAND edDrumsRateTest)
	set_tests_properties(edDrumsRateTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 600)

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

	# OPEN DRUM against the original engine's renders, through the engine too, and its cost (OPENDRUM_REFERENCES: exit 77
	# without them)
	add_executable(edDrumsOpenDrumTest edDrumsOpenDrumTest.cpp)
	target_link_libraries(edDrumsOpenDrumTest PRIVATE edDrums opendrum)
	target_include_directories(edDrumsOpenDrumTest PRIVATE ../md/mdDrums)
	set_property(TARGET edDrumsOpenDrumTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsOpenDrumTest COMMAND edDrumsOpenDrumTest)
	set_tests_properties(edDrumsOpenDrumTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)

	# SOPHIE through the engine against the voice alone, its factory sounds, BODY, its start, stop and cost
	add_executable(edDrumsSophieTest edDrumsSophieTest.cpp)
	target_link_libraries(edDrumsSophieTest PRIVATE edDrums sophie)
	target_include_directories(edDrumsSophieTest PRIVATE ../md/mdDrums)
	set_property(TARGET edDrumsSophieTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsSophieTest COMMAND edDrumsSophieTest)
	set_tests_properties(edDrumsSophieTest PROPERTIES TIMEOUT 300)

	# The analog voices' control against the original routines' vectors, and its fitted tables against the original's
	# words (ANALOG_VECTORS, SYNTAKT_SECTION3: exit 77 without them)
	add_executable(edDrumsAnalogControlTest edDrumsAnalogControlTest.cpp)
	target_link_libraries(edDrumsAnalogControlTest PRIVATE edDrums)
	set_property(TARGET edDrumsAnalogControlTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsAnalogControlTest COMMAND edDrumsAnalogControlTest)
	set_tests_properties(edDrumsAnalogControlTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)

	# The analog family through the engine: Hits sample for sample against the voice alone, HitDelay, jitter 0, let go
	# once quiet, no allocation, the dearest kit's cost
	add_executable(edDrumsAnalogTest edDrumsAnalogTest.cpp)
	target_link_libraries(edDrumsAnalogTest PRIVATE edDrums)
	target_include_directories(edDrumsAnalogTest PRIVATE ../md/mdDrums)
	set_property(TARGET edDrumsAnalogTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsAnalogTest COMMAND edDrumsAnalogTest)
	set_tests_properties(edDrumsAnalogTest PROPERTIES TIMEOUT 600)

	# The Syntakt family's fresh amp against its machines' own decay to -40 dB
	add_executable(edDrumsAmpTest edDrumsAmpTest.cpp)
	target_link_libraries(edDrumsAmpTest PRIVATE edDrums)
	set_property(TARGET edDrumsAmpTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsAmpTest COMMAND edDrumsAmpTest)
	set_tests_properties(edDrumsAmpTest PROPERTIES TIMEOUT 300)

	# The vintage PCM character: the DAC's codes, the low-pass, the VINTAGE grain's hold and pitch, the DAC drive, old
	# Samplers, allocations and cost
	add_executable(edDrumsVintageTest edDrumsVintageTest.cpp)
	target_link_libraries(edDrumsVintageTest PRIVATE edDrums)
	target_include_directories(edDrumsVintageTest PRIVATE ../md/mdDrums)
	set_property(TARGET edDrumsVintageTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME edDrumsVintageTest COMMAND edDrumsVintageTest)
	set_tests_properties(edDrumsVintageTest PROPERTIES TIMEOUT 300)

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
