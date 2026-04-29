#pragma once

#include "BaseView.h"
#include <vector>
#include <string>
#include "../Payment.h"
#include "../Counterparty.h"
#include "../Kosgu.h"
#include "../PaymentDetail.h"
#include "../Contract.h"
#include "../BasePaymentDocument.h"
#include "../Regex.h"
#include "imgui.h"
#include "CustomWidgets.h"

class UIManager; 

class PaymentsView : public BaseView {
public:
    PaymentsView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* uiManager);
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;
    void OnDeactivate() override;
    void ForceSave() override;

private:
    void RefreshData();
    void RefreshDropdownData();
    void SaveChanges();
    void SaveDetailChanges();

    UIManager* uiManager = nullptr;
    std::vector<Payment> payments;
    std::vector<Payment> m_filtered_payments;
    void UpdateFilteredPayments();
    struct SortSpec { int column_index; int sort_direction; };
    std::vector<SortSpec> m_stored_sort_specs;
    void StoreSortSpecs(const struct ImGuiTableSortSpecs* sort_specs);
    void ApplyStoredSorting();
    int scroll_to_item_index = -1;
    bool scroll_pending = false;
    Payment selectedPayment;
    Payment originalPayment;
    int selectedPaymentIndex;
    bool isAdding;
    bool isDirty = false;

    std::string descriptionBuffer;
    std::string noteBuffer;

    std::vector<PaymentDetail> paymentDetails;
    PaymentDetail selectedDetail;
    PaymentDetail originalDetail;
    int selectedDetailIndex;
    bool isAddingDetail;
    bool isDetailDirty = false;

    std::vector<Counterparty> counterpartiesForDropdown;
    std::vector<Kosgu> kosguForDropdown;
    std::vector<Contract> contractsForDropdown;
    std::vector<BasePaymentDocument> baseDocsForDropdown;
    std::vector<SuspiciousWord> suspiciousWordsForFilter;
    int selectedJo4DocumentId = -1;

    struct Jo4Candidate {
        int doc_id = -1;
        int score = 0;
        std::string reason;
        bool referenced_in_payment = false;
        bool amount_match = false;
        bool date_match = false;
        bool date_from_description_match = false;
        bool number_strong_match = false;
        bool date_strong_match = false;
    };
    bool jo4CacheDirty = true;
    int cachedJo4PaymentId = -1;
    std::vector<PaymentBaseDocumentLink> cachedJo4Links;
    std::vector<Jo4Candidate> cachedJo4Candidates;
    std::vector<Jo4Candidate> cachedJo4ReferencedCandidates;
    std::string cachedJo4RefsPreview;
    size_t cachedJo4RefsCount = 0;
    int cachedJo4DetailsDocId = -1;
    std::vector<BasePaymentDocumentDetail> cachedJo4Details;
    bool showJo4Panel = false;

    char filterText[256];
    char counterpartyFilter[256];
    char kosguFilter[256];
    char contractFilter[256];
    char invoiceFilter[256];

    int missing_info_filter_index = 0;

    // Group operations
    int groupKosguId = -1;
    int groupContractId = -1;
    int groupInvoiceId = -1;
    char groupKosguFilter[256];
    char groupContractFilter[256];
    char groupInvoiceFilter[256];

    // Bulk replace operations
    bool show_replace_popup = false;
    bool show_add_kosgu_popup = false;
    int replacement_target = 0; // 0 for KOSGU, 1 for Contract, 2 for Invoice
    int replacement_kosgu_id = -1;
    int replacement_contract_id = -1;
    int replacement_base_document_id = -1;
    char replacement_kosgu_filter[256]{};
    char replacement_contract_filter[256]{};
    char replacement_invoice_filter[256]{};

    // For "Create from Description" popup
    bool show_create_from_desc_popup = false;
    int entity_to_create = 0; // 0 for Contract, 1 for Invoice
    std::string extracted_number;
    std::string extracted_date;
    int existing_entity_id = -1;
    std::vector<Regex> regexesForCreatePopup;
    int selectedRegexIdForCreatePopup = -1;
    char editableRegexPatternForCreate[512] = "";
    char regexFilterForCreatePopup[128] = "";
    bool show_save_regex_popup = false;
    char newRegexNameBuffer[128] = "";
    const char* saveRegexStatusMsg = "";

    // For delete confirmation popups
    bool show_delete_payment_popup = false;
    bool show_delete_detail_popup = false;
    bool show_group_delete_popup = false;
    int payment_id_to_delete = -1;
    int detail_id_to_delete = -1;

    // For chunked group operations with progress bar
    enum GroupOperationType {
        NONE,
        ADD_KOSGU,
        REPLACE,
        DELETE_DETAILS,
        APPLY_REGEX
    };
    GroupOperationType current_operation = NONE;
    int processed_items = 0;
    std::vector<Payment> items_to_process;
    void ProcessGroupOperation();

    // State for "Apply Regex" popup
    bool show_apply_regex_popup = false;
    int regex_target = 0; // 0 for Contract, 1 for Invoice, 2 for Kosgu
    int selected_regex_id = -1;
    std::string selected_regex_name;
    char regex_filter[128]{};
    std::vector<Regex> regexesForDropdown;

    float list_view_height = 200.0f;
    float editor_width = 400.0f;

    double total_filtered_amount = 0.0;
    double total_filtered_details_amount = 0.0;

    void SortPayments(const ImGuiTableSortSpecs* sort_specs);
    void RenderJo4DocumentsPanel();
    void InvalidateJo4Cache();
    void RebuildJo4Cache();
};
