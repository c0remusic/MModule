cmake_minimum_required(VERSION 3.15)

# The instrument as a VST3, a plain JUCE AudioProcessor with JUCE's generic editor until the interface's tickets. Its
# name is a working one, here alone: the product's is still open on the map of .scratch/elektron-drums-v1.
set(ED_DRUMS_NAME "Elektron Drums Dev")
project(edDrumsPlugin VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES edDrumsProcessor.cpp edDrumsProcessor.h)

juce_add_plugin(edDrumsPlugin
	COMPANY_NAME "c0remusic"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	EDITOR_WANTS_KEYBOARD_FOCUS FALSE
	COPY_PLUGIN_AFTER_BUILD FALSE
	PLUGIN_MANUFACTURER_CODE C0rm
	PLUGIN_CODE Eldr
	PRODUCTS_FOLDER "${GEARMULATOR_JUCE_PRODUCTS_ROOT}/$<CONFIG>"
	FORMATS VST3
	PRODUCT_NAME "${ED_DRUMS_NAME}"
	VST3_AUTO_MANIFEST TRUE
	BUNDLE_ID "local.c0remusic.eldrums"
)

set_property(TARGET edDrumsPlugin PROPERTY ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shared-code/$<CONFIG>")

target_sources(edDrumsPlugin PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})
removeJuceDependencies(edDrumsPlugin)
target_compile_definitions(edDrumsPlugin PUBLIC JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_compile_definitions(edDrumsPlugin PRIVATE ED_DRUMS_NAME="${ED_DRUMS_NAME}")
target_link_libraries(edDrumsPlugin PRIVATE edDrums mdProtocol juce_plugin_modules)
set_property(TARGET edDrumsPlugin PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	# With no flash image, needing none (it clears what leads to one)
	add_executable(edDrumsNoFlashTest edDrumsNoFlashTest.cpp)
	target_link_libraries(edDrumsNoFlashTest PRIVATE edDrumsPlugin edDrums mdProtocol juce_plugin_modules)
	target_compile_definitions(edDrumsNoFlashTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 ED_DRUMS_NAME="${ED_DRUMS_NAME}")
	add_test(NAME edDrumsNoFlashTest COMMAND edDrumsNoFlashTest)
	set_tests_properties(edDrumsNoFlashTest PROPERTIES TIMEOUT 300)
	set_property(TARGET edDrumsNoFlashTest PROPERTY FOLDER "Elektron/test")

	# What processBlock tells the engine of the host's transport (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(edDrumsProcessorTest edDrumsProcessorTest.cpp)
	target_link_libraries(edDrumsProcessorTest PRIVATE edDrumsPlugin edDrums mdProtocol juce_plugin_modules)
	target_compile_definitions(edDrumsProcessorTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 ED_DRUMS_NAME="${ED_DRUMS_NAME}")
	add_test(NAME edDrumsProcessorTest COMMAND edDrumsProcessorTest)
	set_tests_properties(edDrumsProcessorTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET edDrumsProcessorTest PROPERTY FOLDER "Elektron/test")
endif()

if(BUILD_TESTING AND TARGET edDrumsPlugin_VST3)
	# The VST3 itself, loaded through JUCE's VST3 hosting as Live loads it, against the engine driven directly
	# (GEARMULATOR_MD_FIRMWARE_BIN: exit 77 without it)
	add_executable(edDrumsVst3HostTest edDrumsVst3HostTest.cpp)
	target_link_libraries(edDrumsVst3HostTest PRIVATE edDrums juce::juce_audio_processors juce::juce_gui_basics)
	target_compile_definitions(edDrumsVst3HostTest PRIVATE ED_DRUMS_NAME="${ED_DRUMS_NAME}"
		JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0
		JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
	add_dependencies(edDrumsVst3HostTest edDrumsPlugin_VST3)
	add_test(NAME edDrumsVst3HostTest COMMAND edDrumsVst3HostTest "$<TARGET_FILE:edDrumsPlugin_VST3>")
	set_tests_properties(edDrumsVst3HostTest PROPERTIES LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 300)
	set_property(TARGET edDrumsVst3HostTest PROPERTY FOLDER "Elektron/test")
endif()
