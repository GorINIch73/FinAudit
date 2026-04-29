#pragma once

#include "BaseView.h"
#include <vector>
#include <map>
#include "../Counterparty.h"
#include "../Payment.h"

class CounterpartiesView : public BaseView {
public:
    CounterpartiesView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;
    void OnDeactivate() override;
    void ForceSave() override;

private:
    void RefreshData();
    void SaveChanges();
    void UpdateFilteredCounterparties();

    std::vector<Counterparty> counterparties;
    std::vector<Counterparty> m_filtered_counterparties;
    std::map<int, std::vector<CounterpartyPaymentInfo>> m_counterparty_details_map;

    struct SortSpec { int column_index; int sort_direction; };
    std::vector<SortSpec> m_stored_sort_specs;
    void StoreSortSpecs(const struct ImGuiTableSortSpecs* sort_specs);
    void ApplyStoredSorting();
    int scroll_to_item_index = -1;
    bool scroll_pending = false;

    Counterparty selectedCounterparty;
    Counterparty originalCounterparty;
    bool showEditModal;
    bool isAdding;
    bool isDirty = false;
    bool show_delete_popup = false;
    int counterparty_id_to_delete = -1;
    std::vector<ContractPaymentInfo> payment_info;
    std::vector<ContractPaymentInfo> m_sorted_payment_info;
    char filterText[256];
    float list_view_height = 200.0f;
    float editor_width = 400.0f;

    void SortPaymentInfo(const struct ImGuiTableSortSpecs* sort_specs);
};
