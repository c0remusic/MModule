cmake_minimum_required(VERSION 3.15)

# Sophie: a metallic FM percussion voice ported from mestela/schwung-sophie (MIT, README.md, LICENSE), in C, a voice
# per caller, and its 19 parameters as machine values (sophieMachine). /fp:precise: the port renders the original's
# samples bit for bit when both are compiled alike, which /fp:fast's reordering and link-time code generation would
# not keep
add_library(sophie STATIC sophieVoice.h sophieVoice.c sophieMachine.h sophieMachine.cpp)
target_include_directories(sophie PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET sophie PROPERTY FOLDER "Elektron")
if(MSVC)
	target_compile_options(sophie PRIVATE /fp:precise /GL-)
else()
	target_compile_options(sophie PRIVATE -ffp-contract=off -fno-fast-math)
endif()

if(BUILD_TESTING)
	# The port against the original, compiled into the test from a clone outside the repository: SOPHIE_ORIGINAL_DIR
	# in the environment when configuring (kept in the cache), else the test exits 77 after the port's own checks
	if(DEFINED ENV{SOPHIE_ORIGINAL_DIR})
		set(SOPHIE_ORIGINAL_DIR "$ENV{SOPHIE_ORIGINAL_DIR}" CACHE PATH "Clone of mestela/schwung-sophie" FORCE)
	endif()
	add_executable(sophieTest sophieTest.cpp)
	target_link_libraries(sophieTest PRIVATE sophie)
	if(SOPHIE_ORIGINAL_DIR AND EXISTS "${SOPHIE_ORIGINAL_DIR}/src/dsp/sophie.c")
		target_sources(sophieTest PRIVATE sophieOriginal.c)
		set_source_files_properties(sophieOriginal.c PROPERTIES INCLUDE_DIRECTORIES "${SOPHIE_ORIGINAL_DIR}/src/dsp"
			COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
		target_compile_definitions(sophieTest PRIVATE SOPHIE_HAS_ORIGINAL=1)
	endif()
	if(MSVC)
		target_compile_options(sophieTest PRIVATE /fp:precise /GL-)
	else()
		target_compile_options(sophieTest PRIVATE -ffp-contract=off -fno-fast-math)
	endif()
	set_property(TARGET sophieTest PROPERTY FOLDER "Elektron/test")
	add_test(NAME sophieTest COMMAND sophieTest)
	set_tests_properties(sophieTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
