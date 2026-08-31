#pragma once

#include <codecvt>
#include <locale>
#include <regex>
#include <string>
#include <vector>

class UnicodeRegex {
public:
    explicit UnicodeRegex(const std::string &pattern) {
        try {
            regex_ = std::wregex(toWide(pattern));
            valid_ = true;
        } catch (const std::regex_error &e) {
            error_ = e.what();
        } catch (const std::range_error &e) {
            error_ = e.what();
        }
    }

    bool isValid() const { return valid_; }
    const std::string &error() const { return error_; }

    bool search(const std::string &text,
                std::vector<std::string> &matches) const {
        matches.clear();
        if (!valid_) {
            return false;
        }

        try {
            const std::wstring wide_text = toWide(text);
            std::wsmatch match;
            if (!std::regex_search(wide_text, match, regex_)) {
                return false;
            }
            matches.reserve(match.size());
            for (const auto &part : match) {
                matches.push_back(toUtf8(part.str()));
            }
            return true;
        } catch (const std::range_error &) {
            return false;
        }
    }

private:
    static std::wstring toWide(const std::string &value) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(value);
    }

    static std::string toUtf8(const std::wstring &value) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(value);
    }

    std::wregex regex_;
    bool valid_ = false;
    std::string error_;
};
