#pragma once

#include "BaseView.h"
#include <vector>
#include <string>
#include <utility>
#include "imgui.h"

class UIManager; // Forward declaration

class SpecialQueryView : public BaseView {
public:
    SpecialQueryView(const std::string& title, const std::string& query);
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;

private:
    void ExecuteQuery();

    std::string query;
    struct QueryResult {
        std::vector<std::string> columns;
        std::vector<std::vector<std::string>> rows;
    } queryResult;

    ImGuiTableSortSpecs sort_specs{}; // To store sort specifications
    void SortRows();

    std::vector<std::vector<bool>> selected_cells;
    ImVec2 last_clicked_cell = ImVec2(-1, -1);
    UIManager* uiManager = nullptr; // Added UIManager pointer

    // Totals calculation
    std::vector<double> column_totals;
    std::vector<bool> is_numeric_column;
    void CalculateTotals();
};
