#include "ExpenseStructureView.h"
#include "../IconsFontAwesome6.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <sstream>

ExpenseStructureView::ExpenseStructureView() {
    Title = "Анализ структуры расходов";
}

void ExpenseStructureView::SetDatabaseManager(DatabaseManager* manager) {
    dbManager = manager;
    needsRefresh = true;
}

void ExpenseStructureView::SetPdfReporter(PdfReporter* reporter) {
    pdfReporter = reporter;
}

void ExpenseStructureView::SetUIManager(UIManager* manager) {
    uiManager = manager;
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
ExpenseStructureView::GetDataAsStrings() {
    std::vector<std::string> headers = {"Группа", "Детализация", "Сумма", "Доля"};
    std::vector<std::vector<std::string>> data;
    data.reserve(rows.size() + 1);

    for (const auto& row : rows) {
        data.push_back({row.group, row.details, FormatMoney(row.amount),
                        FormatPercent(row.share)});
    }
    data.push_back({"Итого", "", FormatMoney(totalAmount), "100.00%"});

    return {headers, data};
}

std::string ExpenseStructureView::BuildQuery() const {
    const std::string dateExpr =
        "CASE "
        "WHEN p.date GLOB '[0-9][0-9].[0-9][0-9].[0-9][0-9][0-9][0-9]' "
        "THEN SUBSTR(p.date, 7, 4) || '-' || SUBSTR(p.date, 4, 2) || '-' || "
        "SUBSTR(p.date, 1, 2) "
        "ELSE p.date END";
    std::string groupExpr =
        "IFNULL(k.code, 'Без КОСГУ') || CASE WHEN IFNULL(k.kps, '') <> '' "
        "THEN ' / КПС ' || k.kps ELSE '' END";
    std::string detailsExpr = "IFNULL(k.name, '')";
    std::string orderExpr = "total_amount DESC";

    switch (groupingMode) {
    case 1:
        groupExpr =
            "IFNULL(k.code, 'Без КОСГУ') || CASE WHEN IFNULL(k.kps, '') <> '' "
            "THEN ' / КПС ' || k.kps ELSE '' END || ' ' || IFNULL(k.name, '')";
        detailsExpr = "IFNULL(c.name, IFNULL(p.recipient, ''))";
        break;
    case 2:
        groupExpr = "IFNULL(c.name, IFNULL(p.recipient, 'Без контрагента'))";
        detailsExpr =
            "IFNULL(k.code, '') || CASE WHEN IFNULL(k.kps, '') <> '' "
            "THEN ' / КПС ' || k.kps ELSE '' END || ' ' || IFNULL(k.name, '')";
        break;
    case 3:
        groupExpr = "STRFTIME('%Y-%m', " + dateExpr + ")";
        detailsExpr =
            "IFNULL(k.code, '') || CASE WHEN IFNULL(k.kps, '') <> '' "
            "THEN ' / КПС ' || k.kps ELSE '' END || ' ' || IFNULL(k.name, '')";
        orderExpr = "grp ASC";
        break;
    default:
        break;
    }

    std::ostringstream sql;
    sql << "SELECT " << groupExpr << " AS grp, "
        << detailsExpr << " AS details, "
        << "SUM(pd.amount) AS total_amount "
        << "FROM PaymentDetails pd "
        << "JOIN Payments p ON p.id = pd.payment_id "
        << "LEFT JOIN KOSGU k ON k.id = pd.kosgu_id "
        << "LEFT JOIN Counterparties c ON c.id = p.counterparty_id "
        << "WHERE 1=1 ";

    if (!includeIncoming) {
        sql << "AND p.type = 0 ";
    }
    const std::string normalizedStartDate = NormalizeDateFilter(startDate);
    const std::string normalizedEndDate = NormalizeDateFilter(endDate);
    if (!normalizedStartDate.empty()) {
        sql << "AND " << dateExpr << " >= '"
            << EscapeSqlLiteral(normalizedStartDate.c_str()) << "' ";
    }
    if (!normalizedEndDate.empty()) {
        sql << "AND " << dateExpr << " <= '"
            << EscapeSqlLiteral(normalizedEndDate.c_str()) << "' ";
    }

    sql << "GROUP BY grp, details "
        << "HAVING total_amount <> 0 "
        << "ORDER BY " << orderExpr << ";";

    return sql.str();
}

void ExpenseStructureView::RefreshData() {
    rows.clear();
    totalAmount = 0.0;

    if (!dbManager || !dbManager->is_open()) {
        needsRefresh = false;
        return;
    }

    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> resultRows;
    if (!dbManager->executeSelect(BuildQuery(), columns, resultRows)) {
        needsRefresh = false;
        return;
    }

    rows.reserve(resultRows.size());
    for (const auto& resultRow : resultRows) {
        if (resultRow.size() < 3) {
            continue;
        }

        Row row;
        row.group = resultRow[0];
        row.details = resultRow[1];
        row.amount = ParseDouble(resultRow[2]);
        totalAmount += row.amount;
        rows.push_back(row);
    }

    if (totalAmount != 0.0) {
        for (auto& row : rows) {
            row.share = row.amount * 100.0 / totalAmount;
        }
    }

    needsRefresh = false;
}

void ExpenseStructureView::Render() {
    if (!IsVisible) {
        return;
    }

    if (needsRefresh) {
        RefreshData();
    }

    if (ImGui::Begin(Title.c_str(), &IsVisible)) {
        const char* modes[] = {
            "По КОСГУ",
            "КОСГУ -> контрагент",
            "По контрагентам",
            "По месяцам"};

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("Группировка", &groupingMode, modes, IM_ARRAYSIZE(modes))) {
            needsRefresh = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText("С даты", startDate, sizeof(startDate))) {
            needsRefresh = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText("По дату", endDate, sizeof(endDate))) {
            needsRefresh = true;
        }

        ImGui::SameLine();
        if (ImGui::Checkbox("Включать поступления", &includeIncoming)) {
            needsRefresh = true;
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE " Обновить")) {
            RefreshData();
        }

        ImGui::Separator();
        ImGui::Text("Итого: %s | Строк: %zu", FormatMoney(totalAmount).c_str(),
                    rows.size());

        if (!dbManager || !dbManager->is_open()) {
            ImGui::TextDisabled("Откройте базу данных для построения сводной формы.");
        } else if (rows.empty()) {
            ImGui::TextDisabled("Нет данных для выбранных условий.");
        } else if (ImGui::BeginTable(
                       "expense_structure_table", 4,
                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_Sortable,
                       ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Группа");
            ImGui::TableSetupColumn("Детализация");
            ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableSetupColumn("Доля", ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
                if (specs->SpecsDirty && specs->SpecsCount > 0) {
                    const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
                    std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                        int delta = 0;
                        if (spec.ColumnIndex == 2) {
                            delta = (a.amount < b.amount) ? -1 : (a.amount > b.amount ? 1 : 0);
                        } else if (spec.ColumnIndex == 3) {
                            delta = (a.share < b.share) ? -1 : (a.share > b.share ? 1 : 0);
                        } else if (spec.ColumnIndex == 1) {
                            delta = a.details.compare(b.details);
                        } else {
                            delta = a.group.compare(b.group);
                        }
                        return spec.SortDirection == ImGuiSortDirection_Ascending
                                   ? delta < 0
                                   : delta > 0;
                    });
                    specs->SpecsDirty = false;
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const Row& row = rows[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.group.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.details.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(FormatMoney(row.amount).c_str());
                    ImGui::TableNextColumn();
                    ImGui::ProgressBar(static_cast<float>(row.share / 100.0),
                                       ImVec2(-FLT_MIN, 0.0f),
                                       FormatPercent(row.share).c_str());
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

std::string ExpenseStructureView::FormatMoney(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

std::string ExpenseStructureView::FormatPercent(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f%%", value);
    return buffer;
}

double ExpenseStructureView::ParseDouble(const std::string& value) {
    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', '.');
    return std::strtod(normalized.c_str(), nullptr);
}

std::string ExpenseStructureView::EscapeSqlLiteral(const char* value) {
    std::string escaped;
    for (const char* p = value; p && *p; ++p) {
        if (*p == '\'') {
            escaped += "''";
        } else {
            escaped += *p;
        }
    }
    return escaped;
}

std::string ExpenseStructureView::NormalizeDateFilter(const char* value) {
    if (!value || value[0] == '\0') {
        return "";
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(value, "%d-%d-%d", &year, &month, &day) == 3) {
        if (year >= 1900 && year <= 2100 && month >= 1 && month <= 12 &&
            day >= 1 && day <= 31) {
            char buffer[11];
            std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year,
                          month, day);
            return buffer;
        }
    }

    if (std::sscanf(value, "%d.%d.%d", &day, &month, &year) == 3 ||
        std::sscanf(value, "%d/%d/%d", &day, &month, &year) == 3) {
        if (year < 100) {
            year += 2000;
        }
        if (year >= 1900 && year <= 2100 && month >= 1 && month <= 12 &&
            day >= 1 && day <= 31) {
            char buffer[11];
            std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year,
                          month, day);
            return buffer;
        }
    }

    return "";
}
