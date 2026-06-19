CXX ?= c++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude
DBGFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Iinclude
TSANFLAGS ?= -std=c++17 -O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Iinclude

BUILD_DIR := build

.PHONY: all test bench pipeline replay replay-bench visualize tsan site clean

all: test bench pipeline replay

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/order_book_tests: tests/order_book_tests.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(DBGFLAGS) $< -o $@

$(BUILD_DIR)/protocol_tests: tests/protocol_tests.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(DBGFLAGS) $< -o $@

$(BUILD_DIR)/replay_tests: tests/replay_tests.cpp $(BUILD_DIR)/replay_runner tests/replay_expected.txt data/sample_replay.csv | $(BUILD_DIR)
	$(CXX) $(DBGFLAGS) $< -o $@

$(BUILD_DIR)/latency_bench: src/latency_bench.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/pipeline_demo: src/pipeline_demo.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/replay_runner: src/replay_runner.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/replay_bench: src/replay_bench.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/visualizer: src/visualizer.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/site_export: src/site_export.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/tsan_pipeline: src/pipeline_demo.cpp include/lob/*.hpp | $(BUILD_DIR)
	$(CXX) $(TSANFLAGS) $< -o $@

test: $(BUILD_DIR)/order_book_tests $(BUILD_DIR)/protocol_tests $(BUILD_DIR)/replay_tests
	./$(BUILD_DIR)/order_book_tests
	./$(BUILD_DIR)/protocol_tests
	./$(BUILD_DIR)/replay_tests

bench: $(BUILD_DIR)/latency_bench
	./$(BUILD_DIR)/latency_bench

pipeline: $(BUILD_DIR)/pipeline_demo
	./$(BUILD_DIR)/pipeline_demo

replay: $(BUILD_DIR)/replay_runner
	./$(BUILD_DIR)/replay_runner data/sample_replay.csv

replay-bench: $(BUILD_DIR)/replay_bench
	./$(BUILD_DIR)/replay_bench data/sample_replay.csv 100000

visualize: $(BUILD_DIR)/visualizer
	./$(BUILD_DIR)/visualizer data/sample_replay.csv

tsan: $(BUILD_DIR)/tsan_pipeline
	./$(BUILD_DIR)/tsan_pipeline 10000

site: $(BUILD_DIR)/site_export
	./$(BUILD_DIR)/site_export data/sample_replay.csv

clean:
	rm -rf $(BUILD_DIR)
