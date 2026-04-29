#pragma once

#include "views/BaseView.h"
#include "IconsFontAwesome6.h"
#include <string>

class SelectiveCleanView : public BaseView {
public:
    SelectiveCleanView() {
        IsVisible = true;
        SetTitle(ICON_FA_ERASER " Очистка базы");
    }

    void Render() override;
    void SetDatabaseManager(DatabaseManager* manager) override;
    void SetPdfReporter(PdfReporter* reporter) override;
    void SetUIManager(UIManager* manager) override;
    std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> GetDataAsStrings() override;


private:
    void ShowConfirmationModal();

    enum class CleanTarget {
        None,
        Payments,
        Counterparties,
        Contracts,
        BasePaymentDocuments,
        OrphanDetails
    };

    CleanTarget currentCleanTarget = CleanTarget::None;
    std::string confirmationMessage;
    std::string resultMessage;
    bool show_confirmation_modal_internal = false;
};
