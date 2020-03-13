cmake_minimum_required(VERSION 3.1)

# Project name
project( FileWatch VERSION 0.0.1 LANGUAGES C CXX)

#option(BuildTests "Build the unit tests" ON)
#enable_testing()

# Enable c++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED on)

# Add debug flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# Update if necessary
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -pedantic")

# create and configure the unit test target
#if(BuildTests)
#    add_subdirectory(tests)
#endif()

add_executable(${PROJECT_NAME} main.cpp FileWatch.hpp)
target_link_libraries(${PROJECT_NAME} pthread)
