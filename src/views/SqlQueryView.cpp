#include "SqlQueryView.h"
#include "../IconsFontAwesome6.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

namespace {
std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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

}

SqlQueryView::SqlQueryView() {
    Title = "SQL Запрос";
    memset(queryInputBuffer, 0, sizeof(queryInputBuffer));
}

void SqlQueryView::SetUIManager(UIManager *manager) {
    uiManager = manager;
}

void SqlQueryView::SetDatabaseManager(DatabaseManager *manager) {
    dbManager = manager;
}

void SqlQueryView::SetPdfReporter(PdfReporter *reporter) {
    pdfReporter = reporter;
}


std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
SqlQueryView::GetDataAsStrings() {
    return {queryResult.columns, queryResult.rows};
}

void SqlQueryView::Render() {
    if (!IsVisible) {
        return;
    }

    if (ImGui::Begin(GetTitle(), &IsVisible)) {

        ImGui::Text("Введите SQL запрос:");
        ImGui::InputTextMultiline(
            "##SQLQueryInput", queryInputBuffer, sizeof(queryInputBuffer),
            ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8));

        if (ImGui::Button(ICON_FA_PLAY " Выполнить")) {
            if (dbManager && dbManager->is_open()) {
                dbManager->executeSelect(queryInputBuffer, queryResult.columns,
                                         queryResult.rows);
            } else {
                queryResult.columns.clear();
                queryResult.rows.clear();
                std::cerr << "No database open to execute SQL query."
                          << std::endl;
            }
        }

        ImGui::Separator();
        ImGui::Text("Результат:");

        if (!queryResult.columns.empty()) {
            if (ImGui::BeginTable(
                    "sql_query_result", queryResult.columns.size(),
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Resizable)) {
                for (const auto &col : queryResult.columns) {
                    const bool long_text_column = IsLongTextColumnName(col);
                    ImGui::TableSetupColumn(
                        col.c_str(),
                        long_text_column
                            ? ImGuiTableColumnFlags_WidthFixed
                            : ImGuiTableColumnFlags_None,
                        long_text_column ? 1200.0f : 0.0f);
                }
                ImGui::TableHeadersRow();

                for (const auto &row : queryResult.rows) {
                    ImGui::TableNextRow();
                    for (const auto &cell : row) {
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", cell.c_str());
                    }
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::Text("Нет результатов или запрос не был выполнен.");
        }
    }
    ImGui::End();
}
