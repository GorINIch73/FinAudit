#pragma once

#include "BaseView.h"
#include <vector>
#include <string>
#include <utility>

class SqlQueryView : public BaseView {
public:
    SqlQueryView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;

private:
    char queryInputBuffer[4096];
    struct QueryResult {
        std::vector<std::string> columns;
        std::vector<std::vector<std::string>> rows;
    } queryResult;
};
