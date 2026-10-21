cmake_minimum_required(VERSION 3.15)

# The Machinedrum's engines without its ColdFire OS: Machinemodule's engine (sd88me/mpc-vst-machinedrum, AGPL-3.0),
# imported at 796ded6 with the changes listed in README.md, built on this repository's dsp56300 and mc68k.
project(mdEngine)

add_library(mdEngine STATIC
	engine/Dsp56.h
	engine/HostModel.cpp engine/HostModel.h
	engine/MachineRunner.cpp engine/MachineRunner.h
	engine/MasterEngine.cpp engine/MasterEngine.h
	engine/MdEngine.h
	engine/Mixer.cpp engine/Mixer.h
	engine/TrackFx.cpp engine/TrackFx.h
	engine/VoiceEngine.cpp engine/VoiceEngine.h
	engine/X20Builtins.cpp engine/X20Builtins.h
	tools/mdfw/Firmware.cpp tools/mdfw/Firmware.h
	tools/mdfw/Lzma.cpp tools/mdfw/Lzma.h
	tools/mdfw/X20Os.cpp tools/mdfw/X20Os.h
	tools/mds/MdsHost.cpp tools/mds/MdsHost.h
	tools/mds/MdsPackage.cpp tools/mds/MdsPackage.h
)
target_include_directories(mdEngine PUBLIC engine tools/mdfw tools/mds ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${CMAKE_CURRENT_SOURCE_DIR}/../../../mc68k)
target_link_libraries(mdEngine PUBLIC dsp56kEmu 68kEmu)
set_property(TARGET mdEngine PROPERTY FOLDER "Elektron")

# A User Machine of MDX X.20 (jmamma/MDS) played on a bare DSP56300, outside the plug-in
add_executable(mdsPlay tools/mds/mdsPlay.cpp)
target_link_libraries(mdsPlay PRIVATE mdEngine)
set_property(TARGET mdsPlay PROPERTY FOLDER "Elektron")
