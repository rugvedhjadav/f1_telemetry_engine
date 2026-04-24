cmake_minimum_required(VERSION 3.20)
project(F1_Telemetry_Server C)

set(CMAKE_C_STANDARD 11)

# Include the headers folder
include_directories(include)

# Build the Main Server Executable
add_executable(pitwall_server src/server.c)
# REMOVE 'rt' for macOS
target_link_libraries(pitwall_server pthread)

# Build the Car Simulator Executable
add_executable(car_client src/car_client.c)
target_link_libraries(car_client pthread)