#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

inline std::string utf8_casefold_ru(const std::string &value) {
    std::string result = value;
    for (size_t i = 0; i < result.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(result[i]);
        if (c >= 'A' && c <= 'Z') {
            result[i] = static_cast<char>(c + ('a' - 'A'));
        } else if (c == 0xD0 && i + 1 < result.size()) {
            unsigned char next = static_cast<unsigned char>(result[i + 1]);
            if (next == 0x81) { // Ё -> ё
                result[i] = static_cast<char>(0xD1);
                result[i + 1] = static_cast<char>(0x91);
                ++i;
            } else if (next >= 0x90 && next <= 0x9F) { // А-П -> а-п
                result[i + 1] = static_cast<char>(next + 0x20);
                ++i;
            } else if (next >= 0xA0 && next <= 0xAF) { // Р-Я -> р-я
                result[i] = static_cast<char>(0xD1);
                result[i + 1] = static_cast<char>(next - 0x20);
                ++i;
            }
        }
    }
    return result;
}

inline bool utf8_icontains(const std::string &haystack,
                           const std::string &needle) {
    if (needle.empty()) {
        return true;
    }
    return utf8_casefold_ru(haystack).find(utf8_casefold_ru(needle)) !=
           std::string::npos;
}

inline const char *platform_strcasestr(const char *haystack,
                                       const char *needle) {
    if (!haystack || !needle) {
        return nullptr;
    }
    const std::string folded_haystack = utf8_casefold_ru(haystack);
    const std::string folded_needle = utf8_casefold_ru(needle);
    const size_t pos = folded_haystack.find(folded_needle);
    return pos == std::string::npos ? nullptr : haystack + pos;
}

#ifdef _WIN32
#ifndef _WINDOWS_
extern "C" __declspec(dllimport) void *__stdcall
ShellExecuteA(void *, const char *, const char *, const char *, const char *,
              int);
#endif

#define strcasecmp _stricmp

inline bool platformOpen(const std::string &target) {
    void *result = ShellExecuteA(nullptr, "open", target.c_str(), nullptr,
                                 nullptr, 1);
    return reinterpret_cast<intptr_t>(result) > 32;
}

inline std::filesystem::path platformPathFromUtf8(const std::string &path) {
    return std::filesystem::u8path(path);
}

#else
#include <strings.h>

inline bool platformOpen(const std::string &target) {
    std::string command = "xdg-open \"" + target + "\"";
    return std::system(command.c_str()) == 0;
}

inline std::filesystem::path platformPathFromUtf8(const std::string &path) {
    return std::filesystem::path(path);
}
#endif

#define strcasestr platform_strcasestr

inline void platformOpenOrLog(const std::string &target,
                              const std::string &description) {
    if (!platformOpen(target)) {
        std::cerr << "Failed to open " << description << ": " << target
                  << std::endl;
    }
}

inline void platformOpenInputFile(std::ifstream &file,
                                  const std::string &path,
                                  std::ios::openmode mode = std::ios::in) {
    file.open(platformPathFromUtf8(path), mode);
}

inline void platformOpenOutputFile(std::ofstream &file,
                                   const std::string &path,
                                   std::ios::openmode mode = std::ios::out) {
    file.open(platformPathFromUtf8(path), mode);
}
