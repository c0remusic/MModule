cmake_minimum_required(VERSION 3.15)

# OPEN DRUM: four drum voices ported from a GPL-3.0-only firmware (README.md, NOTICE.md), in C, a voice per caller.
# /fp:precise: the reference renders are bit for bit those of the original compiled alike, which /fp:fast's
# reordering and link-time code generation would not keep
add_library(opendrum STATIC
	openDrum.h openDrum.c
	1PoleLp.h 1PoleLp.c BufferTools.h config.h CymbalVoice.h CymbalVoice.c datatypes.h Decay.h Decay.c
	distortion.h distortion.c DrumVoice.h DrumVoice.c globals.h HiHat.h HiHat.c InitKit.h initKit.c
	MidiNoteNumbers.h Oscillator.h Oscillator.c Parameters.h ResonantFilter.h ResonantFilter.c SlopeEg2.h SlopeEg2.c
	snapEg.h snapEg.c Snare.h Snare.c stm32f7xx_hal.h transientGenerator.h transientGenerator.c
	transientTables.h transientTables.c valueShaper.h VoiceParameters.h VoiceParameters.c wavetable.h wavetable.c)
target_include_directories(opendrum PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET opendrum PROPERTY FOLDER "Elektron")
if(MSVC)
	target_compile_options(opendrum PRIVATE /fp:precise /GL- /w)
else()
	target_compile_options(opendrum PRIVATE -ffp-contract=off -fno-fast-math -w)
endif()
