#include "SpecialQueryView.h"
#include "../IconsFontAwesome6.h"
#include "../PlatformUtils.h"
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace {
std::string EscapeHtml(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string TrimCopy(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    return first < last ? std::string(first, last) : "";
}

bool IsAmountLike(const std::string& value) {
    const std::string trimmed = TrimCopy(value);
    if (trimmed.empty()) {
        return false;
    }

    size_t pos = 0;
    if (trimmed[pos] == '+' || trimmed[pos] == '-') {
        ++pos;
    }

    bool has_integer_digit = false;
    while (pos < trimmed.size()) {
        const unsigned char ch = static_cast<unsigned char>(trimmed[pos]);
        if (std::isdigit(ch)) {
            has_integer_digit = true;
            ++pos;
            continue;
        }
        if (std::isspace(ch)) {
            ++pos;
            continue;
        }
        break;
    }

    if (!has_integer_digit) {
        return false;
    }

    if (pos == trimmed.size()) {
        return true;
    }

    if (trimmed[pos] != '.' && trimmed[pos] != ',') {
        return false;
    }

    ++pos;
    size_t decimal_digits = 0;
    while (pos < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[pos]))) {
        ++decimal_digits;
        ++pos;
    }

    return decimal_digits == 2 && pos == trimmed.size();
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsAmountColumnName(const std::string& column_name) {
    const std::string lower = ToLowerAscii(column_name);
    return column_name.find("сумм") != std::string::npos ||
           column_name.find("Сумм") != std::string::npos ||
           column_name.find("СУММ") != std::string::npos ||
           column_name.find("итого") != std::string::npos ||
           column_name.find("Итого") != std::string::npos ||
           column_name.find("ИТОГО") != std::string::npos ||
           column_name.find("остат") != std::string::npos ||
           column_name.find("Остат") != std::string::npos ||
           column_name.find("ОСТАТ") != std::string::npos ||
           column_name.find("оплат") != std::string::npos ||
           column_name.find("Оплат") != std::string::npos ||
           column_name.find("ОПЛАТ") != std::string::npos ||
           column_name.find("платеж") != std::string::npos ||
           column_name.find("Платеж") != std::string::npos ||
           column_name.find("ПЛАТЕЖ") != std::string::npos ||
           column_name.find("платёж") != std::string::npos ||
           column_name.find("Платёж") != std::string::npos ||
           column_name.find("ПЛАТЁЖ") != std::string::npos ||
           lower.find("amount") != std::string::npos ||
           lower.find("total") != std::string::npos ||
           lower.find("sum") != std::string::npos ||
           lower.find("balance") != std::string::npos ||
           lower.find("payment") != std::string::npos;
}

bool IsLongTextColumnName(const std::string& column_name) {
    const std::string lower = ToLowerAscii(column_name);
    return column_name.find("Назнач") != std::string::npos ||
           column_name.find("назнач") != std::string::npos ||
           column_name.find("Содерж") != std::string::npos ||
           column_name.find("содерж") != std::string::npos ||
           lower.find("description") != std::string::npos ||
           lower.find("payment_desc") != std::string::npos ||
           lower.find("details") != std::string::npos ||
           lower.find("content") != std::string::npos;
}

bool IsSQLiteNumericType(int type) {
    return type == SQLITE_INTEGER || type == SQLITE_FLOAT;
}

bool NeedsTextPrefixForSpreadsheet(
    const std::string& value,
    const std::string& column_name,
    int sqlite_type) {
    const std::string trimmed = TrimCopy(value);
    if (trimmed.empty()) {
        return false;
    }

    if (IsSQLiteNumericType(sqlite_type) ||
        (IsAmountColumnName(column_name) && IsAmountLike(trimmed))) {
        return false;
    }

    bool has_digit = false;
    for (unsigned char ch : trimmed) {
        if (std::isdigit(ch)) {
            has_digit = true;
            continue;
        }
        if (std::isspace(ch) || ch == '+' || ch == '-' || ch == '/' ||
            ch == '\\' || ch == '.' || ch == ',' || ch == ':' || ch == '#') {
            continue;
        }
        return false;
    }
    return has_digit;
}

std::string FormatCellForSpreadsheetCopy(
    const std::string& value,
    const std::string& column_name,
    int sqlite_type) {
    static const std::string zero_width_space = "\xE2\x80\x8B";
    if (NeedsTextPrefixForSpreadsheet(value, column_name, sqlite_type) &&
        value.rfind(zero_width_space, 0) != 0) {
        return zero_width_space + value;
    }
    return value;
}

std::string MakePrintableHtml(
    const std::string& title,
    const std::vector<std::string>& columns,
    const std::vector<std::vector<std::string>>& rows,
    const std::vector<double>& column_totals,
    const std::vector<bool>& is_numeric_column) {
    std::ostringstream html;
    html << "<!doctype html>\n"
         << "<html lang=\"ru\">\n"
         << "<head>\n"
         << "<meta charset=\"utf-8\">\n"
         << "<title>" << EscapeHtml(title) << "</title>\n"
         << "<style>\n"
         << "body{font-family:Arial,sans-serif;margin:24px;color:#111;}\n"
         << "h1{font-size:20px;margin:0 0 16px;}\n"
         << ".summary{font-size:13px;margin:0 0 12px;color:#333;}\n"
         << "table{border-collapse:collapse;width:100%;font-size:12px;}\n"
         << "th,td{border:1px solid #999;padding:5px 7px;vertical-align:top;}\n"
         << "th{background:#f1f1f1;text-align:left;}\n"
         << "tfoot td{font-weight:bold;background:#fafafa;}\n"
         << "@media print{body{margin:10mm;} table{font-size:10px;} th,td{padding:3px 5px;}}\n"
         << "</style>\n"
         << "</head>\n"
         << "<body>\n"
         << "<h1>" << EscapeHtml(title) << "</h1>\n"
         << "<div class=\"summary\">Количество записей: " << rows.size()
         << "</div>\n"
         << "<table>\n<thead>\n<tr>";

    for (const auto& column : columns) {
        html << "<th>" << EscapeHtml(column) << "</th>";
    }
    html << "</tr>\n</thead>\n<tbody>\n";

    for (const auto& row : rows) {
        html << "<tr>";
        for (size_t col = 0; col < columns.size(); ++col) {
            const std::string cell = col < row.size() ? row[col] : "";
            html << "<td>" << EscapeHtml(cell) << "</td>";
        }
        html << "</tr>\n";
    }

    bool has_totals = false;
    for (size_t col = 0; col < column_totals.size() && col < is_numeric_column.size(); ++col) {
        if (is_numeric_column[col] && column_totals[col] != 0.0) {
            has_totals = true;
            break;
        }
    }

    html << "</tbody>\n";
    if (has_totals) {
        html << "<tfoot>\n<tr>";
        for (size_t col = 0; col < columns.size(); ++col) {
            if (col < is_numeric_column.size() && col < column_totals.size() &&
                is_numeric_column[col] && column_totals[col] != 0.0) {
                html << "<td>Итого: " << std::fixed << std::setprecision(2)
                     << column_totals[col] << "</td>";
            } else {
                html << "<td></td>";
            }
        }
        html << "</tr>\n</tfoot>\n";
    }
    html << "</table>\n"
         << "<script>window.addEventListener('load',function(){window.print();});</script>\n"
         << "</body>\n"
         << "</html>\n";
    return html.str();
}
}

SpecialQueryView::SpecialQueryView(const std::string& title, const std::string& query)
    : query(query) {
    Title = title;
}

void SpecialQueryView::SetDatabaseManager(DatabaseManager *manager) {
    dbManager = manager;
    if (dbManager && dbManager->is_open()) {
        ExecuteQuery();
    }
}

void SpecialQueryView::SetPdfReporter(PdfReporter* reporter) {
    // Not used in this view
}

void SpecialQueryView::SetUIManager(UIManager* manager) {
    uiManager = manager;
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> SpecialQueryView::GetDataAsStrings() {
    return {queryResult.columns, queryResult.rows};
}

void SpecialQueryView::ExecuteQuery() {
    if (dbManager && dbManager->is_open()) {
        dbManager->executeSelect(query.c_str(), queryResult.columns,
                                 queryResult.rows, queryResult.column_types);
        selected_cells.assign(queryResult.rows.size(), std::vector<bool>(queryResult.columns.size(), false));
        last_clicked_cell = ImVec2(-1, -1);
        CalculateTotals();
    } else {
        queryResult.columns.clear();
        queryResult.column_types.clear();
        queryResult.rows.clear();
        selected_cells.clear();
        std::cerr << "No database open to execute SQL query." << std::endl;
    }
}

void SpecialQueryView::Render() {
    if (!IsVisible) {
        return;
    }

    if (ImGui::Begin(GetTitle(), &IsVisible)) {
        if (ImGui::Button(ICON_FA_ROTATE " Обновить")) {
            ExecuteQuery();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_COPY " Копировать")) {
            std::stringstream ss;

            // Check for any selection
            bool any_selection = false;
            if (!selected_cells.empty()) {
                for (const auto& row : selected_cells) {
                    for (bool cell : row) {
                        if (cell) {
                            any_selection = true;
                            break;
                        }
                    }
                    if (any_selection) {
                        break;
                    }
                }
            }

            bool first_row = true;
            for (size_t i = 0; i < queryResult.rows.size(); ++i) {
                bool should_copy = !any_selection;
                if (any_selection) {
                    for (size_t j = 0; j < queryResult.columns.size(); ++j) {
                        if (selected_cells[i][j]) {
                            should_copy = true;
                            break;
                        }
                    }
                }

                if (should_copy) {
                    if (!first_row) {
                        ss << "\n";
                    }
                    for (size_t j = 0; j < queryResult.columns.size(); ++j) {
                        if (j > 0) {
                            ss << "\t";
                        }
                        const int sqlite_type =
                            j < queryResult.column_types.size()
                                ? queryResult.column_types[j]
                                : SQLITE_NULL;
                        ss << FormatCellForSpreadsheetCopy(
                            queryResult.rows[i][j], queryResult.columns[j],
                            sqlite_type);
                    }
                    first_row = false;
                }
            }
            ImGui::SetClipboardText(ss.str().c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PRINT " Печать")) {
            PrintDataAsHtml();
        }

        ImGui::Separator();
        ImGui::Text("Результат:");
        ImGui::Text("Количество записей: %zu", queryResult.rows.size());
        if (!queryResult.columns.empty()) {
            if (ImGui::BeginChild("##TableScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
                if (ImGui::BeginTable("special_query_result", queryResult.columns.size(),
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Sortable)) {
                for (size_t i = 0; i < queryResult.columns.size(); ++i) {
                    const bool long_text_column =
                        IsLongTextColumnName(queryResult.columns[i]);
                    ImGui::TableSetupColumn(
                        queryResult.columns[i].c_str(),
                        ImGuiTableColumnFlags_DefaultSort |
                            (long_text_column
                                 ? ImGuiTableColumnFlags_WidthFixed
                                 : ImGuiTableColumnFlags_None),
                        long_text_column ? 1200.0f : 0.0f, i);
                }
                ImGui::TableHeadersRow();

                if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs()) {
                    if (sorts_specs->SpecsDirty) {
                        this->sort_specs = *sorts_specs; // Copy the content
                        SortRows();
                        CalculateTotals(); // Recalculate totals after sorting
                        sorts_specs->SpecsDirty = false;
                    }
                }

                ImGuiListClipper clipper;
                clipper.Begin(queryResult.rows.size());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                        ImGui::TableNextRow();
                        for (size_t j = 0; j < queryResult.columns.size(); ++j) {
                            ImGui::TableNextColumn();
                            char label[256];
                            snprintf(label, sizeof(label), "%s##%d_%zu", queryResult.rows[i][j].c_str(), i, j);
                            bool is_selected = selected_cells[i][j];

                            if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                                if (ImGui::GetIO().KeyCtrl) {
                                    selected_cells[i][j] = !selected_cells[i][j];
                                } else if (ImGui::GetIO().KeyShift && last_clicked_cell.x != -1) {
                                    for(auto& row : selected_cells) std::fill(row.begin(), row.end(), false);
                                    float r_min = std::min(last_clicked_cell.y, (float)i);
                                    float r_max = std::max(last_clicked_cell.y, (float)i);
                                    float c_min = std::min(last_clicked_cell.x, (float)j);
                                    float c_max = std::max(last_clicked_cell.x, (float)j);
                                    for(int r = r_min; r <= r_max; ++r) {
                                        for (int c = c_min; c <= c_max; ++c) {
                                            selected_cells[r][c] = true;
                                        }
                                    }
                                } else {
                                    for(auto& row : selected_cells) std::fill(row.begin(), row.end(), false);
                                    selected_cells[i][j] = true;
                                }
                                last_clicked_cell = ImVec2(j, i);
                            }
                        }
                    }
                }

                // Draw totals row
                if (!queryResult.rows.empty()) {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetTextLineHeightWithSpacing() * 1.2f);
                    for (size_t j = 0; j < queryResult.columns.size(); ++j) {
                        ImGui::TableNextColumn();
                        if (is_numeric_column[j] && column_totals[j] != 0.0) {
                            char total_text[64];
                            snprintf(total_text, sizeof(total_text), "Итого: %.2f", column_totals[j]);
                            ImGui::TextDisabled("%s", total_text);
                        }
                    }
                }

                ImGui::EndTable();
                }
                ImGui::EndChild();
            }
        } else {
            ImGui::Text("Нет результатов или запрос не был выполнен.");
        }
    }
    ImGui::End();
}

void SpecialQueryView::PrintDataAsHtml() {
    if (queryResult.columns.empty()) {
        return;
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    const std::filesystem::path output_path =
        std::filesystem::temp_directory_path() /
        ("fnaudit_print_" + std::to_string(now) + ".html");

    std::ofstream file;
    platformOpenOutputFile(file, output_path.string(), std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to write print HTML: " << output_path << std::endl;
        return;
    }

    file << MakePrintableHtml(Title, queryResult.columns, queryResult.rows,
                              column_totals, is_numeric_column);
    file.close();

    platformOpenOrLog(output_path.string(), "print HTML");
}

void SpecialQueryView::SortRows() {
    if (this->sort_specs.SpecsCount == 0 || queryResult.rows.empty()) {
        return;
    }

    std::sort(queryResult.rows.begin(), queryResult.rows.end(),
              [&](const std::vector<std::string>& a, const std::vector<std::string>& b) {
                  for (int i = 0; i < this->sort_specs.SpecsCount; i++) {
                      const ImGuiTableColumnSortSpecs* sort_spec = &this->sort_specs.Specs[i];
                      int column_idx = sort_spec->ColumnUserID;
                      if (column_idx >= a.size() || column_idx >= b.size()) {
                          continue; // Should not happen if columnUserID is set correctly
                      }

                      const bool numeric_column =
                          column_idx >= 0 &&
                          static_cast<size_t>(column_idx) <
                              queryResult.column_types.size() &&
                          IsSQLiteNumericType(
                              queryResult.column_types[column_idx]);

                      int delta = 0;
                      if (numeric_column) {
                          double val_a = 0.0;
                          double val_b = 0.0;
                          std::string str_a = a[column_idx];
                          std::string str_b = b[column_idx];
                          std::replace(str_a.begin(), str_a.end(), ',', '.');
                          std::replace(str_b.begin(), str_b.end(), ',', '.');
                          const bool is_numeric_a =
                              sscanf(str_a.c_str(), "%lf", &val_a) == 1;
                          const bool is_numeric_b =
                              sscanf(str_b.c_str(), "%lf", &val_b) == 1;
                          delta = (val_a < val_b) ? -1 : (val_a > val_b) ? 1 : 0;
                          if (!is_numeric_a || !is_numeric_b) {
                              delta = a[column_idx].compare(b[column_idx]);
                          }
                      } else {
                          delta = a[column_idx].compare(b[column_idx]);
                      }

                      if (delta != 0) {
                          return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? (delta < 0) : (delta > 0);
                      }
                  }
                  return false;
              });
}

void SpecialQueryView::CalculateTotals() {
    column_totals.assign(queryResult.columns.size(), 0.0);
    is_numeric_column.assign(queryResult.columns.size(), true);

    for (size_t col = 0; col < queryResult.columns.size(); ++col) {
        if (col < queryResult.column_types.size() &&
            !IsSQLiteNumericType(queryResult.column_types[col])) {
            is_numeric_column[col] = false;
            continue;
        }

        for (const auto& row : queryResult.rows) {
            if (col >= row.size()) continue;

            std::string cell_value = row[col];
            // Replace comma with dot for numeric parsing
            std::replace(cell_value.begin(), cell_value.end(), ',', '.');

            // Try to parse as number (handle spaces as thousand separators)
            std::string cleaned_value;
            for (char c : cell_value) {
                if (c != ' ') cleaned_value += c;
            }

            double value;
            if (sscanf(cleaned_value.c_str(), "%lf", &value) == 1) {
                column_totals[col] += value;
            } else {
                is_numeric_column[col] = false;
                break;
            }
        }
    }
}
