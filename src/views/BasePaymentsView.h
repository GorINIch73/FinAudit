#pragma once

#include "BaseView.h"
#include <functional>
#include <atomic>
#include <vector>
#include "../BasePaymentDocument.h"
#include "../Contract.h"
#include "../Counterparty.h"
#include "../Kosgu.h"
#include "../Payment.h"

class UIManager;

class BasePaymentsView : public BaseView {
public:
    BasePaymentsView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;
    void OnDeactivate() override;
    void ForceSave() override;

private:
    void RefreshData();
    void RefreshDropdownData();
    void SaveChanges();

    std::vector<BasePaymentDocument> documents;
    BasePaymentDocument selectedDoc;
    BasePaymentDocument originalDoc;
    int selectedDocIndex;
    bool showEditModal;
    bool isAdding;
    bool isDirty = false;
    char doc_search_buffer[256] = {0};

    std::vector<Counterparty> counterpartiesForDropdown;
    std::vector<Contract> contractsForDropdown;
    std::vector<Kosgu> kosguForDropdown;
    std::vector<Payment> paymentsForDropdown;
    char filterText[256];
    char counterpartyFilter[256];
    float list_view_height = 200.0f;
    float editor_width = 400.0f;
    float split_pos = 250.0f;
    bool m_dataDirty = true;

    std::vector<BasePaymentDocumentDetail> docDetails;
    BasePaymentDocumentDetail selectedDetail;
    bool isDetailDirty = false;
    bool showDetailAddPopup = false;

    UIManager* uiManager = nullptr;

    // Групповые операции
    enum GroupOperationType {
        NONE,
        ADD_KOSGU,
        REPLACE,
        DELETE_DETAILS,
        APPLY_REGEX,
        SET_FOR_CHECKING,
        UNSET_FOR_CHECKING,
        SET_CHECKED,
        UNSET_CHECKED,
        CLEAR_PAYMENT_LINK,
        AUTO_MATCH_PAYMENTS
    };
    GroupOperationType current_operation = NONE;
    int processed_items = 0;
    std::vector<BasePaymentDocument> items_to_process;
    bool show_group_operation_progress_popup = false;
    bool show_group_operation_confirmation_popup = false;
    std::function<void()> on_group_operation_confirm;
    std::atomic<bool> cancel_group_operation;
    void ProcessGroupOperation();

    int groupKosguId = -1;
    char groupKosguFilter[256];

    // Автоподбор платежа
    bool show_payment_match_popup = false;
    std::vector<DatabaseManager::PaymentMatch> payment_matches;
    int selected_payment_match_index = -1;

    // Групповой автоподбор платежей
    bool show_auto_match_popup = false;
    int group_auto_match_min_score = 50;
    bool group_auto_match_require_counterparty = true;
    std::vector<BasePaymentDocument> group_auto_match_docs;
    int group_auto_match_total;
    int group_auto_match_linked;
    int group_auto_match_no_match;
    int group_auto_match_chunk_start = 0;
    std::vector<std::string> group_auto_match_log;

    // Фильтры
    int doc_filter_index = 0; // 0: Все, 1: Для сверки, 2: Сверенные, 3: Не сверенные

    std::vector<BasePaymentDocument> m_filtered_documents;
    void UpdateFilteredDocuments();
    void SortDocuments(const struct ImGuiTableSortSpecs* sort_specs);
    void AutoMatchPayment();
    void ProcessAutoMatchPayments();
};
