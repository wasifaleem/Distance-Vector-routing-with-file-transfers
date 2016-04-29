cmake_minimum_required(VERSION 3.3)
project(assignment3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG -g -Wall -std=gnu++98")

include_directories(include)
file(GLOB_RECURSE INCLUDE_DIRECTORIES "include/*.h")
file(GLOB_RECURSE SOURCE_FILES "src/*.cpp")


add_executable (assignment3 ${INCLUDE_DIRECTORIES} ${SOURCE_FILES})