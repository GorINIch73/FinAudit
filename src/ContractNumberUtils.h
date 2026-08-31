#pragma once

#include <cctype>
#include <string>

inline std::string normalize_contract_number(const std::string &number) {
    std::string result;
    result.reserve(number.size());

    for (size_t i = 0; i < number.size();) {
        const unsigned char c = static_cast<unsigned char>(number[i]);
        if (c < 0x80) {
            if (!std::isspace(c)) {
                result.push_back(static_cast<char>(c));
            }
            ++i;
            continue;
        }

        size_t length = 1;
        if ((c & 0xE0) == 0xC0) length = 2;
        else if ((c & 0xF0) == 0xE0) length = 3;
        else if ((c & 0xF8) == 0xF0) length = 4;
        if (i + length > number.size()) length = 1;

        const std::string character = number.substr(i, length);
        const bool unicode_space =
            character == "\xC2\xA0" || character == "\xE2\x80\x80" ||
            character == "\xE2\x80\x81" || character == "\xE2\x80\x82" ||
            character == "\xE2\x80\x83" || character == "\xE2\x80\x84" ||
            character == "\xE2\x80\x85" || character == "\xE2\x80\x86" ||
            character == "\xE2\x80\x87" || character == "\xE2\x80\x88" ||
            character == "\xE2\x80\x89" || character == "\xE2\x80\x8A" ||
            character == "\xE2\x80\xAF" || character == "\xE2\x81\x9F" ||
            character == "\xE3\x80\x80";
        if (!unicode_space) {
            result.append(character);
        }
        i += length;
    }
    return result;
}
