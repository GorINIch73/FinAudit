#include "OdsExporter.h"
#include "PlatformUtils.h"
#include <sqlite3.h>
#include <chrono>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace {
// Small ZIP writer using stored entries; no external archiver is needed.
void Little(std::ostream& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.put(static_cast<char>(value >> (8 * i)));
}
uint32_t Crc(const std::string& data) {
    uint32_t crc = 0xffffffff;
    for (unsigned char c : data) {
        crc ^= c;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
std::string Text(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case ' ': result += "<text:s/>"; break;
        case '\t': result += "<text:tab/>"; break;
        case '\n': result += "<text:line-break/>"; break;
        case '\r': result += "&#13;"; break;
        default: if (c >= 32) result += static_cast<char>(c); break;
        }
    }
    return result;
}
}

void OdsExporter::Write(const std::string& path,
                       const std::vector<std::string>& columns,
                       const std::vector<std::vector<std::string>>& rows,
                       const std::vector<int>& columnTypes) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" office:version=\"1.2\">"
        "<office:body><office:spreadsheet><table:table table:name=\"Результат\">";
    static const std::regex number(R"(-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)");
    auto row = [&](const std::vector<std::string>& values, bool header) {
        xml << "<table:table-row>";
        for (size_t i = 0; i < columns.size(); ++i) {
            const std::string value = i < values.size() ? values[i] : "";
            const bool numeric = !header && i < columnTypes.size() &&
                (columnTypes[i] == SQLITE_INTEGER || columnTypes[i] == SQLITE_FLOAT) &&
                std::regex_match(value, number) && value.size() <= 15;
            xml << "<table:table-cell office:value-type=\"" << (numeric ? "float" : "string") << "\"";
            if (numeric) xml << " office:value=\"" << value << "\"";
            xml << "><text:p>" << Text(value) << "</text:p></table:table-cell>";
        }
        xml << "</table:table-row>";
    };
    xml << "<table:table-header-rows>";
    row(columns, true);
    xml << "</table:table-header-rows>";
    for (const auto& values : rows) row(values, false);
    xml << "</table:table></office:spreadsheet></office:body></office:document-content>";
    struct Entry { std::string name, data; uint32_t crc = 0, offset = 0; };
    std::vector<Entry> entries = {
        {"mimetype", "application/vnd.oasis.opendocument.spreadsheet"},
        {"content.xml", xml.str()},
        {"META-INF/manifest.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
         "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.spreadsheet\"/>"
         "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
         "</manifest:manifest>"}
    };
    uint64_t size = 22;
    for (const auto& entry : entries) size += entry.data.size() + 76 + 2 * entry.name.size();
    if (size > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("Таблица слишком велика для ODS.");
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    platformOpenOutputFile(out, path, std::ios::binary | std::ios::out);
    for (auto& entry : entries) {
        entry.offset = static_cast<uint32_t>(out.tellp());
        entry.crc = Crc(entry.data);
        Little(out, 0x04034b50, 4); Little(out, 20, 2);
        Little(out, 0, 2); Little(out, 0, 2); Little(out, 0, 2); Little(out, 33, 2);
        Little(out, entry.crc, 4); Little(out, entry.data.size(), 4); Little(out, entry.data.size(), 4);
        Little(out, entry.name.size(), 2); Little(out, 0, 2);
        out << entry.name << entry.data;
    }
    const uint32_t start = static_cast<uint32_t>(out.tellp());
    for (const auto& entry : entries) {
        Little(out, 0x02014b50, 4); Little(out, 20, 2); Little(out, 20, 2);
        Little(out, 0, 2); Little(out, 0, 2); Little(out, 0, 2); Little(out, 33, 2);
        Little(out, entry.crc, 4); Little(out, entry.data.size(), 4); Little(out, entry.data.size(), 4);
        Little(out, entry.name.size(), 2);
        for (int i = 0; i < 4; ++i) Little(out, 0, 2);
        Little(out, 0, 4); Little(out, entry.offset, 4);
        out << entry.name;
    }
    const uint32_t end = static_cast<uint32_t>(out.tellp());
    Little(out, 0x06054b50, 4); Little(out, 0, 2); Little(out, 0, 2);
    Little(out, entries.size(), 2); Little(out, entries.size(), 2);
    Little(out, end - start, 4); Little(out, start, 4); Little(out, 0, 2);
    out.close();
}

std::string OdsExporter::Open(const std::vector<std::string>& columns,
                              const std::vector<std::vector<std::string>>& rows,
                              const std::vector<int>& columnTypes) {
    try {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto path = (std::filesystem::temp_directory_path() /
            ("fnaudit_query_" + std::to_string(stamp) + ".ods")).u8string();
        Write(path, columns, rows, columnTypes);
        if (!platformOpen(path)) return "Файл создан, но открыть его не удалось: " + path;
        return "ODS создан: " + path;
    } catch (const std::exception& e) {
        return std::string("Не удалось создать ODS: ") + e.what();
    }
}
