#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef _WINDOWS_
extern "C" __declspec(dllimport) void *__stdcall
ShellExecuteA(void *, const char *, const char *, const char *, const char *,
              int);
#endif

inline const char *platform_strcasestr(const char *haystack,
                                       const char *needle) {
    if (!haystack || !needle) {
        return nullptr;
    }
    if (*needle == '\0') {
        return haystack;
    }

    std::string haystack_lower(haystack);
    std::string needle_lower(needle);
    auto lower = [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    };
    std::transform(haystack_lower.begin(), haystack_lower.end(),
                   haystack_lower.begin(), lower);
    std::transform(needle_lower.begin(), needle_lower.end(),
                   needle_lower.begin(), lower);

    const auto pos = haystack_lower.find(needle_lower);
    return pos == std::string::npos ? nullptr : haystack + pos;
}

#define strcasestr platform_strcasestr
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
