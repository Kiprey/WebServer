cmake_minimum_required(VERSION 3.10)

# Project name
project(server)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -I/usr/include/postgresql")

# Enable Debug flags if Debug build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Debug build type")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -ggdb3 -O0")
endif()

if (NOT ENABLE_LOG EQUAL "1")
    message("Disabling logging")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG")
endif()

# Enable AddressSanitizer
if(ASAN EQUAL "1")
    message("Enabling AddressSanitizer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

# Find all source files
file(GLOB SOURCES "*.cpp")

# Set the output directory for the executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} pthread pqxx)
