CXX ?= c++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude
DBGFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Iinclude
TSANFLAGS ?= -std=c++17 -O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Iinclude

BUILD_DIR := build

.PHONY: all test bench pipeline tsan clean

all: test bench pipeline

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/order_book_tests: tests/order_book_tests.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(DBGFLAGS) $< -o $@

$(BUILD_DIR)/latency_bench: src/latency_bench.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/pipeline_demo: src/pipeline_demo.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/tsan_pipeline: src/pipeline_demo.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(TSANFLAGS) $< -o $@

test: $(BUILD_DIR)/order_book_tests
	./$(BUILD_DIR)/order_book_tests

bench: $(BUILD_DIR)/latency_bench
	./$(BUILD_DIR)/latency_bench

pipeline: $(BUILD_DIR)/pipeline_demo
	./$(BUILD_DIR)/pipeline_demo

tsan: $(BUILD_DIR)/tsan_pipeline
	./$(BUILD_DIR)/tsan_pipeline 10000

clean:
	rm -rf $(BUILD_DIR)
