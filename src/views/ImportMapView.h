#pragma once

#include "BaseView.h"
#include <string>
#include <vector>
#include <map>
#include "../Regex.h"
#include <regex>
#include <atomic>

// Forward declaration
class UIManager;

class ImportMapView : public BaseView {
public:
    ImportMapView();
    void Render() override;

    // Open the mapping window for a specific file
    void Open(const std::string& filePath);

    // Implementing pure virtual functions from BaseView
    void SetDatabaseManager(DatabaseManager* manager) override { dbManager = manager; }
    void SetPdfReporter(PdfReporter* reporter) override { /* Not used */ }
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;

private:
    void Reset();
    void ReadPreviewData();
    void RefreshRegexes();

    UIManager* uiManager = nullptr;
    std::string importFilePath;
    std::vector<std::string> fileHeaders;
    std::vector<std::vector<std::string>> sampleData; // To store first few rows for preview
    std::vector<std::string> targetFields;
    std::map<std::string, int> currentMapping; // Maps target field to file header index

    std::vector<Regex> regexes;
    int contract_regex_index = -1;
    int kosgu_regex_index = -1;
    std::string sample_description;
    std::string contract_match;
    std::string kosgu_match;
    std::string contract_pattern_buffer;
    std::string kosgu_pattern_buffer;
    bool import_started = false;
    bool force_income_type = false;
    bool is_return_import = false;
    std::string custom_note_buffer;
    std::atomic<bool>* cancel_flag = nullptr;
};
