# The editor, after the Lockstep mockup (.scratch/elektron-drums/concepts/t.html), included by CMakeLists.txt: its
# sources in the plug-in, the plates it draws from (b0, b1, b2, sa at 1x and 2x, 1.8 MB), and its test
set(ED_DRUMS_EDITOR_SOURCES
	edDrumsEditor.cpp edDrumsEditor.h edDrumsEditorPages.cpp edDrumsEditorPages.h edDrumsEditorScreen.cpp)
target_sources(edDrumsPlugin PRIVATE ${ED_DRUMS_EDITOR_SOURCES})
source_group("editor" FILES ${ED_DRUMS_EDITOR_SOURCES})

set(ED_DRUMS_PLATES)
foreach(plate b0 b1 b2 sa)
	foreach(scale 1x 2x)
		list(APPEND ED_DRUMS_PLATES ${CMAKE_CURRENT_SOURCE_DIR}/resources/lockstep/${plate}_${scale}.jpg)
	endforeach()
endforeach()
juce_add_binary_data(edDrumsLockstepAssets HEADER_NAME LockstepAssets.h NAMESPACE LockstepAssets
	SOURCES ${ED_DRUMS_PLATES})
set_property(TARGET edDrumsLockstepAssets PROPERTY FOLDER "Elektron")
target_link_libraries(edDrumsPlugin PRIVATE edDrumsLockstepAssets)

if(BUILD_TESTING)
	# Headless: one control per parameter, a cell's drag in one gesture, a step key's click, each page to a PNG
	add_executable(edDrumsEditorTest edDrumsEditorTest.cpp)
	target_link_libraries(edDrumsEditorTest PRIVATE edDrumsPlugin edDrums mdProtocol juce_plugin_modules)
	target_compile_definitions(edDrumsEditorTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
		ED_DRUMS_NAME="${ED_DRUMS_NAME}")
	add_test(NAME edDrumsEditorTest COMMAND edDrumsEditorTest)
	set_tests_properties(edDrumsEditorTest PROPERTIES TIMEOUT 120)
	set_property(TARGET edDrumsEditorTest PROPERTY FOLDER "Elektron/test")
endif()
