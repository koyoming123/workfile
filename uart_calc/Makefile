# UART Calculator - 模組化版本
#
#   make          編譯 uart_calc
#   make test     編譯並執行單元測試
#   make clean    清除編譯產物

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS += -D_GNU_SOURCE -Isrc
LDLIBS    = -pthread -lm

BUILD = build
SRCS  = $(wildcard src/*.c)
OBJS  = $(SRCS:src/%.c=$(BUILD)/%.o)

# 每個測試只連結它需要的模組
TEST_CALC_OBJS   = $(BUILD)/test_calc.o $(BUILD)/calc.o
TEST_CONFIG_OBJS = $(BUILD)/test_config.o $(BUILD)/config.o $(BUILD)/role.o $(BUILD)/util.o

uart_calc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# -MMD -MP：自動產生 .d 相依檔，修改 .h 時會自動重新編譯用到它的 .c
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: tests/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD):
	mkdir -p $@

$(BUILD)/test_calc: $(TEST_CALC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/test_config: $(TEST_CONFIG_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD)/test_calc $(BUILD)/test_config
	cd $(BUILD) && ./test_calc && ./test_config

clean:
	rm -rf $(BUILD) uart_calc

-include $(wildcard $(BUILD)/*.d)

.PHONY: test clean
