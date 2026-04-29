#pragma once

#include "views/BaseView.h"
#include "DatabaseManager.h"
#include <vector>
#include "SuspiciousWord.h"

class SuspiciousWordsView : public BaseView {
public:
    SuspiciousWordsView();
    void Render() override;
    const char* GetTitle() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;
private:
    std::vector<SuspiciousWord> suspiciousWords;
    SuspiciousWord editedWord;
    char editedWordBuffer[256];
    bool showEditPopup = false;
    void loadSuspiciousWords();
};
