cmake_minimum_required(VERSION 3.15)

# The voice DSP's machines ported to C++, without an emulator (ticket 02 of .scratch/elektron-drums-v1)
add_library(mdVoice STATIC mdVoice.h mdVoiceDsp.h mdVoiceInternal.h mdVoice.cpp mdVoiceRom.cpp mdVoiceTrx.h
	mdVoiceTrx.cpp mdVoiceTrxBd.cpp mdVoiceTrxSd.cpp mdVoiceTrxXt.cpp mdVoiceTrxCp.cpp mdVoiceTrxRs.cpp
	mdVoiceTrxCb.cpp mdVoiceTrxCh.cpp mdVoiceTrxCy.cpp mdVoiceTrxMa.cpp mdVoiceTrxCl.cpp mdVoiceTrxXc.cpp
	mdVoiceTrxB2.cpp mdVoiceTrxS2.cpp mdVoiceEfm.h mdVoiceEfm.cpp mdVoiceEfmBd.cpp
	mdVoiceEfmSd.cpp mdVoiceEfmXt.cpp mdVoiceEfmCp.cpp mdVoiceEfmRs.cpp mdVoiceEfmCb.cpp mdVoiceEfmHh.cpp
	mdVoiceEfmCy.cpp mdVoiceE12.cpp mdVoiceGnd.cpp mdVoicePi.h mdVoicePiImport.h mdVoicePi.cpp mdVoicePiMetal.cpp
	mdVoiceCoefficients.h mdVoiceCoefficients.cpp mdVoiceTrack.h mdVoiceTrack.cpp)
target_include_directories(mdVoice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# These sources round doubles to the OS's table words: /fp:fast and link-time code generation must not move a rounding
if(MSVC)
	set_property(SOURCE mdVoiceCoefficients.cpp mdVoiceRom.cpp mdVoiceTrx.cpp APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
else()
	set_property(SOURCE mdVoiceCoefficients.cpp mdVoiceRom.cpp mdVoiceTrx.cpp APPEND PROPERTY COMPILE_OPTIONS
		-fno-fast-math -ffp-contract=off)
endif()
set_property(TARGET mdVoice PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	# The port against mdEngine's VoiceEngine, sample for sample (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(mdVoicePortTest mdVoicePortTest.cpp)
	target_link_libraries(mdVoicePortTest PRIVATE mdVoice mdEngine)
	set_property(TARGET mdVoicePortTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdVoicePortTest COMMAND mdVoicePortTest)
	set_tests_properties(mdVoicePortTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)

	# The OS's coefficient functions and smoothing against MachineRunner, then voices without it
	add_executable(mdVoiceCoefficientsTest mdVoiceCoefficientsTest.cpp)
	target_link_libraries(mdVoiceCoefficientsTest PRIVATE mdVoice mdEngine)
	set_property(TARGET mdVoiceCoefficientsTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdVoiceCoefficientsTest COMMAND mdVoiceCoefficientsTest)
	set_tests_properties(mdVoiceCoefficientsTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)

	# The ROM machines on our filters against the same on the DSP's, measured
	add_executable(mdVoiceRomFiltersTest mdVoiceRomFiltersTest.cpp)
	target_link_libraries(mdVoiceRomFiltersTest PRIVATE mdVoice mdEngine)
	target_include_directories(mdVoiceRomFiltersTest PRIVATE ../../syntakt/stFast)
	set_property(TARGET mdVoiceRomFiltersTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME mdVoiceRomFiltersTest COMMAND mdVoiceRomFiltersTest)
	set_tests_properties(mdVoiceRomFiltersTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
