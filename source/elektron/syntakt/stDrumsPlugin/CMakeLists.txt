cmake_minimum_required(VERSION 3.15)

# ST Drums: syntakt::Syntakt's 16 digital tracks as an instrument plug-in, a plain JUCE AudioProcessor with JUCE's
# generic editor and a button for the firmware (stDrums plays it). Its own name and codes: manufacturer C0rm, plug-in
# Stdr.
project(stDrumsPlugin VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	stDrumsEditor.cpp stDrumsEditor.h
	stDrumsParameters.h
	stDrumsProcessor.cpp stDrumsProcessor.h
)

juce_add_plugin(stDrumsPlugin
	COMPANY_NAME "c0remusic"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	EDITOR_WANTS_KEYBOARD_FOCUS FALSE
	COPY_PLUGIN_AFTER_BUILD FALSE
	PLUGIN_MANUFACTURER_CODE C0rm
	PLUGIN_CODE Stdr
	PRODUCTS_FOLDER "${GEARMULATOR_JUCE_PRODUCTS_ROOT}/$<CONFIG>"
	FORMATS ${juce_formats}
	PRODUCT_NAME "ST Drums"
	VST3_AUTO_MANIFEST TRUE
	BUNDLE_ID "local.c0remusic.stdrums"
	LV2URI "urn:c0remusic:stdrums"
)

set_property(TARGET stDrumsPlugin PROPERTY ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shared-code/$<CONFIG>")

target_sources(stDrumsPlugin PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})
removeJuceDependencies(stDrumsPlugin)
target_compile_definitions(stDrumsPlugin PUBLIC JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_link_libraries(stDrumsPlugin PRIVATE stDrums juce_plugin_modules)
set_property(TARGET stDrumsPlugin PROPERTY FOLDER "Elektron")

if(BUILD_TESTING AND TARGET stDrumsPlugin_VST3)
	# The VST3 itself, loaded through JUCE's VST3 hosting as Live loads it, against syntakt::Syntakt driven directly
	# (SYNTAKT_OS_SYX: exit 77 without it)
	add_executable(stDrumsVst3HostTest stDrumsVst3HostTest.cpp)
	target_link_libraries(stDrumsVst3HostTest PRIVATE stDrums juce::juce_audio_processors juce::juce_gui_basics)
	target_compile_definitions(stDrumsVst3HostTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
		JUCE_STANDALONE_APPLICATION=1 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_AU=0
		JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
	add_dependencies(stDrumsVst3HostTest stDrumsPlugin_VST3)
	add_test(NAME stDrumsVst3HostTest COMMAND stDrumsVst3HostTest "$<TARGET_FILE:stDrumsPlugin_VST3>")
	set_tests_properties(stDrumsVst3HostTest PROPERTIES LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 300)
	set_property(TARGET stDrumsVst3HostTest PROPERTY FOLDER "Elektron/test")
endif()
