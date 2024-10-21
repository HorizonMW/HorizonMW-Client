#pragma once

#include <string>
#ifndef CLANTAGS_UTILS_H
#define CLANTAGS_UTILS_H

static inline bool is_hex_in_range(char c, int start, int end) {
    return (static_cast<unsigned char>(c) >= start && static_cast<unsigned char>(c) <= end);
}

static inline bool contains_invalid_chars(const std::string& str) {
    static const std::string invalid_prefix = "^";
    static const std::string invalid_suffixes = "0123456789:";  

    for (std::size_t i = 0; i < str.length(); ++i) {
        if (is_hex_in_range(str[i], 0x01, 0x20)) {
            return true;
        }
        if (str[i] == '^' && i + 1 < str.length()) {
            char next_char = str[i + 1];
            if (invalid_suffixes.find(next_char) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

#endif
