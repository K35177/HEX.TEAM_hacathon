CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude
BUILD_DIR := build
CORE_SOURCES := src/audio/wav.cpp src/protocol/crc32.cpp src/protocol/packet.cpp src/modem/fsk.cpp src/transfer/transfer.cpp

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

test: $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/wav_tests $(BUILD_DIR)/fsk_tests $(BUILD_DIR)/transfer_tests $(BUILD_DIR)/acoustic-transfer
	./$(BUILD_DIR)/protocol_tests
	./$(BUILD_DIR)/wav_tests
	./$(BUILD_DIR)/fsk_tests
	./$(BUILD_DIR)/transfer_tests
	./$(BUILD_DIR)/acoustic-transfer self-test

clean:
	rm -f $(BUILD_DIR)/acoustic-transfer $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/wav_tests $(BUILD_DIR)/fsk_tests $(BUILD_DIR)/transfer_tests
