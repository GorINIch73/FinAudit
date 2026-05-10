#pragma once

#include "BaseView.h"
#include <filesystem>
#include <string>
#include <vector>

class ExpenseStructureView : public BaseView {
public:
    ExpenseStructureView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* manager) override;
    void SetPdfReporter(PdfReporter* reporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
    GetDataAsStrings() override;

private:
    struct Row {
        std::string group;
        std::string details;
        double amount = 0.0;
        double share = 0.0;
    };

    void RefreshData();
    bool WriteHtmlReport(std::filesystem::path& htmlPath);
    void PrintReport();
    void ApplyCurrentSort();
    void StoreSortSpecs(const struct ImGuiTableSortSpecs* specs);
    std::string BuildPdfReportTitle() const;
    std::string BuildHtmlReport() const;
    std::string BuildReportBaseName() const;
    std::string BuildQuery() const;
    const char* GetGroupingModeName() const;
    static std::string FormatMoney(double value);
    static std::string FormatPercent(double value);
    static double ParseDouble(const std::string& value);
    static std::string EscapeSqlLiteral(const char* value);
    static std::string EscapeHtml(const std::string& value);
    static std::string NormalizeDateFilter(const char* value);

    std::vector<Row> rows;
    double totalAmount = 0.0;
    int groupingMode = 0;
    char startDate[16] = {0};
    char endDate[16] = {0};
    bool includeIncoming = false;
    bool needsRefresh = true;
    std::string exportStatus;
    int sortColumn = 2;
    int sortDirection = 1;
    bool hasStoredSort = true;
};
