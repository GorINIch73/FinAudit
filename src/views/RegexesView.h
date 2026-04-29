#pragma once

#include "BaseView.h"
#include <vector>
#include "../Regex.h"

class RegexesView : public BaseView {
public:
    RegexesView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;
    void OnDeactivate() override;

private:
    void RefreshData();
    void SaveChanges();

    std::vector<Regex> regexes;
    Regex selectedRegex;
    Regex originalRegex;
    int selectedRegexIndex;
    bool isAdding;
    bool isDirty = false;
    bool show_delete_popup = false;
    int regex_id_to_delete = -1;
    char filterText[256];
};
