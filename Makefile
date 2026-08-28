# Host tests for the M5Paper Color port's pure logic.
#
# Same rule as the Waveshare tree it came from: nothing here builds firmware.
# These modules are IDF-free and board-free on purpose, which is exactly why
# the port was tractable -- the whole decision core moved across untouched and
# was green before a single line of M5Stack code existed.
#
# If `make test` stops compiling, something pulled a board header into core/.
# That is the bug, not the Makefile.

CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O1 -g
BUILD   := build-host
CORE    := main/core
CJSON_DIR ?= $(HOME)/esp/esp-idf/components/json/cJSON

.PHONY: test clean
test: $(BUILD)/run_tests $(BUILD)/link_check
	@$(BUILD)/link_check
	@$(BUILD)/run_tests

SRC := tests/run_tests.c $(CORE)/riddle_decide.c $(CORE)/wake_log.c \
       $(CORE)/kids.c $(CORE)/weather.c $(CORE)/schedule.c \
       $(CORE)/sd_json.c $(CORE)/daily_layout.c \
       $(CORE)/he_text.c $(CJSON_DIR)/cJSON.c
HDR := $(wildcard $(CORE)/*.h)

$(BUILD)/run_tests: $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -I$(CORE) -I$(CJSON_DIR) -o $@ $(SRC) -lm

# C++ linkage guard. main.cpp will be C++ (M5Unified is a C++ library) while
# core/ stays C, so a missing extern "C" would break the link exactly as it did
# on the Waveshare board -- where --gc-sections hid it through four clean
# builds. Compile the .c files AS C, then link against a C++ object.
PURE   := $(CORE)/riddle_decide.c $(CORE)/wake_log.c $(CORE)/kids.c \
          $(CORE)/weather.c $(CORE)/schedule.c $(CORE)/sd_json.c \
          $(CORE)/daily_layout.c $(CORE)/he_text.c
PURE_O := $(BUILD)/riddle_decide.o $(BUILD)/wake_log.o $(BUILD)/kids.o \
          $(BUILD)/weather.o $(BUILD)/schedule.o $(BUILD)/sd_json.o \
          $(BUILD)/daily_layout.o $(BUILD)/he_text.o $(BUILD)/cJSON.o

$(BUILD)/%.o: $(CORE)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(CORE) -I$(CJSON_DIR) -c -o $@ $<

$(BUILD)/cJSON.o: $(CJSON_DIR)/cJSON.c | $(BUILD)
	$(CC) -std=c99 -O1 -I$(CJSON_DIR) -c -o $@ $<

$(BUILD)/link_check: tests/link_check.cc $(PURE_O) $(HDR) | $(BUILD)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -O1 -I$(CORE) -I$(CJSON_DIR) \
	    -o $@ tests/link_check.cc $(PURE_O) -lm

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
