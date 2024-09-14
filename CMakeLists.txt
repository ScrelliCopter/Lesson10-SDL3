cmake_minimum_required(VERSION "3.15" FATAL_ERROR)
project(Lesson10 LANGUAGES C)

find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

add_executable(Lesson10 Lesson10.c)
set_target_properties(Lesson10 PROPERTIES
	C_STANDARD 99
	WIN32_EXECUTABLE ON)
target_link_libraries(Lesson10 SDL3::SDL3 OpenGL::GL)
target_compile_options(Lesson10 PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -pedantic>)
target_compile_definitions(Lesson10 PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	get_property(SDL3_IMPORTED_LOCATION TARGET SDL3::SDL3 PROPERTY IMPORTED_LOCATION)
	if (SDL3_IMPORTED_LOCATION MATCHES "^/Library/Frameworks/")
		set_property(TARGET Lesson10 PROPERTY BUILD_RPATH "/Library/Frameworks")
	endif()
endif()
