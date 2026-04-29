#pragma once

#include "BaseView.h"
#include "../ImportManager.h"
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

class UIManager;

class JO4ImportMapView : public BaseView {
public:
    JO4ImportMapView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* reporter) override {}
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;

    void Open(const std::string& filePath);
    void Reset();
    void ReadPreviewData();
    void RefreshDropdownData();

    // Доступ для UIManager
    bool import_started = false;

private:
    DatabaseManager* dbManager = nullptr;
    UIManager* uiManager = nullptr;

    std::string importFilePath;
    std::vector<std::string> fileHeaders;
    std::vector<std::vector<std::string>> sampleData;

    // Доступные целевые поля для маппинга
    std::vector<std::string> targetFields = {
        "Дата документа",
        "Номер документа",
        "Наименование документа",
        "Наименование показателя",
        "Содержание операции",
        "Счет дебет",
        "Счет кредит",
        "Сумма"
    };

    // Текущий маппинг: поле -> индекс колонки (-1 = не выбрано)
    std::map<std::string, int> currentMapping;

    std::atomic<float>* progressPtr = nullptr;
    std::string* messagePtr = nullptr;
    std::mutex* messageMutexPtr = nullptr;

    std::atomic<bool>* cancel_flag_ptr = nullptr;

    void StartImport();
};
