#include "SpecialQueryView.h"
#include "../IconsFontAwesome6.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>

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
        dbManager->executeSelect(query.c_str(), queryResult.columns, queryResult.rows);
        selected_cells.assign(queryResult.rows.size(), std::vector<bool>(queryResult.columns.size(), false));
        last_clicked_cell = ImVec2(-1, -1);
        CalculateTotals();
    } else {
        queryResult.columns.clear();
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
                        ss << queryResult.rows[i][j];
                    }
                    first_row = false;
                }
            }
            ImGui::SetClipboardText(ss.str().c_str());
        }

        ImGui::Separator();
        ImGui::Text("Результат:");
        if (!queryResult.columns.empty()) {
            if (ImGui::BeginChild("##TableScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
                if (ImGui::BeginTable("special_query_result", queryResult.columns.size(),
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Sortable)) {
                for (size_t i = 0; i < queryResult.columns.size(); ++i) {
                    ImGui::TableSetupColumn(queryResult.columns[i].c_str(), ImGuiTableColumnFlags_DefaultSort, 0.0f, i);
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

                      double val_a, val_b;
                      std::string str_a = a[column_idx];
                      std::string str_b = b[column_idx];
                      std::replace(str_a.begin(), str_a.end(), ',', '.');
                      std::replace(str_b.begin(), str_b.end(), ',', '.');

                      bool is_numeric_a = (sscanf(str_a.c_str(), "%lf", &val_a) == 1);
                      bool is_numeric_b = (sscanf(str_b.c_str(), "%lf", &val_b) == 1);

                      int delta = 0;
                      if (is_numeric_a && is_numeric_b) {
                          delta = (val_a < val_b) ? -1 : (val_a > val_b) ? 1 : 0;
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
