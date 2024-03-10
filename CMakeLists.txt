cmake_minimum_required(VERSION "3.5" FATAL_ERROR)
project(Lesson10 LANGUAGES CXX)

find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

add_executable(Lesson10 Lesson10.cpp)
set_target_properties(Lesson10 PROPERTIES
	CXX_STANDARD 98
	WIN32_EXECUTABLE ON)
target_link_libraries(Lesson10 SDL3::SDL3 OpenGL::GL OpenGL::GLU)
target_compile_options(Lesson10 PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -pedantic>)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	get_property(SDL3_IMPORTED_LOCATION TARGET SDL3::SDL3 PROPERTY IMPORTED_LOCATION)
	if (SDL3_IMPORTED_LOCATION MATCHES "^/Library/Frameworks/")
		target_link_options(Lesson10 PRIVATE -Wl,-rpath,/Library/Frameworks)
	endif()
endif()
