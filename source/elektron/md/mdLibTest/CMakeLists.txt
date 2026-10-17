cmake_minimum_required(VERSION 3.15)

# The firmware as MD Drums' oracle: mdLib runs the whole Machinedrum, ColdFire OS and both DSPs, so that the engines
# without the OS (mdEngine) and the Kit codecs (mdProtocol) are compared with it. Each test needs
# GEARMULATOR_MD_FIRMWARE_BIN and skips (77) without it.

add_executable(mdAutomationMidiTest automationMidiTest.cpp)
target_link_libraries(mdAutomationMidiTest PRIVATE mdLib)
add_test(NAME mdAutomationMidiTest COMMAND mdAutomationMidiTest)
set_tests_properties(mdAutomationMidiTest PROPERTIES LABELS "UnitTest")
set_property(TARGET mdAutomationMidiTest PROPERTY FOLDER "Elektron")

add_executable(mdKitDumpFirmwareTest kitDumpFirmwareTest.cpp)
target_link_libraries(mdKitDumpFirmwareTest PRIVATE mdLib)
add_test(NAME mdKitDumpFirmwareTest COMMAND mdKitDumpFirmwareTest)
set_tests_properties(mdKitDumpFirmwareTest PROPERTIES
	LABELS "Integration;FirmwareTest;Midi"
	SKIP_RETURN_CODE 77
	TIMEOUT 300)
set_property(TARGET mdKitDumpFirmwareTest PROPERTY FOLDER "Elektron/test")

# Measurement, not a gate: run by hand to report note-to-sound latency, jitter, the note map and velocity.
add_executable(mdTrigLatencyFirmwareTest trigLatencyFirmwareTest.cpp firmwareBench.h hitParameters.h)
target_link_libraries(mdTrigLatencyFirmwareTest PRIVATE mdLib)
set_property(TARGET mdTrigLatencyFirmwareTest PROPERTY FOLDER "Elektron/test")

# The engines without the ColdFire OS against the full firmware. Two processes: Musashi's entry points serve one
# CPU class per executable, so the firmware records its hits first (keep the ids in step with g_hitMachines in
# hitParameters.h) and the voice DSP's memory from the UW bank's directory ($147e00, fw::RomBank::DirectoryAddr)
# past its samples' end ($18fc12), the scenarios of Links and Chokes (g_groupScenarios) and those of the master
# effects (g_masterScenarios), and the engine test compares them.
add_executable(mdEngineFirmwareTest engineFirmwareTest.cpp hitParameters.h)
target_link_libraries(mdEngineFirmwareTest PRIVATE mdEngine mdProtocol)
set(MD_ENGINE_HITS "${CMAKE_CURRENT_BINARY_DIR}/mdEngineFirmwareHits.bin")
set(MD_ENGINE_ROM_BANK "${CMAKE_CURRENT_BINARY_DIR}/mdEngineFirmwareRomBank.bin")
set(MD_ENGINE_GROUPS "${CMAKE_CURRENT_BINARY_DIR}/mdEngineFirmwareGroups.bin")
set(MD_ENGINE_MASTER "${CMAKE_CURRENT_BINARY_DIR}/mdEngineFirmwareMaster.bin")
add_test(NAME mdEngineFirmwareHits COMMAND mdTrigLatencyFirmwareTest --hits ${MD_ENGINE_HITS} 16 17 37 66 128 129 149)
add_test(NAME mdEngineFirmwareRomBank COMMAND mdTrigLatencyFirmwareTest --dsp2 ${MD_ENGINE_ROM_BANK} 147e00 190000)
add_test(NAME mdEngineFirmwareGroups COMMAND mdTrigLatencyFirmwareTest --groups ${MD_ENGINE_GROUPS})
add_test(NAME mdEngineFirmwareMaster COMMAND mdTrigLatencyFirmwareTest --master ${MD_ENGINE_MASTER})
add_test(NAME mdEngineFirmwareTest COMMAND mdEngineFirmwareTest ${MD_ENGINE_HITS} ${MD_ENGINE_ROM_BANK}
	${MD_ENGINE_GROUPS} ${MD_ENGINE_MASTER})
set_tests_properties(mdEngineFirmwareHits mdEngineFirmwareRomBank mdEngineFirmwareGroups mdEngineFirmwareMaster PROPERTIES
	LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 300 FIXTURES_SETUP MdEngineHits)
set_tests_properties(mdEngineFirmwareTest PROPERTIES
	LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 120 FIXTURES_REQUIRED MdEngineHits)
set_property(TARGET mdEngineFirmwareTest PROPERTY FOLDER "Elektron/test")
