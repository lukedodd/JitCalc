cmake_minimum_required(VERSION 2.8)
project(PIXSLAM)

add_subdirectory(libs)

# C++11
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11") 

include_directories(${PIXSLAM_SOURCE_DIR} ${PIXSLAM_SOURCE_DIR}/libs/asmjit/src)

add_executable(jitcalc main.cpp)

target_link_libraries(jitcalc asmjit)
