#include "ExpenseStructureView.h"
#include "../IconsFontAwesome6.h"
#include "../PlatformUtils.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

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
    const std::string counterpartyExpr =
        "IFNULL(contract_cp.name, IFNULL(payment_cp.name, IFNULL(p.recipient, "
        "'Без контрагента')))";
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
        detailsExpr = counterpartyExpr;
        break;
    case 2:
        groupExpr = counterpartyExpr;
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
        << "SUM(COALESCE(pd.amount, 0.0)) AS total_amount "
        << "FROM PaymentDetails pd "
        << "JOIN Payments p ON p.id = pd.payment_id "
        << "LEFT JOIN KOSGU k ON k.id = pd.kosgu_id "
        << "LEFT JOIN Contracts ct ON ct.id = pd.contract_id "
        << "LEFT JOIN Counterparties contract_cp ON contract_cp.id = ct.counterparty_id "
        << "LEFT JOIN Counterparties payment_cp ON payment_cp.id = p.counterparty_id "
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

const char* ExpenseStructureView::GetGroupingModeName() const {
    switch (groupingMode) {
    case 1:
        return "КОСГУ -> контрагент";
    case 2:
        return "По контрагентам";
    case 3:
        return "По месяцам";
    default:
        return "По КОСГУ";
    }
}

std::string ExpenseStructureView::BuildPdfReportTitle() const {
    std::ostringstream title;
    title << "Анализ структуры расходов";
    title << "\nГруппировка: " << GetGroupingModeName();
    if (startDate[0] != '\0' || endDate[0] != '\0') {
        title << "\nПериод: "
              << (startDate[0] != '\0' ? startDate : "с начала")
              << " - "
              << (endDate[0] != '\0' ? endDate : "по конец");
    }
    title << "\nИсточник сумм: расшифровки платежей";
    title << "\nИтого: " << FormatMoney(totalAmount);
    return title.str();
}

std::string ExpenseStructureView::BuildReportBaseName() const {
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    char stamp[32] = {0};
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localTime);
    return std::string("expense_structure_") + stamp;
}

void ExpenseStructureView::StoreSortSpecs(const ImGuiTableSortSpecs* specs) {
    if (!specs || specs->SpecsCount <= 0) {
        return;
    }
    sortColumn = specs->Specs[0].ColumnIndex;
    sortDirection = specs->Specs[0].SortDirection;
    hasStoredSort = true;
}

void ExpenseStructureView::ApplyCurrentSort() {
    if (!hasStoredSort) {
        return;
    }

    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        int delta = 0;
        if (sortColumn == 2) {
            delta = (a.amount < b.amount) ? -1 : (a.amount > b.amount ? 1 : 0);
        } else if (sortColumn == 3) {
            delta = (a.share < b.share) ? -1 : (a.share > b.share ? 1 : 0);
        } else if (sortColumn == 1) {
            delta = a.details.compare(b.details);
        } else {
            delta = a.group.compare(b.group);
        }

        if (delta == 0) {
            delta = a.group.compare(b.group);
        }
        if (delta == 0) {
            delta = a.details.compare(b.details);
        }

        return sortDirection == ImGuiSortDirection_Ascending ? delta < 0
                                                             : delta > 0;
    });
}

std::string ExpenseStructureView::BuildHtmlReport() const {
    std::ostringstream html;
    html << "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
         << "<title>Структура расходов</title>"
         << "<style>"
         << "@page{size:A4;margin:14mm 12mm;}"
         << "body{font-family:'Segoe UI',Arial,sans-serif;color:#1f2933;margin:0;}"
         << ".title{font-size:22px;font-weight:700;margin:0 0 6px;}"
         << ".meta{display:grid;grid-template-columns:1fr 1fr;gap:6px 18px;"
         << "font-size:11px;color:#4b5563;margin:0 0 14px;}"
         << ".summary{display:flex;gap:20px;border-top:2px solid #2f5d62;"
         << "border-bottom:1px solid #cad2d3;padding:8px 0;margin-bottom:12px;"
         << "font-size:12px;}"
         << ".summary b{font-size:14px;color:#163235;}"
         << "table{width:100%;border-collapse:collapse;font-size:10px;}"
         << "thead{display:table-header-group;}"
         << "th{background:#2f5d62;color:white;text-align:left;padding:7px 8px;"
         << "font-weight:600;border:1px solid #2f5d62;}"
         << "td{padding:6px 8px;border:1px solid #d7dee0;vertical-align:top;}"
         << "tbody tr:nth-child(even){background:#f6f9f9;}"
         << ".amount,.share{text-align:right;white-space:nowrap;}"
         << ".total td{font-weight:700;background:#edf4f4;border-top:2px solid #2f5d62;}"
         << ".muted{color:#607079;}"
         << "</style></head><body>";

    html << "<h1 class=\"title\">Анализ структуры расходов</h1>";
    html << "<div class=\"meta\">";
    html << "<div><b>Группировка:</b> " << EscapeHtml(GetGroupingModeName())
         << "</div>";
    html << "<div><b>Источник сумм:</b> расшифровки платежей</div>";
    html << "<div><b>С даты:</b> "
         << EscapeHtml(startDate[0] != '\0' ? startDate : "не задано")
         << "</div>";
    html << "<div><b>По дату:</b> "
         << EscapeHtml(endDate[0] != '\0' ? endDate : "не задано")
         << "</div>";
    html << "<div><b>Поступления:</b> "
         << (includeIncoming ? "включены" : "исключены") << "</div>";
    html << "</div>";

    html << "<div class=\"summary\"><div>Итого<br><b>"
         << EscapeHtml(FormatMoney(totalAmount)) << "</b></div><div>Строк<br><b>"
         << rows.size() << "</b></div></div>";

    html << "<table><thead><tr><th>Группа</th><th>Детализация</th>"
         << "<th class=\"amount\">Сумма</th><th class=\"share\">Доля</th>"
         << "</tr></thead><tbody>";
    for (const auto& row : rows) {
        html << "<tr><td>" << EscapeHtml(row.group) << "</td><td>"
             << EscapeHtml(row.details.empty() ? "-" : row.details)
             << "</td><td class=\"amount\">" << EscapeHtml(FormatMoney(row.amount))
             << "</td><td class=\"share\">" << EscapeHtml(FormatPercent(row.share))
             << "</td></tr>";
    }
    html << "<tr class=\"total\"><td>Итого</td><td></td><td class=\"amount\">"
         << EscapeHtml(FormatMoney(totalAmount))
         << "</td><td class=\"share\">100.00%</td></tr>";
    html << "</tbody></table></body></html>";
    return html.str();
}

bool ExpenseStructureView::WriteHtmlReport(std::filesystem::path& htmlPath) {
    exportStatus.clear();

    if (!dbManager || !dbManager->is_open()) {
        exportStatus = "Откройте базу данных перед формированием отчета.";
        return false;
    }

    RefreshData();
    if (rows.empty()) {
        exportStatus = "Нет данных для формирования отчета.";
        return false;
    }

    std::error_code ec;
    std::filesystem::path reportsDir = std::filesystem::current_path() / "reports";
    std::filesystem::create_directories(reportsDir, ec);
    if (ec) {
        exportStatus = "Не удалось создать папку reports.";
        return false;
    }

    std::string baseName = BuildReportBaseName();
    htmlPath = reportsDir / (baseName + ".html");

    std::ofstream htmlFile;
    platformOpenOutputFile(htmlFile, htmlPath.string(),
                           std::ios::out | std::ios::binary);
    if (!htmlFile) {
        exportStatus = "Не удалось записать HTML отчета.";
        return false;
    }
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    htmlFile.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    htmlFile << BuildHtmlReport();
    htmlFile.close();
    return true;
}

void ExpenseStructureView::PrintReport() {
    std::filesystem::path htmlPath;
    if (!WriteHtmlReport(htmlPath)) {
        return;
    }

    platformOpenOrLog(htmlPath.string(), "expense structure print report");
    exportStatus = "Печатная версия открыта: " + htmlPath.string();
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
    ApplyCurrentSort();

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

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PRINT " Печать")) {
            PrintReport();
        }

        ImGui::Separator();
        if (!exportStatus.empty()) {
            ImGui::TextWrapped("%s", exportStatus.c_str());
        }
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
                    StoreSortSpecs(specs);
                    ApplyCurrentSort();
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
    std::string formatted = buffer;
    size_t dotPos = formatted.find('.');
    size_t integerEnd =
        dotPos == std::string::npos ? formatted.size() : dotPos;
    size_t firstDigit = formatted[0] == '-' ? 1 : 0;

    for (size_t pos = integerEnd; pos > firstDigit + 3; pos -= 3) {
        formatted.insert(pos - 3, " ");
    }
    return formatted;
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

std::string ExpenseStructureView::EscapeHtml(const std::string& value) {
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
