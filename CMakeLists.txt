cmake_minimum_required(VERSION "3.15" FATAL_ERROR)
project(Lesson10 LANGUAGES C)

find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

set(SOURCES Lesson10.c)

set(DATA
	Data/Shader.vertex.metal
	Data/Shader.fragment.metal
	Data/Mud.bmp
	Data/World.txt)

add_executable(Lesson10 WIN32 MACOSX_BUNDLE ${SOURCES} ${DATA})
set_property(TARGET Lesson10 PROPERTY C_STANDARD 99)
source_group("Data\\Random" FILES ${DATA})
target_link_libraries(Lesson10 SDL3::SDL3 OpenGL::GL)
target_compile_options(Lesson10 PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -pedantic>)
target_compile_definitions(Lesson10 PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	get_property(SDL3_IMPORTED_LOCATION TARGET SDL3::SDL3 PROPERTY IMPORTED_LOCATION)
	if (SDL3_IMPORTED_LOCATION MATCHES "^/Library/Frameworks/")
		set_property(TARGET Lesson10 PROPERTY BUILD_RPATH "/Library/Frameworks")
	endif()
	foreach (RESOURCE IN LISTS DATA)
		set_source_files_properties("${RESOURCE}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Data")
	endforeach()
else()
	add_custom_command(TARGET Lesson10 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/Data" "$<TARGET_FILE_DIR:Lesson10>/Data")
	if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
		add_custom_command(TARGET Lesson10 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:Lesson10>)
	endif()
endif()
