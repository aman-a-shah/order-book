#pragma once

#include <cctype>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lob/types.hpp"

namespace lob {

struct ReplayEvent {
    std::uint64_t timestamp_ns{0};
    OrderCommand command{};
};

inline bool parse_order_type(const std::string& token, OrderType& out) {
    if (token == "L" || token == "LIMIT") {
        out = OrderType::Limit;
    } else if (token == "M" || token == "MARKET") {
        out = OrderType::Market;
    } else if (token == "C" || token == "CANCEL") {
        out = OrderType::Cancel;
    } else if (token == "U" || token == "MODIFY") {
        out = OrderType::Modify;
    } else if (token == "R" || token == "REPLACE") {
        out = OrderType::Replace;
    } else {
        return false;
    }
    return true;
}

inline bool parse_side(const std::string& token, Side& out) {
    if (token == "B" || token == "BUY") {
        out = Side::Buy;
    } else if (token == "S" || token == "SELL") {
        out = Side::Sell;
    } else {
        return false;
    }
    return true;
}

inline std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

inline bool parse_replay_line(const std::string& line, ReplayEvent& event, std::string& error) {
    const auto hash_pos = line.find('#');
    const std::string without_comment = trim(line.substr(0, hash_pos));
    if (without_comment.empty()) {
        error.clear();
        return false;
    }

    std::vector<std::string> fields;
    std::stringstream ss(without_comment);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(trim(field));
    }
    if (fields.size() != 7 && fields.size() != 8) {
        error = "expected 7 or 8 comma-separated fields";
        return false;
    }

    OrderType type;
    Side side;
    if (!parse_order_type(fields[1], type)) {
        error = "unknown order type: " + fields[1];
        return false;
    }
    if (!parse_side(fields[2], side)) {
        error = "unknown side: " + fields[2];
        return false;
    }

    try {
        event.timestamp_ns = std::stoull(fields[0]);
        event.command = OrderCommand{
            type,
            side,
            std::stoull(fields[3]),
            static_cast<std::uint32_t>(std::stoul(fields[4])),
            static_cast<std::uint32_t>(std::stoul(fields[5])),
            std::stoull(fields[6]),
            fields.size() == 8 ? static_cast<std::uint32_t>(std::stoul(fields[7])) : 0U
        };
    } catch (const std::exception&) {
        error = "numeric parse failed";
        return false;
    }
    error.clear();
    return true;
}

inline bool load_replay_file(const std::string& path, std::vector<ReplayEvent>& events, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "could not open replay file: " + path;
        return false;
    }

    std::string line;
    std::size_t line_no = 0;
    while (std::getline(input, line)) {
        ++line_no;
        ReplayEvent event;
        std::string line_error;
        if (parse_replay_line(line, event, line_error)) {
            events.push_back(event);
        } else if (!line_error.empty()) {
            error = path + ":" + std::to_string(line_no) + ": " + line_error;
            return false;
        }
    }
    error.clear();
    return true;
}

}  // namespace lob
