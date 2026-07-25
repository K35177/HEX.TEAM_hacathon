cmake_minimum_required(VERSION 3.16)
project(acoustic_file_transfer VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(acoustic_core
    src/audio/audio_device.cpp
    src/audio/audio_processing.cpp
    src/audio/wav.cpp
    src/protocol/crc32.cpp
    src/protocol/packet.cpp
    src/protocol/sha256.cpp
    src/modem/fsk.cpp
    src/modem/framing.cpp
    src/modem/channel_simulator.cpp
    src/modem/profile.cpp
    src/transfer/transfer.cpp
)
target_include_directories(acoustic_core PUBLIC include)
target_link_libraries(acoustic_core PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(acoustic_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(acoustic_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(acoustic_core PUBLIC winmm)
elseif(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    if(NOT AUDIOTOOLBOX_FRAMEWORK)
        message(FATAL_ERROR "AudioToolbox framework was not found")
    endif()
    target_link_libraries(acoustic_core PUBLIC ${AUDIOTOOLBOX_FRAMEWORK})
endif()

add_executable(acoustic-transfer src/main.cpp)
target_link_libraries(acoustic-transfer PRIVATE acoustic_core)
if(MSVC)
    target_compile_options(acoustic-transfer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(acoustic-transfer PRIVATE -Wall -Wextra -Wpedantic)
endif()

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

add_executable(audio_processing_tests tests/audio_processing_tests.cpp)
target_link_libraries(audio_processing_tests PRIVATE acoustic_core)
add_test(NAME audio_processing_tests COMMAND audio_processing_tests)

add_executable(profile_tests tests/profile_tests.cpp)
target_link_libraries(profile_tests PRIVATE acoustic_core)
add_test(NAME profile_tests COMMAND profile_tests)

add_executable(channel_simulator_tests tests/channel_simulator_tests.cpp)
target_link_libraries(channel_simulator_tests PRIVATE acoustic_core)
add_test(NAME channel_simulator_tests COMMAND channel_simulator_tests)

# The test programs intentionally use assert(). Keep those checks enabled in
# Release configurations as well.
set(ACOUSTIC_TEST_TARGETS
    protocol_tests wav_tests fsk_tests transfer_tests framing_tests
    sha256_tests audio_processing_tests profile_tests channel_simulator_tests)
foreach(test_target IN LISTS ACOUSTIC_TEST_TARGETS)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /UNDEBUG /utf-8)
    else()
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
endforeach()
