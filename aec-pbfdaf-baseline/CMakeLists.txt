cmake_minimum_required(VERSION 3.20)
project(EchoCancel)

set(CMAKE_CXX_STANDARD 17)

# Add AEC Core Library
add_subdirectory(src/aec_core)

# Add GUI App
add_subdirectory(src/app_gui)
