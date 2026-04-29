#pragma once

#include "BaseView.h"
#include <vector>
#include <string>

class UIManager;

class ReconciliationView : public BaseView {
public:
    ReconciliationView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;
    void OnDeactivate() override;

private:
    void RefreshData();

    std::vector<DatabaseManager::ReconciliationRecord> records;
    std::vector<DatabaseManager::ReconciliationRecord> filtered_records;
    char filter_buffer[256] = {0};

    int selected_index = -1;
    int selected_payment_id = -1;

    // Группировка по платежам
    struct PaymentGroup {
        int payment_id;
        std::string payment_date;
        std::string payment_doc_number;
        double payment_amount;
        std::string counterparty_name;
        std::vector<int> record_indices;
    };
    std::vector<PaymentGroup> payment_groups;

    UIManager* uiManager = nullptr;
};
