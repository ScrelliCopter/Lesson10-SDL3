cmake_minimum_required(VERSION "3.5" FATAL_ERROR)
project(Lesson10 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 98)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(Lesson10 Lesson10.cpp)
target_link_libraries(Lesson10 SDL2::SDL2 OpenGL::GL)
target_compile_options(Lesson10 PRIVATE -Wall -Wextra -pedantic)
