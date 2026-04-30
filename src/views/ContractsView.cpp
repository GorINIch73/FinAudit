#include "ContractsView.h"
#include "../CustomWidgets.h"
#include "../IconsFontAwesome6.h"
#include "../SuspiciousWord.h"
#include "../UIManager.h" // Added include for UIManager
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream> // For std::ostringstream

#include <cstdlib>
#include <functional> // Добавить для std::function

ContractsView::ContractsView()
    : selectedContractIndex(-1),
      showEditModal(false),
      isAdding(false),
      isDirty(false),
      cancel_group_operation(false),
      scroll_to_item_index(-1) {
    Title = "Справочник 'Договоры'";
    memset(filterText, 0, sizeof(filterText));
    memset(counterpartyFilter, 0, sizeof(counterpartyFilter));
    memset(contract_search_buffer, 0, sizeof(contract_search_buffer));
}

void ContractsView::SetDatabaseManager(DatabaseManager *manager) {
    dbManager = manager;
}

void ContractsView::SetPdfReporter(PdfReporter *reporter) {
    pdfReporter = reporter;
}

void ContractsView::SetUIManager(UIManager *manager) { uiManager = manager; }

void ContractsView::RefreshData() {
    if (dbManager) {
        contracts = dbManager->getContracts();
        selectedContractIndex = -1;
        UpdateFilteredContracts();
    }
}

void ContractsView::RefreshDropdownData() {
    if (dbManager) {
        counterpartiesForDropdown = dbManager->getCounterparties();
        suspiciousWordsForFilter = dbManager->getSuspiciousWords();
    }
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
ContractsView::GetDataAsStrings() {
    std::vector<std::string> headers = {"ID", "Номер", "Дата", "Контрагент",
                                        "Сумма"};
    std::vector<std::vector<std::string>> rows;
    for (const auto &entry : contracts) {
        std::string counterpartyName = "N/A";
        for (const auto &cp : counterpartiesForDropdown) {
            if (cp.id == entry.counterparty_id) {
                counterpartyName = cp.name;
                break;
            }
        }
        rows.push_back({std::to_string(entry.id), entry.number, entry.date,
                        counterpartyName, std::to_string(entry.total_amount)});
    }
    return {headers, rows};
}

void ContractsView::OnDeactivate() { SaveChanges(); }
bool ContractsView::ForceSave() { return SaveChanges(); }

bool ContractsView::SaveChanges() {
    // Сравниваем поля редактора с оригиналом
    bool hasChanges = (selectedContract.id != -1) && (
        selectedContract.number != originalContract.number ||
        selectedContract.date != originalContract.date ||
        selectedContract.counterparty_id != originalContract.counterparty_id ||
        selectedContract.contract_amount != originalContract.contract_amount ||
        selectedContract.end_date != originalContract.end_date ||
        selectedContract.procurement_code != originalContract.procurement_code ||
        selectedContract.note != originalContract.note ||
        selectedContract.is_for_checking != originalContract.is_for_checking ||
        selectedContract.is_for_special_control != originalContract.is_for_special_control ||
        selectedContract.is_found != originalContract.is_found
    );

    if (!hasChanges) {
        return true;
    }

    if (dbManager && selectedContract.id != -1) {
        if (!dbManager->updateContract(selectedContract)) {
            return false;
        }
        // Обновляем запись в локальных массивых
        auto it = std::find_if(
            contracts.begin(), contracts.end(), [&](const Contract &c) {
                return c.id == selectedContract.id;
            });
        if (it != contracts.end()) {
            *it = selectedContract;
        }
        for (auto& fc : m_filtered_contracts) {
            if (fc.id == selectedContract.id) { fc = selectedContract; break; }
        }
        // Применяем сохранённую сортировку
        ApplyStoredSorting();
        originalContract = selectedContract;
    } else {
        return false;
    }

    isDirty = false;
    return true;
}

void ContractsView::SortContracts(const ImGuiTableSortSpecs *sort_specs) {
    // Сохраняем текущую сортировку для восстановления
    StoreSortSpecs(sort_specs);

    std::sort(
        m_filtered_contracts.begin(), m_filtered_contracts.end(),
        [&](const Contract &a, const Contract &b) {
            for (int i = 0; i < sort_specs->SpecsCount; i++) {
                const ImGuiTableColumnSortSpecs *column_spec =
                    &sort_specs->Specs[i];
                int delta = 0;

                auto get_cp_name = [&](int cp_id) {
                    for (const auto &cp : counterpartiesForDropdown) {
                        if (cp.id == cp_id)
                            return cp.name;
                    }
                    return std::string("");
                };

                switch (column_spec->ColumnIndex) {
                case 0:
                    delta = (a.id < b.id) ? -1 : (a.id > b.id) ? 1 : 0;
                    break;
                case 1:
                    delta = a.number.compare(b.number);
                    break;
                case 2:
                    delta = a.date.compare(b.date);
                    break;
                case 3:
                    delta = get_cp_name(a.counterparty_id)
                                .compare(get_cp_name(b.counterparty_id));
                    break;
                case 4:
                    delta = (a.contract_amount < b.contract_amount)   ? -1
                            : (a.contract_amount > b.contract_amount) ? 1
                                                                      : 0;
                    break;
                case 5:
                    delta = (a.total_amount < b.total_amount)   ? -1
                            : (a.total_amount > b.total_amount) ? 1
                                                                : 0;
                    break;
                case 6:
                    delta = a.end_date.compare(b.end_date);
                    break;
                case 7:
                    delta = a.procurement_code.compare(b.procurement_code);
                    break;
                case 8:
                    delta = a.note.compare(b.note);
                    break;
                case 9:
                    delta = (a.is_for_checking < b.is_for_checking)   ? -1
                            : (a.is_for_checking > b.is_for_checking) ? 1
                                                                      : 0;
                    break;
                case 10:
                    delta =
                        (a.is_for_special_control < b.is_for_special_control)
                            ? -1
                        : (a.is_for_special_control > b.is_for_special_control)
                            ? 1
                            : 0;
                    break;
                case 11:
                    delta = (a.is_found < b.is_found)   ? -1
                            : (a.is_found > b.is_found) ? 1
                                                        : 0;
                    break;
                default:
                    break;
                }

                if (delta != 0) {
                    return (column_spec->SortDirection ==
                            ImGuiSortDirection_Ascending)
                               ? (delta < 0)
                               : (delta > 0);
                }
            }
            return false;
        });
}

void ContractsView::StoreSortSpecs(const ImGuiTableSortSpecs* sort_specs) {
    m_stored_sort_specs.clear();
    if (sort_specs && sort_specs->SpecsCount > 0) {
        for (int i = 0; i < sort_specs->SpecsCount; i++) {
            m_stored_sort_specs.push_back({
                sort_specs->Specs[i].ColumnIndex,
                sort_specs->Specs[i].SortDirection
            });
        }
    }
}

void ContractsView::ApplyStoredSorting() {
    if (m_stored_sort_specs.empty()) {
        return;
    }
    // Создаём фейковую ImGuiTableSortSpecs для совместимости с SortContracts
    std::vector<ImGuiTableColumnSortSpecs> fake_specs(m_stored_sort_specs.size());
    for (size_t i = 0; i < m_stored_sort_specs.size(); i++) {
        fake_specs[i].ColumnIndex = m_stored_sort_specs[i].column_index;
        fake_specs[i].SortDirection = static_cast<ImGuiSortDirection>(m_stored_sort_specs[i].sort_direction);
    }
    ImGuiTableSortSpecs wrapped_specs;
    wrapped_specs.Specs = fake_specs.data();
    wrapped_specs.SpecsCount = fake_specs.size();
    SortContracts(&wrapped_specs);
}

bool ContractsView::StartGroupOperation(GroupOperationType operation) {
    if (!dbManager || current_operation != NONE || m_filtered_contracts.empty()) {
        return false;
    }

    if (uiManager) {
        std::string backupPath;
        if (!uiManager->BackupCurrentDatabase("group_contracts", backupPath)) {
            return false;
        }
    }

    cancel_group_operation = false;
    items_to_process = m_filtered_contracts;
    processed_items = 0;
    group_transaction_active = false;
    current_operation = operation;
    return true;
}

void ContractsView::ProcessGroupOperation() {
    if (!dbManager || current_operation == NONE || items_to_process.empty()) {
        if (group_transaction_active && dbManager) {
            dbManager->rollbackTransaction();
            group_transaction_active = false;
        }
        show_group_operation_progress_popup = false;
        return;
    }

    if (!group_transaction_active && processed_items == 0) {
        if (!dbManager->beginTransaction()) {
            current_operation = NONE;
            processed_items = 0;
            items_to_process.clear();
            show_group_operation_progress_popup = false;
            cancel_group_operation = false;
            return;
        }
        group_transaction_active = true;
    }

    const int items_per_frame = 20;
    int processed_in_frame = 0;

    while (processed_items < items_to_process.size() &&
           processed_in_frame < items_per_frame) {
        // Check for cancellation
        if (cancel_group_operation) {
            if (group_transaction_active) {
                dbManager->rollbackTransaction();
                group_transaction_active = false;
            }
            current_operation = NONE;
            processed_items = 0;
            items_to_process.clear();
            show_group_operation_progress_popup = false;
            cancel_group_operation = false; // Reset the flag
            RefreshData(); // Refresh to reflect any partial changes
            return;        // Exit early
        }

        const auto &contract = items_to_process[processed_items];

        switch (current_operation) {
        case SET_FOR_CHECKING:
            dbManager->updateContractFlags(contract.id, true,
                                           contract.is_for_special_control);
            break;
        case UNSET_FOR_CHECKING:
            dbManager->updateContractFlags(contract.id, false,
                                           contract.is_for_special_control);
            break;
        case SET_SPECIAL_CONTROL:
            dbManager->updateContractFlags(contract.id,
                                           contract.is_for_checking, true);
            break;
        case UNSET_SPECIAL_CONTROL:
            dbManager->updateContractFlags(contract.id,
                                           contract.is_for_checking, false);
            break;
        case CLEAR_PROCUREMENT_CODE:
            dbManager->updateContractProcurementCode(contract.id, "");
            break;
        case NONE: // Should not happen
            break;
        }

        processed_items++;
        processed_in_frame++;
    }

    if (processed_items >= items_to_process.size()) {
        if (group_transaction_active) {
            if (!dbManager->commitTransaction()) {
                dbManager->rollbackTransaction();
            }
            group_transaction_active = false;
        }

        // Operation finished
        current_operation = NONE;
        processed_items = 0;
        items_to_process.clear();
        show_group_operation_progress_popup = false;
        cancel_group_operation = false; // Reset the flag
        RefreshData();                  // Refresh to reflect changes
    }
}

void ContractsView::Render() {
    if (!IsVisible) {
        if (isDirty) {
            SaveChanges();
        }
        return;
    }

    // Handle chunked group operation processing
    if (current_operation != NONE) {
        ProcessGroupOperation();
        show_group_operation_progress_popup = true;
    }

    if (ImGui::Begin(GetTitle(), &IsVisible)) {

        // --- Progress Bar Popup ---
        if (show_group_operation_progress_popup && current_operation != NONE) {
            ImGui::OpenPopup("Выполнение групповой операции...");
        }
        if (ImGui::BeginPopupModal("Выполнение групповой операции...", NULL,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoMove)) {
            if (current_operation == NONE) { // Operation finished
                show_group_operation_progress_popup = false;
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::Text("Обработка %d из %zu...", processed_items,
                            items_to_process.size());
                float progress = (items_to_process.empty())
                                     ? 0.0f
                                     : (float)processed_items /
                                           (float)items_to_process.size();
                ImGui::ProgressBar(progress, ImVec2(250.0f, 0.0f));

                if (ImGui::Button("Отмена")) {
                    cancel_group_operation = true;
                    // No need to close popup here, ProcessGroupOperation will
                    // handle it on next frame
                }
            }
            ImGui::EndPopup();
        }

        if (dbManager && contracts.empty()) {
            RefreshData();
            RefreshDropdownData();
        }

        // Панель управления
        if (ImGui::Button(ICON_FA_PLUS " Добавить")) {
            SaveChanges();
            // Сразу создаём пустую запись в БД с реальным ID
            Contract newContract;
            newContract.id = -1;
            newContract.number = "";
            newContract.date = "";
            newContract.counterparty_id = -1;
            newContract.total_amount = 0.0;
            newContract.contract_amount = 0.0;
            newContract.end_date = "";
            newContract.procurement_code = "";
            newContract.note = "";
            newContract.is_for_checking = false;
            newContract.is_for_special_control = false;
            newContract.is_found = false;
            int new_id = -1;
            if (dbManager) {
                new_id = dbManager->addContract(newContract);
            }
            // Обновляем contracts из БД
            contracts = dbManager->getContracts();
            counterpartiesForDropdown = dbManager->getCounterparties();
            m_filtered_contracts = contracts;
            UpdateFilteredContracts();
            ApplyStoredSorting();
            // Находим новую запись в отсортированном списке
            selectedContractIndex = -1;
            scroll_to_item_index = -1;
            scroll_pending = false;
            for (int i = 0; i < (int)m_filtered_contracts.size(); i++) {
                if (m_filtered_contracts[i].id == new_id) {
                    selectedContractIndex = i;
                    scroll_to_item_index = i;
                    break;
                }
            }
            if (selectedContractIndex == -1) {
                // Новая запись отфильтрована — добавляем вручную
                auto new_it = std::find_if(contracts.begin(), contracts.end(),
                    [&](const Contract &c) { return c.id == new_id; });
                if (new_it != contracts.end()) {
                    m_filtered_contracts.push_back(*new_it);
                    ApplyStoredSorting();
                    for (int i = 0; i < (int)m_filtered_contracts.size(); i++) {
                        if (m_filtered_contracts[i].id == new_id) {
                            selectedContractIndex = i;
                            scroll_to_item_index = i;
                            break;
                        }
                    }
                }
            }
            if (selectedContractIndex != -1) {
                selectedContract = m_filtered_contracts[selectedContractIndex];
                originalContract = selectedContract;
            }
            isAdding = false;
            isDirty = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " Удалить")) {
            if (selectedContractIndex != -1 &&
                selectedContractIndex < (int)contracts.size()) {
                contract_id_to_delete = contracts[selectedContractIndex].id;
                show_delete_popup = true;
                destination_contract_id = -1; // Reset on popup open
                memset(contract_search_buffer, 0,
                       sizeof(contract_search_buffer));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Обновить")) {
            SaveChanges();
            RefreshData();
            RefreshDropdownData();
        }
        ImGui::SameLine();
        if (ImGui::Button("Список по фильтру")) { // New button for report
            if (uiManager && !m_filtered_contracts.empty()) {
                std::ostringstream oss;
                oss << "SELECT c.id AS ID, c.number AS 'Номер договора'"
                       ", c.date AS 'Дата договора', cp.name AS 'Контрагент'"
                       ", COALESCE(ds.total_details_amount, 0.0) AS 'Всего "
                       "сумма"
                       "по расшифровкам'"
                       ", COALESCE(ds.details_count, 0) AS 'Количество "
                       "расшифровок'"
                       ", CASE WHEN c.is_for_checking THEN 'Да'"
                       " ELSE 'Нет'"
                       " END AS 'Для проверки'"
                       ", CASE WHEN c.is_for_special_control THEN 'Да'"
                       " ELSE 'Нет'"
                       " END AS 'Для контроля', c.note AS 'Примечание'"
                       " FROM Contracts c LEFT JOIN Counterparties cp ON "
                       "c.counterparty_id = cp.id LEFT JOIN (SELECT "
                       "contract_id, SUM(amount) AS total_details_amount, "
                       "COUNT(*) AS details_count FROM PaymentDetails "
                       "GROUP "
                       "BY contract_id) AS ds ON c.id = ds.contract_id "
                       "WHERE contract_id IN (";

                for (size_t i = 0; i < m_filtered_contracts.size(); ++i) {
                    oss << m_filtered_contracts[i].id;
                    if (i < m_filtered_contracts.size() - 1) {
                        oss << ", ";
                    }
                }
                oss << ") "
                       "ORDER "
                       "BY c.date DESC ";
                uiManager->CreateSpecialQueryView("Отчет по договорам",
                                                  oss.str());
            } else if (uiManager && m_filtered_contracts.empty()) {
                // If no contracts are filtered, show an empty report or a
                // message
                uiManager->CreateSpecialQueryView(
                    "Отчет по договорам",
                    "SELECT c.number AS 'Номер договора'"
                    ", c.date AS 'Дата договора', cp.name AS 'Контрагент'"
                    ", COALESCE(ds.total_details_amount, 0.0) AS 'Всего сумма"
                    "по расшифровкам'"
                    ", COALESCE(ds.details_count, 0) AS 'Количество "
                    "расшифровок'"
                    ", CASE WHEN c.is_for_checking THEN 'Да'"
                    " ELSE 'Нет'"
                    " END AS 'Для проверки'"
                    ", CASE WHEN c.is_for_special_control THEN 'Да'"
                    " ELSE 'Нет'"
                    " END AS 'Для контроля', c.note AS 'Примечание'"
                    " FROM Contracts c LEFT JOIN Counterparties cp ON "
                    "c.counterparty_id = cp.id LEFT JOIN (SELECT "
                    "contract_id, SUM(amount) AS total_details_amount, "
                    "COUNT(*) AS details_count FROM PaymentDetails GROUP "
                    "BY contract_id) AS ds ON c.id = ds.contract_id ORDER "
                    "BY c.date DESC;"); // Empty result
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Подробный список")) { // New button for report
            if (uiManager && !m_filtered_contracts.empty()) {
                std::ostringstream oss;
                oss << "SELECT c.number AS 'Номер договора'"
                       ", c.date AS 'Дата договора'"
                       ", cp.name AS 'Контрагент по договору'"
                       ", p.date AS 'Дата платежа'"
                       ", p.doc_number AS 'Номер платежа' "
                       ", p.amount AS 'Сумма по ПП'"
                       ", pd.amount AS 'Сумма по расшифровке' "
                       ", k.code AS 'КОСГУ' "
                       ", p.description AS 'Назначение платежа' "
                       ", c.note AS 'Примечание' "
                       " FROM PaymentDetails pd"
                       " JOIN "
                       "Payments p ON pd.payment_id = p.id JOIN Contracts c ON "
                       " pd.contract_id = "
                       " c.id LEFT JOIN Counterparties cp ON c.counterparty_id "
                       "= "
                       " cp.id LEFT JOIN KOSGU k ON pd.kosgu_id = k.id WHERE "
                       "pd.contract_id IS NOT NULL AND "
                       "contract_id IN (";

                for (size_t i = 0; i < m_filtered_contracts.size(); ++i) {
                    oss << m_filtered_contracts[i].id;
                    if (i < m_filtered_contracts.size() - 1) {
                        oss << ", ";
                    }
                }
                oss << ") ORDER BY c.date DESC, p.date DESC ";
                uiManager->CreateSpecialQueryView(
                    "Отчет по договорам подробный", oss.str());
            } else if (uiManager && m_filtered_contracts.empty()) {
                // If no contracts are filtered, show an empty report or a
                // message
                uiManager->CreateSpecialQueryView(
                    "Отчет по договорам",
                    "SELECT c.number AS 'Номер договора'"
                    ", c.date AS 'Дата договора'"
                    ", cp.name AS 'Контрагент по договору'"
                    ", p.date AS 'Дата платежа'"
                    ", p.doc_number AS 'Номер платежа' "
                    ", p.amount AS 'Сумма по ПП'"
                    ", pd.amount AS 'Сумма по расшифровке' "
                    ", k.code AS 'КОСГУ' "
                    ", p.description AS 'Назначение платежа' "
                    ", c.note AS 'Примечание' "
                    " FROM PaymentDetails pd"
                    " JOIN "
                    "Payments p ON pd.payment_id = p.id JOIN Contracts c ON "
                    " pd.contract_id = "
                    " c.id LEFT JOIN Counterparties cp ON c.counterparty_id "
                    "= "
                    " cp.id LEFT JOIN KOSGU k ON pd.kosgu_id = k.id WHERE "
                    "pd.contract_id IS NOT NULL "
                    " ORDER BY c.date DESC, p.date DESC "); // Empty result
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Итоги для акта")) { // New button for report
            if (uiManager) {
                std::ostringstream oss;
                // oss << "SELECT * FROM Contracts WHERE id IN (";
                oss << "SELECT KOSGU.code, KOSGU.name, "
                       "ROUND(SUM(PaymentDetails.amount),2), COUNT(DISTINCT "
                       "contracts.id) FROM PaymentDetails inner join Contracts "
                       "on PaymentDetails.contract_id=contracts.id inner join "
                       "KOSGU on PaymentDetails.kosgu_id=KOSGU.id inner join "
                       "Payments on PaymentDetails.payment_id=Payments.id "
                       "inner join Counterparties on "
                       "Contracts.counterparty_id=Counterparties.id WHERE "
                       "Contracts.is_found=true GROUP BY "
                       "KOSGU.code";

                uiManager->CreateSpecialQueryView("Отчет по договорам для акта",
                                                  oss.str());
            }
        }

        if (CustomWidgets::ConfirmationModal(
                "Подтверждение удаления контракты", "Подтверждение удаления",
                "Вы уверены, что хотите удалить этот договор?\nЭто действие "
                "нельзя отменить.",
                "Да", "Нет", show_delete_popup,
                [&]() { // Lambda for additional content
                    ImGui::Text("Перенести расшифровки на другой договор "
                                "(необязательно):");
                    std::vector<CustomWidgets::ComboItem> contractItems;
                    for (const auto &c : contracts) {
                        if (c.id != contract_id_to_delete) {
                            contractItems.push_back(
                                {c.id, c.number + "  " + c.date});
                        }
                    }
                    CustomWidgets::ComboWithFilter(
                        "Новый договор##contract_transfer",
                        destination_contract_id, contractItems,
                        contract_search_buffer, sizeof(contract_search_buffer),
                        0);
                })) {
            if (dbManager && contract_id_to_delete != -1) {
                if (destination_contract_id != -1) {
                    dbManager->transferPaymentDetails(contract_id_to_delete,
                                                      destination_contract_id);
                }
                dbManager->deleteContract(contract_id_to_delete);
                RefreshData();
                selectedContract = Contract{};
                originalContract = Contract{};
                selectedContractIndex = -1; // Reset selection
            }
            contract_id_to_delete = -1;
        }

        // --- Group Operation Confirmation Modal ---
        if (CustomWidgets::ConfirmationModal(
                "Подтверждение групповой операции", "Подтверждение операции",
                ("Вы уверены, что хотите применить эту операцию ко всем " +
                 std::to_string(m_filtered_contracts.size()) +
                 " отфильтрованным договорам? Это действие может быть "
                 "необратимым.")
                    .c_str(),
                "Да", "Нет", show_group_operation_confirmation_popup)) {
            if (this->on_group_operation_confirm) {
                this->on_group_operation_confirm();
            }
            // Reset the flag after handling the confirmation
            show_group_operation_confirmation_popup = false;
        }

        ImGui::Separator();

        bool filter_changed = false;

        float combo_width = 220.0f;
        // Estimate the width of the clear button. A simple square button is
        // usually FrameHeight.
        float clear_button_width =
            ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float input_width = ImGui::GetContentRegionAvail().x - combo_width -
                            clear_button_width - spacing;

        ImGui::PushItemWidth(input_width);
        if (ImGui::InputText("Фильтр", filterText, sizeof(filterText))) {
            filter_changed = true;
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK "##clear_filter_contract")) {
            if (filterText[0] != '\0') {
                filterText[0] = '\0';
                filter_changed = true;
            }
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(combo_width);
        const char *filter_items[] = {
            "Все",           "Для проверки", "Усиленный контроль",
            "С примечанием", "С ИКЗ",        "Подозрительное в платежах",
            "Ненайденные"};
        if (ImGui::Combo("Фильтр по статусу", &contract_filter_index,
                         filter_items, IM_ARRAYSIZE(filter_items))) {
            filter_changed = true;
        }
        ImGui::PopItemWidth();

        if (filter_changed) {
            SaveChanges();
            UpdateFilteredContracts();
        }

        // --- Групповые операции ---
        if (ImGui::CollapsingHeader("Групповые операции")) {
            ImGui::Text("Применить к %zu отфильтрованным договорам:",
                        m_filtered_contracts.size());
            if (ImGui::Button("Установить 'Для проверки'")) {
                if (!m_filtered_contracts.empty() &&
                    current_operation == NONE) {
                    this->on_group_operation_confirm = [this]() {
                        this->StartGroupOperation(SET_FOR_CHECKING);
                    };
                    show_group_operation_confirmation_popup = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Снять 'Для проверки'")) {
                if (!m_filtered_contracts.empty() &&
                    current_operation == NONE) {
                    this->on_group_operation_confirm = [this]() {
                        this->StartGroupOperation(UNSET_FOR_CHECKING);
                    };
                    show_group_operation_confirmation_popup = true;
                }
            }
            if (ImGui::Button("Установить 'Усиленный контроль'")) {
                if (!m_filtered_contracts.empty() &&
                    current_operation == NONE) {
                    this->on_group_operation_confirm = [this]() {
                        this->StartGroupOperation(SET_SPECIAL_CONTROL);
                    };
                    show_group_operation_confirmation_popup = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Снять 'Усиленный контроль'")) {
                if (!m_filtered_contracts.empty() &&
                    current_operation == NONE) {
                    this->on_group_operation_confirm = [this]() {
                        this->StartGroupOperation(UNSET_SPECIAL_CONTROL);
                    };
                    show_group_operation_confirmation_popup = true;
                }
            }
            if (ImGui::Button("Очистить ИКЗ")) {
                if (!m_filtered_contracts.empty() &&
                    current_operation == NONE) {
                    this->on_group_operation_confirm = [this]() {
                        this->StartGroupOperation(CLEAR_PROCUREMENT_CODE);
                    };
                    show_group_operation_confirmation_popup = true;
                }
            }
        }
        ImGui::Separator();

        // Таблица со списком
        ImGui::BeginChild("ContractsList", ImVec2(0, list_view_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("contracts_table", 12,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Sortable |
                                  ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("ID", 0, 0.0f, 0);
            ImGui::TableSetupColumn("Номер", 0, 0.0f, 1);
            ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_DefaultSort,
                                    0.0f, 2);
            ImGui::TableSetupColumn("Контрагент", 0, 0.0f, 3);
            ImGui::TableSetupColumn("Сумма договора", 0, 0.0f, 4);
            ImGui::TableSetupColumn("Сумма платежей", 0, 0.0f, 5);
            ImGui::TableSetupColumn("Дата окончания", 0, 0.0f, 6);
            ImGui::TableSetupColumn("ИКЗ", 0, 0.0f, 7);
            ImGui::TableSetupColumn("Примечание", 0, 0.0f, 8);
            ImGui::TableSetupColumn("Проверка", 0, 0.0f, 9);
            ImGui::TableSetupColumn("Контроль", 0, 0.0f, 10);
            ImGui::TableSetupColumn("Найден", 0, 0.0f, 11);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty) {
                    SortContracts(sort_specs);
                    sort_specs->SpecsDirty = false;
                }
            }

            // Прокрутка к новой записи: SetScrollY вызывается ОДИН раз
            if (scroll_to_item_index >= 0 && scroll_to_item_index < (int)m_filtered_contracts.size()) {
                if (!scroll_pending) {
                    float row_y = scroll_to_item_index * ImGui::GetTextLineHeightWithSpacing();
                    ImGui::SetScrollY(row_y);
                    scroll_pending = true;
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(m_filtered_contracts.size());
            bool need_to_break = false;
            while (clipper.Step() && !need_to_break) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd && !need_to_break;
                     ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    int contract_id = m_filtered_contracts[i].id;

                    bool is_selected = (selectedContractIndex == i);
                    char label[256];
                    sprintf(label, "%d##%d", m_filtered_contracts[i].id, i);
                    if (ImGui::Selectable(
                            label, is_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
                        if (selectedContractIndex != i) {
                            SaveChanges();
                            // Обновляем contracts из БД
                            if (dbManager) {
                                contracts = dbManager->getContracts();
                                counterpartiesForDropdown = dbManager->getCounterparties();
                                m_filtered_contracts = contracts;
                                UpdateFilteredContracts();
                                ApplyStoredSorting();
                                // Находим новую строку в отсортированном списке
                                for (int j = 0; j < (int)m_filtered_contracts.size(); j++) {
                                    if (m_filtered_contracts[j].id == contract_id) {
                                        selectedContractIndex = j;
                                        selectedContract = m_filtered_contracts[j];
                                        originalContract = selectedContract;
                                        break;
                                    }
                                }
                                isAdding = false;
                                isDirty = false;
                                m_sorted_payment_info.clear();
                                payment_info =
                                    dbManager->getPaymentInfoForContract(
                                        selectedContract.id);
                            }
                            need_to_break = true;
                        }
                    }
                    if (!need_to_break && is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    // Прокрутка завершена — строка отрисована, сбрасываем оба флага
                    if (!need_to_break && scroll_to_item_index >= 0 && scroll_to_item_index == i) {
                        ImGui::SetScrollHereY(0.5f);
                        scroll_to_item_index = -1;
                        scroll_pending = false;
                    }

                    if (!need_to_break) {
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", m_filtered_contracts[i].number.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", m_filtered_contracts[i].date.c_str());
                        ImGui::TableNextColumn();
                        const char *counterpartyName = "N/A";
                        for (const auto &cp : counterpartiesForDropdown) {
                            if (cp.id == m_filtered_contracts[i].counterparty_id) {
                                counterpartyName = cp.name.c_str();
                                break;
                            }
                        }
                        ImGui::Text("%s", counterpartyName);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f",
                                    m_filtered_contracts[i].contract_amount);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f", m_filtered_contracts[i].total_amount);
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", m_filtered_contracts[i].end_date.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(
                            "%s", m_filtered_contracts[i].procurement_code.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", m_filtered_contracts[i].note.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(
                            m_filtered_contracts[i].is_for_checking ? "Да" : "Нет");
                        ImGui::TableNextColumn();
                        ImGui::Text(m_filtered_contracts[i].is_for_special_control
                                        ? "Да"
                                        : "Нет");
                        ImGui::TableNextColumn();
                        ImGui::Text(m_filtered_contracts[i].is_found ? "Да"
                                                                     : "Нет");
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        CustomWidgets::HorizontalSplitter("h_splitter", &list_view_height);

        // Редактор
        if ((selectedContractIndex != -1 && selectedContractIndex < (int)contracts.size()) || isAdding) {
            ImGui::BeginChild("ContractEditor", ImVec2(editor_width, 0), true);

            if (isAdding) {
                ImGui::Text("Добавление нового договора");
            } else {
                ImGui::Text("Редактирование договора ID: %d",
                            selectedContract.id);
            }

            char numberBuf[256];
            char dateBuf[12];

            snprintf(numberBuf, sizeof(numberBuf), "%s",
                     selectedContract.number.c_str());
            snprintf(dateBuf, sizeof(dateBuf), "%s",
                     selectedContract.date.c_str());

            if (ImGui::InputText("Номер", numberBuf, sizeof(numberBuf))) {
                selectedContract.number = numberBuf;
                isDirty = true;
            }
            if (CustomWidgets::InputDate("Дата", selectedContract.date)) {
                isDirty = true;
            }

            if (!counterpartiesForDropdown.empty()) {
                std::vector<CustomWidgets::ComboItem> counterpartyItems;
                for (const auto &cp : counterpartiesForDropdown) {
                    counterpartyItems.push_back({cp.id, cp.name});
                }
                if (CustomWidgets::ComboWithFilter(
                        "Контрагент", selectedContract.counterparty_id,
                        counterpartyItems, counterpartyFilter,
                        sizeof(counterpartyFilter), 0)) {
                    isDirty = true;
                }
            }

            ImGui::SetNextItemWidth(150.0f);
            if (CustomWidgets::AmountInput("Сумма договора",
                                           selectedContract.contract_amount)) {
                isDirty = true;
            }

            char endDateBuf[12];
            // snprintf(endDateBuf, sizeof(endDateBuf), "%s",
            //          selectedContract.end_date.c_str()); // This is no longer
            //          needed
            ImGui::SetNextItemWidth(100.0f);
            if (CustomWidgets::InputDate("Дата окончания",
                                         selectedContract.end_date)) {
                isDirty = true;
            }

            char procurementCodeBuf[256];
            snprintf(procurementCodeBuf, sizeof(procurementCodeBuf), "%s",
                     selectedContract.procurement_code.c_str());
            ImGui::SetNextItemWidth(250.0f);
            if (ImGui::InputText("ИКЗ", procurementCodeBuf,
                                 sizeof(procurementCodeBuf))) {
                selectedContract.procurement_code = procurementCodeBuf;
                isDirty = true;
            }

            char noteBuf[512];
            snprintf(noteBuf, sizeof(noteBuf), "%s",
                     selectedContract.note.c_str());
            if (ImGui::InputTextMultiline("Примечание", noteBuf,
                                          sizeof(noteBuf))) {
                selectedContract.note = noteBuf;
                isDirty = true;
            }

            if (ImGui::Checkbox("Для проверки",
                                &selectedContract.is_for_checking)) {
                isDirty = true;
            }
            if (ImGui::Checkbox("Усиленный контроль",
                                &selectedContract.is_for_special_control)) {
                isDirty = true;
            }
            if (ImGui::Checkbox("Найден", &selectedContract.is_found)) {
                isDirty = true;
            }

            ImGui::BeginDisabled(selectedContract.number.empty() &&
                                 selectedContract.procurement_code.empty());
            if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE
                              " Открыть на ГосЗакупках")) {
                std::string url;
                if (!selectedContract.procurement_code.empty()) {
                    // Используем шаблон из настроек
                    Settings settings = dbManager->getSettings();
                    std::string template_url = settings.zakupki_url_template;
                    
                    // Очищаем ИКЗ от невидимых символов
                    std::string clean_ikz = selectedContract.procurement_code;
                    while (!clean_ikz.empty()) {
                        unsigned char c0 = static_cast<unsigned char>(clean_ikz[0]);
                        
                        // UTF-8 BOM (EF BB BF) - U+FEFF
                        if (clean_ikz.size() >= 3 && c0 == 0xEF &&
                            static_cast<unsigned char>(clean_ikz[1]) == 0xBB &&
                            static_cast<unsigned char>(clean_ikz[2]) == 0xBF) {
                            clean_ikz.erase(0, 3);
                        }
                        // Неразрывный пробел U+00A0 (C2 A0)
                        else if (clean_ikz.size() >= 2 && c0 == 0xC2 &&
                                 static_cast<unsigned char>(clean_ikz[1]) == 0xA0) {
                            clean_ikz.erase(0, 2);
                        }
                        // U+2000..U+200F (E2 80 80 .. E2 80 8F)
                        else if (clean_ikz.size() >= 3 && c0 == 0xE2 &&
                                 static_cast<unsigned char>(clean_ikz[1]) == 0x80 &&
                                 static_cast<unsigned char>(clean_ikz[2]) >= 0x80 &&
                                 static_cast<unsigned char>(clean_ikz[2]) <= 0x8F) {
                            clean_ikz.erase(0, 3);
                        }
                        // U+202F NARROW NO-BREAK SPACE (E2 80 AF)
                        else if (clean_ikz.size() >= 3 && c0 == 0xE2 &&
                                 static_cast<unsigned char>(clean_ikz[1]) == 0x80 &&
                                 static_cast<unsigned char>(clean_ikz[2]) == 0xAF) {
                            clean_ikz.erase(0, 3);
                        }
                        // U+205F MEDIUM MATHEMATICAL SPACE (E2 81 9F)
                        else if (clean_ikz.size() >= 3 && c0 == 0xE2 &&
                                 static_cast<unsigned char>(clean_ikz[1]) == 0x81 &&
                                 static_cast<unsigned char>(clean_ikz[2]) == 0x9F) {
                            clean_ikz.erase(0, 3);
                        }
                        // Обычные ASCII пробелы
                        else if (std::isspace(c0)) {
                            clean_ikz.erase(0, 1);
                        }
                        else {
                            break;
                        }
                    }
                    
                    // Заменяем {IKZ} на реальный номер закупки
                    size_t pos = template_url.find("{IKZ}");
                    if (pos != std::string::npos) {
                        template_url.replace(pos, 5, clean_ikz);
                    }
                    url = template_url;
                } else {
                    // Используем шаблон поиска из настроек
                    Settings settings = dbManager->getSettings();
                    std::string template_url = settings.zakupki_url_search_template;
                    
                    // Очищаем номер договора от невидимых символов
                    std::string clean_number = selectedContract.number;
                    while (!clean_number.empty()) {
                        unsigned char c0 = static_cast<unsigned char>(clean_number[0]);
                        if (clean_number.size() >= 3 && c0 == 0xEF &&
                            static_cast<unsigned char>(clean_number[1]) == 0xBB &&
                            static_cast<unsigned char>(clean_number[2]) == 0xBF) {
                            clean_number.erase(0, 3);
                        } else if (clean_number.size() >= 2 && c0 == 0xC2 &&
                                   static_cast<unsigned char>(clean_number[1]) == 0xA0) {
                            clean_number.erase(0, 2);
                        } else if (clean_number.size() >= 3 && c0 == 0xE2 &&
                                   static_cast<unsigned char>(clean_number[1]) == 0x80 &&
                                   static_cast<unsigned char>(clean_number[2]) >= 0x80 &&
                                   static_cast<unsigned char>(clean_number[2]) <= 0x8F) {
                            clean_number.erase(0, 3);
                        } else if (clean_number.size() >= 3 && c0 == 0xE2 &&
                                   static_cast<unsigned char>(clean_number[1]) == 0x80 &&
                                   static_cast<unsigned char>(clean_number[2]) == 0xAF) {
                            clean_number.erase(0, 3);
                        } else if (clean_number.size() >= 3 && c0 == 0xE2 &&
                                   static_cast<unsigned char>(clean_number[1]) == 0x81 &&
                                   static_cast<unsigned char>(clean_number[2]) == 0x9F) {
                            clean_number.erase(0, 3);
                        } else if (std::isspace(c0)) {
                            clean_number.erase(0, 1);
                        } else {
                            break;
                        }
                    }
                    
                    // Заменяем {NUMBER} на реальный номер договора
                    size_t pos = template_url.find("{NUMBER}");
                    if (pos != std::string::npos) {
                        template_url.replace(pos, 8, clean_number);
                    }
                    url = template_url;
                }
                std::string command = "xdg-open \"" + url + "\"";
                system(command.c_str());
            }
            ImGui::EndDisabled();

            ImGui::EndChild();
            ImGui::SameLine();

            CustomWidgets::VerticalSplitter("v_splitter", &editor_width);

            ImGui::SameLine();

            ImGui::BeginChild("PaymentDetails", ImVec2(0, 0), true);
            ImGui::Text("Расшифровки платежей:");
            if (ImGui::BeginTable(
                    "payment_details_table", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
                ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_DefaultSort, 0, 0);
                ImGui::TableSetupColumn("Номер док.", 0, 0, 1);
                ImGui::TableSetupColumn("Сумма", 0, 0, 2);
                ImGui::TableSetupColumn("КОСГУ", 0, 0, 3);
                ImGui::TableSetupColumn("Назначение", 0, 0, 4);
                ImGui::TableHeadersRow();

                if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                    if (sort_specs->SpecsDirty || m_sorted_payment_info.empty()) {
                        m_sorted_payment_info = payment_info;
                        SortPaymentInfo(sort_specs);
                        sort_specs->SpecsDirty = false;
                    }
                }

                for (const auto &info : m_sorted_payment_info) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", info.date.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", info.doc_number.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", info.amount);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", info.kosgu_code.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", info.description.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
        } else {
            ImGui::BeginChild("BottomPane", ImVec2(0, 0), true);
            ImGui::Text(
                "Выберите договор для редактирования или добавьте новый.");
            ImGui::EndChild();
        }
    }

    ImGui::End();
}

void ContractsView::UpdateFilteredContracts() {
    if (!dbManager)
        return;

    // 1. Fetch all payment info once
    auto all_payment_info = dbManager->getAllContractPaymentInfo();
    m_contract_details_map.clear();
    for (const auto &info : all_payment_info) {
        m_contract_details_map[info.contract_id].push_back(info);
    }

    // 2. Text filter pass
    std::vector<Contract> text_filtered_contracts;
    if (filterText[0] != '\0') {
        for (const auto &entry : contracts) {
            bool contract_match = false;
            if (strcasestr(entry.number.c_str(), filterText) != nullptr ||
                strcasestr(entry.date.c_str(), filterText) != nullptr ||
                strcasestr(entry.end_date.c_str(), filterText) != nullptr ||
                strcasestr(entry.procurement_code.c_str(), filterText) !=
                    nullptr ||
                strcasestr(entry.note.c_str(), filterText) != nullptr) {
                contract_match = true;
            }

            char amount_str[32];
            snprintf(amount_str, sizeof(amount_str), "%.2f",
                     entry.contract_amount);
            if (strcasestr(amount_str, filterText) != nullptr) {
                contract_match = true;
            }

            // Check counterparty name
            for (const auto &cp : counterpartiesForDropdown) {
                if (cp.id == entry.counterparty_id) {
                    if (strcasestr(cp.name.c_str(), filterText) != nullptr) {
                        contract_match = true;
                        break;
                    }
                }
            }

            bool payment_detail_match = false;
            auto it = m_contract_details_map.find(entry.id);
            if (it != m_contract_details_map.end()) {
                for (const auto &detail : it->second) {
                    if (strcasestr(detail.date.c_str(), filterText) !=
                            nullptr ||
                        strcasestr(detail.doc_number.c_str(), filterText) !=
                            nullptr ||
                        strcasestr(detail.description.c_str(), filterText) !=
                            nullptr ||
                        strcasestr(detail.kosgu_code.c_str(), filterText) !=
                            nullptr) {
                        payment_detail_match = true;
                        break;
                    }
                    char detail_amount_str[32];
                    snprintf(detail_amount_str, sizeof(detail_amount_str),
                             "%.2f", detail.amount);
                    if (strcasestr(detail_amount_str, filterText) != nullptr) {
                        payment_detail_match = true;
                        break;
                    }
                }
            }

            if (contract_match || payment_detail_match) {
                text_filtered_contracts.push_back(entry);
            }
        }
    } else {
        text_filtered_contracts = contracts;
    }

    // 3. Category filter pass
    m_filtered_contracts.clear();
    switch (contract_filter_index) {
    case 0: // Все
        m_filtered_contracts = text_filtered_contracts;
        break;
    case 1: // Для проверки
        for (const auto &contract : text_filtered_contracts) {
            if (contract.is_for_checking) {
                m_filtered_contracts.push_back(contract);
            }
        }
        break;
    case 2: // Усиленный контроль
        for (const auto &contract : text_filtered_contracts) {
            if (contract.is_for_special_control) {
                m_filtered_contracts.push_back(contract);
            }
        }
        break;
    case 3: // С примечанием
        for (const auto &contract : text_filtered_contracts) {
            if (!contract.note.empty()) {
                m_filtered_contracts.push_back(contract);
            }
        }
        break;
    case 4: // С ИКЗ
        for (const auto &contract : text_filtered_contracts) {
            if (!contract.procurement_code.empty()) {
                m_filtered_contracts.push_back(contract);
            }
        }
        break;
    case 5: // Подозрительное в платежах
        for (const auto &contract : text_filtered_contracts) {
            bool payment_has_suspicious = false;
            auto it = m_contract_details_map.find(contract.id);
            if (it != m_contract_details_map.end()) {
                for (const auto &detail : it->second) {
                    for (const auto &sw : suspiciousWordsForFilter) {
                        if (strcasestr(detail.description.c_str(),
                                       sw.word.c_str()) != nullptr) {
                            payment_has_suspicious = true;
                            break;
                        }
                    }
                    if (payment_has_suspicious)
                        break;
                }
            }
            if (payment_has_suspicious) {
                m_filtered_contracts.push_back(contract);
            }
        }
        break;
    case 6: // Ненайденные
        for (const auto &contract : text_filtered_contracts) {
            if (contract.is_for_checking && !contract.is_found) {
                m_filtered_contracts.push_back(contract);
            }
        }
        break;
    default:
        m_filtered_contracts = text_filtered_contracts;
        break;
    }
}

void ContractsView::SortPaymentInfo(const ImGuiTableSortSpecs* sort_specs) {
    std::sort(
        m_sorted_payment_info.begin(), m_sorted_payment_info.end(),
        [&](const ContractPaymentInfo &a, const ContractPaymentInfo &b) {
            for (int i = 0; i < sort_specs->SpecsCount; i++) {
                const ImGuiTableColumnSortSpecs *column_spec =
                    &sort_specs->Specs[i];
                int delta = 0;

                switch (column_spec->ColumnIndex) {
                case 0: // Дата
                    delta = a.date.compare(b.date);
                    break;
                case 1: // Номер док.
                    delta = a.doc_number.compare(b.doc_number);
                    break;
                case 2: // Сумма
                    delta = (a.amount < b.amount)   ? -1
                            : (a.amount > b.amount) ? 1
                                                    : 0;
                    break;
                case 3: // КОСГУ
                    delta = a.kosgu_code.compare(b.kosgu_code);
                    break;
                case 4: // Назначение
                    delta = a.description.compare(b.description);
                    break;
                default:
                    break;
                }

                if (delta != 0) {
                    return (column_spec->SortDirection ==
                            ImGuiSortDirection_Ascending)
                               ? (delta < 0)
                               : (delta > 0);
                }
            }
            return false;
        });
}
