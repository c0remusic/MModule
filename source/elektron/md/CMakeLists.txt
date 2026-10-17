cmake_minimum_required(VERSION 3.15)

add_subdirectory(mdProtocol)
add_subdirectory(mdLib)
add_subdirectory(mdEngine)
add_subdirectory(mdDrums)

if(BUILD_TESTING)
	add_subdirectory(mdLibTest)
endif()

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	add_subdirectory(mdDrumsPlugin)
endif()
