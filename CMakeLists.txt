cmake_minimum_required(VERSION 3.16)
project(acoustic_file_transfer VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(acoustic_core
    src/audio/audio_device.cpp
    src/audio/wav.cpp
    src/protocol/crc32.cpp
    src/protocol/packet.cpp
    src/protocol/sha256.cpp
    src/modem/fsk.cpp
    src/modem/framing.cpp
    src/transfer/transfer.cpp
)
target_include_directories(acoustic_core PUBLIC include)
target_link_libraries(acoustic_core PUBLIC Threads::Threads)
target_compile_options(acoustic_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(acoustic-transfer src/main.cpp)
target_link_libraries(acoustic-transfer PRIVATE acoustic_core)
target_compile_options(acoustic-transfer PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(protocol_tests tests/protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE acoustic_core)
add_test(NAME protocol_tests COMMAND protocol_tests)

add_executable(wav_tests tests/wav_tests.cpp)
target_link_libraries(wav_tests PRIVATE acoustic_core)
add_test(NAME wav_tests COMMAND wav_tests)

add_executable(fsk_tests tests/fsk_tests.cpp)
target_link_libraries(fsk_tests PRIVATE acoustic_core)
add_test(NAME fsk_tests COMMAND fsk_tests)

add_executable(transfer_tests tests/transfer_tests.cpp)
target_link_libraries(transfer_tests PRIVATE acoustic_core)
add_test(NAME transfer_tests COMMAND transfer_tests)

add_executable(framing_tests tests/framing_tests.cpp)
target_link_libraries(framing_tests PRIVATE acoustic_core)
add_test(NAME framing_tests COMMAND framing_tests)

add_executable(sha256_tests tests/sha256_tests.cpp)
target_link_libraries(sha256_tests PRIVATE acoustic_core)
add_test(NAME sha256_tests COMMAND sha256_tests)
