CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread -Iinclude
BUILD_DIR := build
CORE_SOURCES := src/audio/audio_device.cpp src/audio/audio_processing.cpp src/audio/wav.cpp src/protocol/crc32.cpp src/protocol/packet.cpp src/protocol/sha256.cpp src/modem/fsk.cpp src/modem/framing.cpp src/transfer/transfer.cpp

.PHONY: all test clean

all: $(BUILD_DIR)/acoustic-transfer

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/acoustic-transfer: src/main.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/protocol_tests: tests/protocol_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/wav_tests: tests/wav_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/fsk_tests: tests/fsk_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/transfer_tests: tests/transfer_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/framing_tests: tests/framing_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/sha256_tests: tests/sha256_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/audio_processing_tests: tests/audio_processing_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/wav_tests $(BUILD_DIR)/fsk_tests $(BUILD_DIR)/transfer_tests $(BUILD_DIR)/framing_tests $(BUILD_DIR)/sha256_tests $(BUILD_DIR)/audio_processing_tests $(BUILD_DIR)/acoustic-transfer
	./$(BUILD_DIR)/protocol_tests
	./$(BUILD_DIR)/wav_tests
	./$(BUILD_DIR)/fsk_tests
	./$(BUILD_DIR)/transfer_tests
	./$(BUILD_DIR)/framing_tests
	./$(BUILD_DIR)/sha256_tests
	./$(BUILD_DIR)/audio_processing_tests
	./$(BUILD_DIR)/acoustic-transfer self-test

clean:
	rm -f $(BUILD_DIR)/acoustic-transfer $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/wav_tests $(BUILD_DIR)/fsk_tests $(BUILD_DIR)/transfer_tests $(BUILD_DIR)/framing_tests $(BUILD_DIR)/sha256_tests $(BUILD_DIR)/audio_processing_tests
