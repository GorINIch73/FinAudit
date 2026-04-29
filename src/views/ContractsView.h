#pragma once

#include "BaseView.h"
#include <functional>
#include <atomic>
#include <vector>
#include <map>
#include "../Contract.h"
#include "../Counterparty.h"
#include "../Payment.h"

class UIManager; // Forward declaration

class ContractsView : public BaseView {
public:
    ContractsView();
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

    std::vector<Contract> contracts;
    Contract selectedContract;
    Contract originalContract;
    int selectedContractIndex;
    bool showEditModal;
    bool isAdding;
    bool isDirty = false;
    bool show_delete_popup = false;
    int contract_id_to_delete = -1;
    int destination_contract_id = -1;
    char contract_search_buffer[256] = {0};

    std::vector<ContractPaymentInfo> payment_info;
    std::vector<Counterparty> counterpartiesForDropdown;
    char filterText[256];
    char counterpartyFilter[256];
    float list_view_height = 200.0f;
    float editor_width = 400.0f;

    int contract_filter_index = 0;
    std::vector<SuspiciousWord> suspiciousWordsForFilter;

    std::vector<Contract> m_filtered_contracts;
    void UpdateFilteredContracts();
    void SortContracts(const struct ImGuiTableSortSpecs* sort_specs);
    void ApplyStoredSorting();
    void StoreSortSpecs(const struct ImGuiTableSortSpecs* sort_specs);
    struct SortSpec { int column_index; int sort_direction; };
    std::vector<SortSpec> m_stored_sort_specs;
    int scroll_to_item_index = -1;
    bool scroll_pending = false;
    std::map<int, std::vector<ContractPaymentInfo>> m_contract_details_map;
    UIManager* uiManager = nullptr;

    std::vector<ContractPaymentInfo> m_sorted_payment_info;
    void SortPaymentInfo(const struct ImGuiTableSortSpecs* sort_specs);

    enum GroupOperationType {
        NONE,
        SET_FOR_CHECKING,
        UNSET_FOR_CHECKING,
        SET_SPECIAL_CONTROL,
        UNSET_SPECIAL_CONTROL,
        CLEAR_PROCUREMENT_CODE
    };
    GroupOperationType current_operation = NONE;
    int processed_items = 0;
    std::vector<Contract> items_to_process;
    bool show_group_operation_progress_popup = false;
    bool show_group_operation_confirmation_popup = false;
    std::function<void()> on_group_operation_confirm;
    std::atomic<bool> cancel_group_operation;
    void ProcessGroupOperation();
};
