#pragma once

#include "BaseView.h"
#include <vector>
#include "../Kosgu.h"
#include "../Payment.h"
#include "../SuspiciousWord.h"

class KosguView : public BaseView {
public:
    KosguView();
    void Render() override;
    void SetDatabaseManager(DatabaseManager* dbManager) override;
    void SetPdfReporter(PdfReporter* pdfReporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;
    void OnDeactivate() override;
    bool ForceSave() override;

private:
    void RefreshData();
    bool SaveChanges();

    std::vector<Kosgu> kosguEntries;
    Kosgu selectedKosgu;
    Kosgu originalKosgu;
    int selectedKosguIndex;
    bool showEditModal;
    bool isAdding;
    bool isDirty = false;
    bool show_delete_popup = false;
    int kosgu_id_to_delete = -1;
    std::vector<ContractPaymentInfo> payment_info;
    std::vector<ContractPaymentInfo> m_filtered_payment_info;
    char filterText[256];
    
    std::vector<Kosgu> m_filtered_kosgu_entries;
    void UpdateFilteredKosgu();
    int m_filter_index = 0;
    std::vector<SuspiciousWord> suspiciousWordsForFilter;

    struct SortSpec { int column_index; int sort_direction; };
    std::vector<SortSpec> m_stored_sort_specs;
    void StoreSortSpecs(const struct ImGuiTableSortSpecs* sort_specs);
    void ApplyStoredSorting();
    int scroll_to_item_index = -1;
    bool scroll_pending = false;  // true: SetScrollY уже вызван, ждём отрисовки строки

    float list_view_height = 200.0f;
    float editor_width = 400.0f;
};
