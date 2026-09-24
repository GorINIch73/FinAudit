#pragma once
#include <string>
#include <vector>

namespace OdsExporter {
void Write(const std::string& path, const std::vector<std::string>& columns,
           const std::vector<std::vector<std::string>>& rows,
           const std::vector<int>& columnTypes = {});
// Returns a user-visible status, including the path if opening fails.
std::string Open(const std::vector<std::string>& columns,
                 const std::vector<std::vector<std::string>>& rows,
                 const std::vector<int>& columnTypes = {});
}
