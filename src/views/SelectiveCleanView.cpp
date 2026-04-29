#include "views/SelectiveCleanView.h"
#include "../UIManager.h"
#include "imgui.h"
#include <string>
#include "../CustomWidgets.h" // Добавлено для CustomWidgets::ConfirmationModal

void SelectiveCleanView::Render() {
    if (!IsVisible) {
        return;
    }

    ImGui::Begin(GetTitle(), &IsVisible);

    ImGui::TextWrapped("Эта форма позволяет проверять целостность базы и выборочно удалять данные. Перед каждой операцией очистки автоматически создается резервная копия рядом с текущей базой.");
    ImGui::Separator();

    ImGui::Text("Проверка целостности:");
    ImGui::BeginDisabled(!dbManager || !dbManager->is_open());
    if (ImGui::Button(ICON_FA_DATABASE " Проверить базу")) {
        integrityIssues = dbManager->getIntegrityReport();
        showIntegrityReport = true;
    }
    ImGui::EndDisabled();

    if (showIntegrityReport) {
        ImGui::SameLine();
        if (ImGui::Button("Скрыть результат")) {
            showIntegrityReport = false;
        }

        if (ImGui::BeginTable("db_operations_integrity_report", 3,
                              ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable,
                              ImVec2(0, 220))) {
            ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed,
                                    110.0f);
            ImGui::TableSetupColumn("Проверка", ImGuiTableColumnFlags_WidthFixed,
                                    220.0f);
            ImGui::TableSetupColumn("Детали");
            ImGui::TableHeadersRow();

            for (const auto& issue : integrityIssues) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(issue.severity.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(issue.check_name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", issue.details.c_str());
            }

            ImGui::EndTable();
        }
    }

    ImGui::Separator();

    ImGui::Text("Очистка основных таблиц:");
    if (ImGui::Button(ICON_FA_MONEY_BILL " Удалить все платежи и расшифровки")) {
        currentCleanTarget = CleanTarget::Payments;
        confirmationMessage = "Вы уверены, что хотите удалить ВСЕ платежи и связанные с ними расшифровки?";
        ImGui::OpenPopup("ConfirmationModal");
        show_confirmation_modal_internal = true;
    }

    if (ImGui::Button(ICON_FA_ADDRESS_BOOK " Удалить всех контрагентов")) {
        currentCleanTarget = CleanTarget::Counterparties;
        confirmationMessage = "Вы уверены, что хотите удалить ВСЕХ контрагентов? Это может нарушить целостность данных, если существуют связанные договоры или платежи.";
        ImGui::OpenPopup("ConfirmationModal");
        show_confirmation_modal_internal = true;
    }

    if (ImGui::Button(ICON_FA_FILE_CONTRACT " Удалить все договоры")) {
        currentCleanTarget = CleanTarget::Contracts;
        confirmationMessage = "Вы уверены, что хотите удалить ВСЕ договоры? Это может нарушить целостность данных, если существуют связанные платежи.";
        ImGui::OpenPopup("ConfirmationModal");
        show_confirmation_modal_internal = true;
    }

    if (ImGui::Button(ICON_FA_FILE_LINES " Удалить все документы основания")) {
        currentCleanTarget = CleanTarget::BasePaymentDocuments;
        confirmationMessage = "Вы уверены, что хотите удалить ВСЕ документы основания и связанные расшифровки? Это может нарушить целостность данных, если существуют связанные платежи.";
        ImGui::OpenPopup("ConfirmationModal");
        show_confirmation_modal_internal = true;
    }
    
    ImGui::Separator();
    ImGui::Text("Обслуживание связей:");
    if (ImGui::Button(ICON_FA_BROOM " Очистить 'повисшие' расшифровки")) {
        currentCleanTarget = CleanTarget::OrphanDetails;
        confirmationMessage = "Будет выполнен поиск и удаление записей в таблице расшифровок, которые не связаны ни с одним платежом. Продолжить?";
        ImGui::OpenPopup("ConfirmationModal");
        show_confirmation_modal_internal = true;
    }

    if (!resultMessage.empty()) {
        ImGui::Separator();
        ImGui::Text("%s", resultMessage.c_str());
    }

    ShowConfirmationModal();

    ImGui::End();
}

void SelectiveCleanView::ShowConfirmationModal() {
    if (CustomWidgets::ConfirmationModal("ConfirmationModal", "Подтверждение операции", confirmationMessage.c_str(), "Да, я уверен", "Отмена", show_confirmation_modal_internal)) {
        bool success = false;
        std::string backupPath;
        if (dbManager) {
            if (BackupBeforeDangerousOperation(backupPath)) {
                switch (currentCleanTarget) {
                    case CleanTarget::Payments:       success = dbManager->ClearPayments(); break;
                    case CleanTarget::Counterparties: success = dbManager->ClearCounterparties(); break;
                    case CleanTarget::Contracts:      success = dbManager->ClearContracts(); break;
                    case CleanTarget::BasePaymentDocuments: success = dbManager->ClearBasePaymentDocuments(); break;
                    case CleanTarget::OrphanDetails:  success = dbManager->CleanOrphanPaymentDetails(); break;
                    case CleanTarget::None: break;
                }
            }
        }
        if (success) {
            resultMessage = "Операция выполнена успешно. Резервная копия: " + backupPath;
        } else {
            resultMessage = "Ошибка при выполнении операции.";
        }
        currentCleanTarget = CleanTarget::None;
    } else if (!show_confirmation_modal_internal && currentCleanTarget != CleanTarget::None) {
        // If modal was closed by "Отмена" or other means, and a target was set
        resultMessage = "Операция отменена.";
        currentCleanTarget = CleanTarget::None;
    }
}

bool SelectiveCleanView::BackupBeforeDangerousOperation(std::string& backupPath) {
    if (!uiManager || !uiManager->BackupCurrentDatabase("cleanup", backupPath)) {
        resultMessage = "Ошибка: не удалось создать резервную копию перед очисткой.";
        return false;
    }

    return true;
}

void SelectiveCleanView::SetDatabaseManager(DatabaseManager* manager) {
    this->dbManager = manager;
}

void SelectiveCleanView::SetUIManager(UIManager* manager) {
    this->uiManager = manager;
}

void SelectiveCleanView::SetPdfReporter(PdfReporter* reporter) {
    this->pdfReporter = reporter;
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> SelectiveCleanView::GetDataAsStrings() {
    // This view doesn't present tabular data for export, so return empty.
    return {};
}
