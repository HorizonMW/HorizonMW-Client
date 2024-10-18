#pragma once

#include <string>
#ifndef CLANTAGS_UTILS_H
#define CLANTAGS_UTILS_H

static inline bool is_hex_in_range(char c, int start, int end) {
    return (static_cast<unsigned char>(c) >= start && static_cast<unsigned char>(c) <= end);
}

static inline bool contains_hex_01_to_20(const std::string& str) {
    for (char c : str) {
        if (is_hex_in_range(c, 0x01, 0x20)) {
            return true;
        }
    }
    return false;
}

#endif

