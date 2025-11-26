cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(myfs VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=64 -no-pie")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall --pedantic -g")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

find_package(FUSE REQUIRED)
include_directories(${FUSE_INCLUDE_DIR} ./include)

file(GLOB DIR_SRCS "./src/*.c" "./src/*.cpp") 

add_executable(myfs ${DIR_SRCS})

message("FUSE_INCLUDE_DIR ${FUSE_INCLUDE_DIR}")
message("FUSE_LIBRARIES ${FUSE_LIBRARIES}")
message("DIR_SRCS ${DIR_SRCS}")

target_link_libraries(myfs ${FUSE_LIBRARIES} $ENV{HOME}/lib/libddriver.a)