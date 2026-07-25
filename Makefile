CXX ?= c++
PYTHON ?= python
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread -Iinclude
BUILD_DIR := build
PACKAGE_DIR := dist/AcousticFileTransfer
EXEEXT :=
CORE_SOURCES := src/audio/audio_device.cpp src/audio/audio_processing.cpp src/audio/wav.cpp src/protocol/crc32.cpp src/protocol/packet.cpp src/protocol/sha256.cpp src/protocol/fec.cpp src/modem/fsk.cpp src/modem/framing.cpp src/modem/channel_simulator.cpp src/modem/profile.cpp src/transfer/transfer.cpp

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
LDLIBS += -lwinmm
CLIENT_LDFLAGS += -mwindows
else ifeq ($(shell uname -s),Darwin)
LDLIBS += -framework AudioToolbox
endif

.PHONY: all test package onefile clean

all: $(BUILD_DIR)/acoustic-transfer$(EXEEXT) $(BUILD_DIR)/acoustic-client$(EXEEXT)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/acoustic-transfer$(EXEEXT): src/main.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/acoustic-client$(EXEEXT): src/client_main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(CLIENT_LDFLAGS) -o $@

package: all
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(PACKAGE_DIR)/ui','$(PACKAGE_DIR)/config' | Out-Null"
	powershell -NoProfile -Command "Copy-Item -Force '$(BUILD_DIR)/acoustic-transfer.exe','$(BUILD_DIR)/acoustic-client.exe' '$(PACKAGE_DIR)'"
	powershell -NoProfile -Command "Copy-Item -Force 'ui/acoustic_ui.py' '$(PACKAGE_DIR)/ui/'"
	powershell -NoProfile -Command "Copy-Item -Force 'config/*.conf' '$(PACKAGE_DIR)/config/'"
	powershell -NoProfile -Command "Copy-Item -Force 'README.md' '$(PACKAGE_DIR)/'"
else
	mkdir -p $(PACKAGE_DIR)/ui $(PACKAGE_DIR)/config
	cp $(BUILD_DIR)/acoustic-transfer $(BUILD_DIR)/acoustic-client $(PACKAGE_DIR)/
	cp ui/acoustic_ui.py $(PACKAGE_DIR)/ui/
	cp config/*.conf $(PACKAGE_DIR)/config/
	cp README.md $(PACKAGE_DIR)/
endif

onefile: all
ifeq ($(OS),Windows_NT)
	$(PYTHON) -m PyInstaller --noconfirm --clean --distpath dist --workpath build/pyinstaller packaging/AcousticFileTransfer.spec
else
	@echo "onefile target must be built separately on Windows"
	@exit 1
endif

$(BUILD_DIR)/protocol_tests$(EXEEXT): tests/protocol_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/wav_tests$(EXEEXT): tests/wav_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/fsk_tests$(EXEEXT): tests/fsk_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/transfer_tests$(EXEEXT): tests/transfer_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/framing_tests$(EXEEXT): tests/framing_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/sha256_tests$(EXEEXT): tests/sha256_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/profile_tests$(EXEEXT): tests/profile_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/fec_tests$(EXEEXT): tests/fec_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/audio_processing_tests$(EXEEXT): tests/audio_processing_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/channel_simulator_tests$(EXEEXT): tests/channel_simulator_tests.cpp $(CORE_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

test: $(BUILD_DIR)/protocol_tests$(EXEEXT) $(BUILD_DIR)/wav_tests$(EXEEXT) $(BUILD_DIR)/fsk_tests$(EXEEXT) $(BUILD_DIR)/transfer_tests$(EXEEXT) $(BUILD_DIR)/framing_tests$(EXEEXT) $(BUILD_DIR)/sha256_tests$(EXEEXT) $(BUILD_DIR)/audio_processing_tests$(EXEEXT) $(BUILD_DIR)/profile_tests$(EXEEXT) $(BUILD_DIR)/fec_tests$(EXEEXT) $(BUILD_DIR)/channel_simulator_tests$(EXEEXT) $(BUILD_DIR)/acoustic-transfer$(EXEEXT)
	./$(BUILD_DIR)/protocol_tests$(EXEEXT)
	./$(BUILD_DIR)/wav_tests$(EXEEXT)
	./$(BUILD_DIR)/fsk_tests$(EXEEXT)
	./$(BUILD_DIR)/transfer_tests$(EXEEXT)
	./$(BUILD_DIR)/framing_tests$(EXEEXT)
	./$(BUILD_DIR)/sha256_tests$(EXEEXT)
	./$(BUILD_DIR)/audio_processing_tests$(EXEEXT)
	./$(BUILD_DIR)/profile_tests$(EXEEXT)
	./$(BUILD_DIR)/fec_tests$(EXEEXT)
	./$(BUILD_DIR)/channel_simulator_tests$(EXEEXT)
	./$(BUILD_DIR)/acoustic-transfer$(EXEEXT) self-test

clean:
	rm -f $(BUILD_DIR)/acoustic-transfer$(EXEEXT) $(BUILD_DIR)/acoustic-client$(EXEEXT) $(BUILD_DIR)/protocol_tests$(EXEEXT) $(BUILD_DIR)/wav_tests$(EXEEXT) $(BUILD_DIR)/fsk_tests$(EXEEXT) $(BUILD_DIR)/transfer_tests$(EXEEXT) $(BUILD_DIR)/framing_tests$(EXEEXT) $(BUILD_DIR)/sha256_tests$(EXEEXT) $(BUILD_DIR)/audio_processing_tests$(EXEEXT) $(BUILD_DIR)/profile_tests$(EXEEXT) $(BUILD_DIR)/fec_tests$(EXEEXT) $(BUILD_DIR)/channel_simulator_tests$(EXEEXT)
