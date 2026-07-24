cmake_minimum_required(VERSION 3.16)
project(acoustic_file_transfer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(acoustic_core
    src/protocol/crc32.cpp
    src/protocol/packet.cpp
    src/modem/fsk.cpp
)
target_include_directories(acoustic_core PUBLIC include)
target_compile_options(acoustic_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(acoustic-transfer src/main.cpp)
target_link_libraries(acoustic-transfer PRIVATE acoustic_core)
target_compile_options(acoustic-transfer PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(protocol_tests tests/protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE acoustic_core)
add_test(NAME protocol_tests COMMAND protocol_tests)
