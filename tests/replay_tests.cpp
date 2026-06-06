#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main() {
    const std::string command = "./build/replay_runner data/sample_replay.csv";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        std::cerr << "FAIL: could not run replay runner\n";
        return 1;
    }

    std::ostringstream actual;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        actual << buffer;
    }
    const int status = pclose(pipe);
    if (status != 0) {
        std::cerr << "FAIL: replay runner exited with status " << status << '\n';
        return 1;
    }

    std::ifstream expected_file("tests/replay_expected.txt");
    std::ostringstream expected;
    expected << expected_file.rdbuf();

    if (actual.str() != expected.str()) {
        std::cerr << "FAIL: replay output mismatch\n";
        std::cerr << "Expected:\n" << expected.str();
        std::cerr << "Actual:\n" << actual.str();
        return 1;
    }

    std::cout << "Replay golden-file test passed\n";
    return 0;
}
