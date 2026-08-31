#pragma once

#include <cctype>
#include <string>

inline std::string prepare_payment_purpose_for_contract_regex(
    const std::string &purpose) {
    std::string result = purpose;
    const std::string date_marker = "от";

    size_t pos = 0;
    while ((pos = result.find(date_marker, pos)) != std::string::npos) {
        size_t next = pos + date_marker.size();
        while (next < result.size() &&
               std::isspace(static_cast<unsigned char>(result[next]))) {
            ++next;
        }

        const bool attached_to_number =
            pos > 0 &&
            !std::isspace(static_cast<unsigned char>(result[pos - 1])) &&
            next < result.size() &&
            std::isdigit(static_cast<unsigned char>(result[next]));
        if (attached_to_number) {
            result.insert(pos, 1, ' ');
            ++pos;
        }
        pos += date_marker.size();
    }

    return result;
}
