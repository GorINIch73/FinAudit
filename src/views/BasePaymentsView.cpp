#include "BasePaymentsView.h"
#include "../CustomWidgets.h"
#include "../IconsFontAwesome6.h"
#include "../UIManager.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

BasePaymentsView::BasePaymentsView()
    : selectedDocIndex(-1),
      showEditModal(false),
      isAdding(false),
      isDirty(false),
      cancel_group_operation(false) {
    Title = "Документы Основания";
    memset(filterText, 0, sizeof(filterText));
    memset(counterpartyFilter, 0, sizeof(counterpartyFilter));
    memset(doc_search_buffer, 0, sizeof(doc_search_buffer));
    memset(groupKosguFilter, 0, sizeof(groupKosguFilter));
}

void BasePaymentsView::SetDatabaseManager(DatabaseManager *manager) {
    dbManager = manager;
}

void BasePaymentsView::SetPdfReporter(PdfReporter *reporter) {
    pdfReporter = reporter;
}

void BasePaymentsView::SetUIManager(UIManager *manager) { uiManager = manager; }

void BasePaymentsView::RefreshData() {
    if (dbManager) {
        documents = dbManager->getBasePaymentDocuments();
        UpdateFilteredDocuments();
    }
}

void BasePaymentsView::RefreshDropdownData() {
    if (dbManager) {
        counterpartiesForDropdown = dbManager->getCounterparties();
        contractsForDropdown = dbManager->getContracts();
        kosguForDropdown = dbManager->getKosguEntries();
        paymentsForDropdown = dbManager->getPayments();
    }
}

void BasePaymentsView::UpdateFilteredDocuments() {
    m_filtered_documents.clear();

    // Загружаем все расшифровки один раз для поиска по фильтру
    std::vector<BasePaymentDocumentDetail> allDetails;
    if (dbManager) {
        allDetails = dbManager->getAllBasePaymentDocumentDetails();
    }

    for (const auto &doc : documents) {
        bool matches_filter = true;

        // Текстовый фильтр (ищет по полям документа И по полям расшифровок)
        if (filterText[0] != '\0') {
            std::string search = filterText;
            std::transform(search.begin(), search.end(), search.begin(),
                           ::tolower);

            // Проверяем поля документа
            std::string doc_num = doc.number;
            std::transform(doc_num.begin(), doc_num.end(), doc_num.begin(),
                           ::tolower);
            std::string doc_name = doc.document_name;
            std::transform(doc_name.begin(), doc_name.end(), doc_name.begin(),
                           ::tolower);
            std::string doc_note = doc.note;
            std::transform(doc_note.begin(), doc_note.end(), doc_note.begin(),
                           ::tolower);

            // Имя контрагента уже текстовое поле документа
            std::string doc_counterparty_name = doc.counterparty_name;
            std::transform(doc_counterparty_name.begin(), doc_counterparty_name.end(),
                           doc_counterparty_name.begin(), ::tolower);

            bool doc_matches = (doc_num.find(search) != std::string::npos) ||
                               (doc_name.find(search) != std::string::npos) ||
                               (doc_note.find(search) != std::string::npos) ||
                               (doc_counterparty_name.find(search) != std::string::npos);

            // Проверяем номер, контрагента и назначение привязанного платежа
            bool payment_matches = false;
            if (doc.payment_id > 0) {
                for (const auto& pay : paymentsForDropdown) {
                    if (pay.id == doc.payment_id) {
                        std::string pay_num = pay.doc_number;
                        std::transform(pay_num.begin(), pay_num.end(), pay_num.begin(), ::tolower);
                        std::string pay_desc = pay.description;
                        std::transform(pay_desc.begin(), pay_desc.end(), pay_desc.begin(), ::tolower);
                        std::string pay_cp = pay.recipient;
                        std::transform(pay_cp.begin(), pay_cp.end(), pay_cp.begin(), ::tolower);

                        if (pay_num.find(search) != std::string::npos ||
                            pay_desc.find(search) != std::string::npos ||
                            pay_cp.find(search) != std::string::npos) {
                            payment_matches = true;
                        }
                        break;
                    }
                }
            }
            if (payment_matches) {
                doc_matches = true;
            }

            // Проверяем поля расшифровок этого документа
            bool detail_matches = false;
            for (const auto& detail : allDetails) {
                if (detail.document_id != doc.id) continue;

                std::string op_content = detail.operation_content;
                std::transform(op_content.begin(), op_content.end(), op_content.begin(),
                               ::tolower);
                std::string debit = detail.debit_account;
                std::transform(debit.begin(), debit.end(), debit.begin(),
                               ::tolower);
                std::string credit = detail.credit_account;
                std::transform(credit.begin(), credit.end(), credit.begin(),
                               ::tolower);
                std::string detail_note = detail.note;
                std::transform(detail_note.begin(), detail_note.end(), detail_note.begin(),
                               ::tolower);

                if (op_content.find(search) != std::string::npos ||
                    debit.find(search) != std::string::npos ||
                    credit.find(search) != std::string::npos ||
                    detail_note.find(search) != std::string::npos) {
                    detail_matches = true;
                    break;
                }
            }

            if (!doc_matches && !detail_matches) {
                matches_filter = false;
            }
        }

        // Фильтр по типу
        if (matches_filter) {
            switch (doc_filter_index) {
            case 1: // Для сверки
                if (!doc.is_for_checking)
                    matches_filter = false;
                break;
            case 2: // Сверенные
                if (!doc.is_checked)
                    matches_filter = false;
                break;
            case 3: // Не сверенные
                if (doc.is_checked)
                    matches_filter = false;
                break;
            }
        }

        if (matches_filter) {
            m_filtered_documents.push_back(doc);
        }
    }
}

void BasePaymentsView::SortDocuments(const ImGuiTableSortSpecs* sort_specs) {
    if (!sort_specs || sort_specs->SpecsCount == 0 || m_filtered_documents.empty()) return;

    const ImGuiTableColumnSortSpecs* specs = &sort_specs->Specs[0];
    try {
        std::sort(m_filtered_documents.begin(), m_filtered_documents.end(),
            [specs](const BasePaymentDocument& a, const BasePaymentDocument& b) {
                bool result = false;
                switch (specs->ColumnUserID) {
                    case 0: // ID
                        result = a.id < b.id; break;
                    case 1: // Дата
                        result = a.date < b.date; break;
                    case 2: // Номер
                        result = a.number < b.number; break;
                    case 3: // Наименование
                        result = a.document_name < b.document_name; break;
                    case 4: // Контрагент (по имени)
                        result = a.counterparty_name < b.counterparty_name; break;
                    case 5: // Сумма
                        result = a.total_amount < b.total_amount; break;
                    case 6: // Для сверки
                        result = a.is_for_checking < b.is_for_checking; break;
                    case 7: // ПП № (по payment_id)
                        result = a.payment_id < b.payment_id; break;
                    case 8: // ПП дата (по payment_id)
                        result = a.payment_id < b.payment_id; break;
                    case 9: // ПП сумма (по payment_id)
                        result = a.payment_id < b.payment_id; break;
                    case 10: // ПП назначение (по payment_id)
                        result = a.payment_id < b.payment_id; break;
                    case 11: // ПП контрагент (по payment_id)
                        result = a.payment_id < b.payment_id; break;
                    default: break;
                }
                return specs->SortDirection == ImGuiSortDirection_Ascending ? result : !result;
            });
    } catch (const std::exception& e) {
        std::cerr << "Sort error: " << e.what() << std::endl;
    }
}

void BasePaymentsView::SaveChanges() {
    if (dbManager) {
        // Проверяем, есть ли несохранённый документ (id == -1)
        if (selectedDoc.id == -1) {
            // Новый документ ещё не сохранён - добавляем его
            dbManager->addBasePaymentDocument(selectedDoc);
            // Добавляем новую запись в локальные массивы
            documents.push_back(selectedDoc);
            m_filtered_documents.push_back(selectedDoc);
        } else if (isDirty) {
            // Существующий документ изменён - обновляем
            dbManager->updateBasePaymentDocument(selectedDoc);
            // Обновляем локальные массивы напрямую, чтобы не сбрасывать сортировку
            for (auto& doc : documents) {
                if (doc.id == selectedDoc.id) { doc = selectedDoc; break; }
            }
            for (auto& doc : m_filtered_documents) {
                if (doc.id == selectedDoc.id) { doc = selectedDoc; break; }
            }
        }
        isDirty = false;

        // Сохраняем изменения в расшифровке
        if (isDetailDirty && selectedDetail.id != -1) {
            dbManager->updateBasePaymentDocumentDetail(selectedDetail);
            isDetailDirty = false;
        }
    }
}

void BasePaymentsView::OnDeactivate() {
    SaveChanges();
    IsVisible = false;
}
void BasePaymentsView::ForceSave() {
    SaveChanges();
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
BasePaymentsView::GetDataAsStrings() {
    std::vector<std::string> headers = {
        "ID",      "Дата",  "Номер",      "Наименование", "Контрагент",
        "Договор", "Сумма", "Для сверки", "Сверено"};
    std::vector<std::vector<std::string>> data;
    for (const auto &doc : documents) {
        std::string contract_num;
        for (const auto &c : contractsForDropdown) {
            if (c.id == doc.contract_id) {
                contract_num = c.number;
                break;
            }
        }
        data.push_back(
            {std::to_string(doc.id), doc.date, doc.number, doc.document_name,
             doc.counterparty_name, contract_num, std::to_string(doc.total_amount),
             doc.is_for_checking ? "Да" : "", doc.is_checked ? "Да" : ""});
    }
    return {headers, data};
}

void BasePaymentsView::Render() {
    if (!IsVisible)
        return;

    // Обновляем данные только если они помечены как "грязные"
    if (m_dataDirty) {
        RefreshData();
        RefreshDropdownData();
        m_dataDirty = false;
    }

    ImGui::Begin(Title.c_str(), &IsVisible);

    // --- Панель фильтров ---
    ImGui::Text("Фильтры:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200.0f);
    if (ImGui::InputText("##DocSearch", doc_search_buffer,
                         sizeof(doc_search_buffer))) {
        strncpy(filterText, doc_search_buffer, sizeof(filterText) - 1);
        UpdateFilteredDocuments();
    }
    ImGui::SameLine();
    if (doc_search_buffer[0] != '\0') {
        if (ImGui::Button(ICON_FA_XMARK "##ClearFilter")) {
            memset(doc_search_buffer, 0, sizeof(doc_search_buffer));
            memset(filterText, 0, sizeof(filterText));
            UpdateFilteredDocuments();
        }
    }
    ImGui::SameLine();
    const char *filter_items[] = {"Все", "Для сверки", "Сверенные",
                                  "Не сверенные"};
    if (ImGui::Combo("##DocFilter", &doc_filter_index, filter_items,
                     IM_ARRAYSIZE(filter_items))) {
        UpdateFilteredDocuments();
    }

    // --- Групповые операции ---
    if (ImGui::CollapsingHeader("Групповые операции")) {
        if (ImGui::Button("Добавить КОСГУ")) {
            if (!m_filtered_documents.empty() && current_operation == NONE) {
                groupKosguId = -1;
                memset(groupKosguFilter, 0, sizeof(groupKosguFilter));
                show_group_operation_confirmation_popup = true;
                on_group_operation_confirm = [&]() {
                    items_to_process = m_filtered_documents;
                    processed_items = 0;
                    current_operation = ADD_KOSGU;
                };
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Очистить 'Для сверки'")) {
            if (!m_filtered_documents.empty() && current_operation == NONE) {
                items_to_process = m_filtered_documents;
                processed_items = 0;
                current_operation = UNSET_FOR_CHECKING;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Отметить 'Для сверки'")) {
            if (!m_filtered_documents.empty() && current_operation == NONE) {
                items_to_process = m_filtered_documents;
                processed_items = 0;
                current_operation = SET_FOR_CHECKING;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Удалить расшифровки")) {
            if (!m_filtered_documents.empty() && current_operation == NONE) {
                items_to_process = m_filtered_documents;
                processed_items = 0;
                current_operation = DELETE_DETAILS;
            }
        }

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_LINK " Автоподбор платежей")) {
            if (!m_filtered_documents.empty() && current_operation == NONE) {
                group_auto_match_min_score = 50;
                group_auto_match_docs = m_filtered_documents;
                show_group_operation_confirmation_popup = true;
                on_group_operation_confirm = [&]() {
                    items_to_process = m_filtered_documents;
                    group_auto_match_total = 0;
                    group_auto_match_linked = 0;
                    group_auto_match_no_match = 0;
                    group_auto_match_chunk_start = 0;
                    group_auto_match_log.clear();
                    show_auto_match_popup = true;
                    current_operation = AUTO_MATCH_PAYMENTS;
                };
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LINK_SLASH " Очистить связь с ПП")) {
            if (!m_filtered_documents.empty() && current_operation == NONE) {
                items_to_process = m_filtered_documents;
                processed_items = 0;
                current_operation = CLEAR_PAYMENT_LINK;
            }
        }
    }

    // --- Таблица документов (верхняя часть) ---
    ImGui::Separator();

    // Popup подтверждения групповой операции
    if (show_group_operation_confirmation_popup) {
        ImGui::OpenPopup("Подтверждение групповой операции");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Подтверждение групповой операции", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Выполнить операцию для %zu документов?",
                        items_to_process.empty() ? group_auto_match_docs.size() : items_to_process.size());

            // Для автоподбора — показать настройку мин. процента и чекбокс контрагента
            if (!group_auto_match_docs.empty()) {
                ImGui::Separator();
                ImGui::Text("Минимальный процент совпадения:");
                ImGui::SetNextItemWidth(120);
                ImGui::SliderInt("##MinScore", &group_auto_match_min_score, 10, 100, "%d%%");
                ImGui::Checkbox("Обязательное совпадение по контрагенту",
                                &group_auto_match_require_counterparty);
            }

            ImGui::Separator();
            if (ImGui::Button("Да")) {
                if (on_group_operation_confirm) {
                    on_group_operation_confirm();
                }
                show_group_operation_confirmation_popup = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Отмена")) {
                show_group_operation_confirmation_popup = false;
            }
            ImGui::EndPopup();
        }
    }

    ImGui::Text("Документы основания (%zu):", m_filtered_documents.size());

    ImGui::BeginChild("DocumentsListRegion", ImVec2(0, split_pos), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginTable("basePaymentsTable", 12,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 40.0f, 0);
        ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_WidthFixed, 90.0f, 1);
        ImGui::TableSetupColumn("Номер", 0, 0, 2);
        ImGui::TableSetupColumn("Наименование", 0, 0, 3);
        ImGui::TableSetupColumn("Контрагент", 0, 0, 4);
        ImGui::TableSetupColumn("ПП №", 0, 0, 7);
        ImGui::TableSetupColumn("ПП дата", ImGuiTableColumnFlags_WidthFixed, 80.0f, 8);
        ImGui::TableSetupColumn("ПП контрагент", 0, 0, 11);
        ImGui::TableSetupColumn("ПП сумма", ImGuiTableColumnFlags_WidthFixed, 110.0f, 9);
        ImGui::TableSetupColumn("ПП назначение", 0, 0, 10);
        ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_WidthFixed, 120.0f, 5);
        ImGui::TableSetupColumn("Для сверки", ImGuiTableColumnFlags_WidthFixed, 80.0f, 6);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty) {
                SortDocuments(sort_specs);
                sort_specs->SpecsDirty = false;
            }
        }

        for (int row = 0; row < m_filtered_documents.size(); row++) {
            const auto &doc = m_filtered_documents[row];
            ImGui::PushID(doc.id); // Уникальный ID для каждой строки
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", doc.id);
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc.date.c_str());
            ImGui::TableNextColumn();
            if (ImGui::Selectable(doc.number.c_str(),
                                  row == selectedDocIndex,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                SaveChanges();
                selectedDocIndex = row;
                selectedDoc = doc;
                originalDoc = doc;
                docDetails =
                    dbManager
                        ? dbManager->getBasePaymentDocumentDetails(doc.id)
                        : std::vector<BasePaymentDocumentDetail>();
            }
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc.document_name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc.counterparty_name.c_str());
            ImGui::TableNextColumn();
            // ПП №
            if (doc.payment_id > 0) {
                bool found = false;
                for (const auto &pay : paymentsForDropdown) {
                    if (pay.id == doc.payment_id) {
                        ImGui::Text("%s", pay.doc_number.c_str());
                        found = true;
                        break;
                    }
                }
                if (!found) ImGui::Text("%d", doc.payment_id);
            }
            ImGui::TableNextColumn();
            // ПП дата
            if (doc.payment_id > 0) {
                for (const auto &pay : paymentsForDropdown) {
                    if (pay.id == doc.payment_id) {
                        ImGui::Text("%s", pay.date.c_str());
                        break;
                    }
                }
            }
            ImGui::TableNextColumn();
            // ПП контрагент
            if (doc.payment_id > 0) {
                for (const auto &pay : paymentsForDropdown) {
                    if (pay.id == doc.payment_id) {
                        ImGui::Text("%s", pay.recipient.c_str());
                        break;
                    }
                }
            }
            ImGui::TableNextColumn();
            // ПП сумма
            if (doc.payment_id > 0) {
                for (const auto &pay : paymentsForDropdown) {
                    if (pay.id == doc.payment_id) {
                        ImGui::Text("%.2f", pay.amount);
                        break;
                    }
                }
            }
            ImGui::TableNextColumn();
            // ПП назначение
            if (doc.payment_id > 0) {
                for (const auto &pay : paymentsForDropdown) {
                    if (pay.id == doc.payment_id) {
                        ImGui::Text("%s", pay.description.c_str());
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                            ImGui::SetTooltip("%s", pay.description.c_str());
                        }
                        break;
                    }
                }
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", doc.total_amount);
            ImGui::TableNextColumn();
            ImGui::Text(doc.is_for_checking ? ICON_FA_CHECK : "");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild(); // DocumentsListRegion

    // Сплиттер между списком и редактором
    CustomWidgets::HorizontalSplitter("h_splitter_bpd", &split_pos, 100.0f);

    // --- Редактор и расшифровки (нижняя часть) ---
    if (selectedDocIndex >= 0 &&
        selectedDocIndex < m_filtered_documents.size()) {
        ImGui::BeginChild("EditorRegion", ImVec2(0, 0), true);

        // Левая часть - редактор
        ImGui::BeginChild("DocEditor", ImVec2(editor_width, 0), true);
        ImGui::Text("Редактор документа:");

        if (CustomWidgets::InputText("Дата", &selectedDoc.date))
            isDirty = true;
        if (CustomWidgets::InputText("Номер", &selectedDoc.number))
            isDirty = true;
        if (CustomWidgets::InputText("Наименование",
                                     &selectedDoc.document_name))
            isDirty = true;

        if (CustomWidgets::InputText("Контрагент", &selectedDoc.counterparty_name))
            isDirty = true;

        // Договор
        {
            std::vector<CustomWidgets::ComboItem> items;
            items.push_back({-1, "Не выбрано"});
            for (const auto &c : contractsForDropdown) {
                items.push_back({c.id, c.number + " " + c.date});
            }
            char contractFilter[128] = {0};
            if (CustomWidgets::ComboWithFilter(
                    "Договор", selectedDoc.contract_id, items, contractFilter,
                    sizeof(contractFilter), 0)) {
                isDirty = true;
            }
        }

        if (CustomWidgets::InputTextMultilineWithWrap(
                "Примечание", &selectedDoc.note,
                ImVec2(-1, ImGui::GetTextLineHeight() * 3))) {
            isDirty = true;
        }

        if (ImGui::Checkbox("Для сверки", &selectedDoc.is_for_checking))
            isDirty = true;
        if (ImGui::Checkbox("Сверено", &selectedDoc.is_checked))
            isDirty = true;

        ImGui::Separator();
        if (ImGui::Button(ICON_FA_LINK " Автоподбор платежа из банка")) {
            AutoMatchPayment();
        }
        if (selectedDoc.payment_id > 0) {
            ImGui::SameLine();
            ImGui::Text("Привязан к платежу ID: %d", selectedDoc.payment_id);
        }

        ImGui::EndChild();

        ImGui::SameLine();
        CustomWidgets::VerticalSplitter("v_splitter_bpd", &editor_width);
        ImGui::SameLine();

        // Правая часть - расшифровки
        ImGui::BeginChild("DetailsView", ImVec2(0, 0), true);
        ImGui::Text("Расшифровки документа (всего: %zu):", docDetails.size());

        if (ImGui::Button(ICON_FA_PLUS " Добавить расшифровку")) {
            showDetailAddPopup = true;
        }

        if (!docDetails.empty()) {
            if (ImGui::BeginTable("detailsTable", 5,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_ScrollX |
                                      ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Содержание");
                ImGui::TableSetupColumn("Дебет");
                ImGui::TableSetupColumn("Кредит");
                ImGui::TableSetupColumn("КОСГУ");
                ImGui::TableSetupColumn(
                    "Сумма", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < docDetails.size(); i++) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", docDetails[i].operation_content.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", docDetails[i].debit_account.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", docDetails[i].credit_account.c_str());
                    ImGui::TableNextColumn();
                    for (const auto &k : kosguForDropdown) {
                        if (k.id == docDetails[i].kosgu_id) {
                            ImGui::Text("%s", k.code.c_str());
                            break;
                        }
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", docDetails[i].amount);
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::Text("Нет расшифровок для этого документа.");
        }

        ImGui::EndChild(); // DetailsView
        ImGui::EndChild(); // EditorRegion
    }

    ImGui::End();

    // Popup с результатами автоподбора платежа
    if (show_payment_match_popup) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(850, 520), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(600, 300), ImVec2(1400, 900));
        
        bool open = true;
        if (ImGui::Begin("Результаты автоподбора платежа", &open,
                         ImGuiWindowFlags_NoCollapse)) {
            ImGui::Checkbox("Обязательное совпадение по контрагенту",
                            &group_auto_match_require_counterparty);
            ImGui::SameLine();
            if (ImGui::Button("Пересчитать")) {
                payment_matches = dbManager->findMatchingPayments(selectedDoc,
                    group_auto_match_require_counterparty);
                selected_payment_match_index = payment_matches.empty() ? -1 : 0;
            }
            ImGui::Separator();

            if (!payment_matches.empty()) {
                ImGui::Text("Найдено %zu совпадений:", payment_matches.size());

                float table_height = ImGui::GetContentRegionAvail().y - 130.0f;
                if (table_height < 60.0f) table_height = 60.0f;
                
                if (ImGui::BeginChild("PaymentMatchesTable", ImVec2(0, table_height), true,
                                      ImGuiWindowFlags_HorizontalScrollbar)) {
                    if (ImGui::BeginTable("paymentMatchesTable", 6,
                                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupColumn("Дата");
                    ImGui::TableSetupColumn("Номер");
                    ImGui::TableSetupColumn("Сумма", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Совпадение", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Причины");
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(payment_matches.size()));
                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            const auto& match = payment_matches[i];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", match.payment_id);
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", match.date.c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", match.doc_number.c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%.2f", match.amount);
                            ImGui::TableNextColumn();
                            // Цветовая индикация совпадения
                            ImColor color = match.match_score >= 75 ? IM_COL32(0, 200, 0, 255) :
                                           match.match_score >= 50 ? IM_COL32(200, 200, 0, 255) :
                                           IM_COL32(200, 100, 0, 255);
                            ImGui::TextColored(color, "%d%%", match.match_score);
                            ImGui::TableNextColumn();
                            ImGui::TextWrapped("%s", match.match_reasons.c_str());
                        }
                    }
                    ImGui::EndTable();
                    }
                }
                ImGui::EndChild();

                ImGui::Separator();
                ImGui::Text("Выберите платёж для привязки:");
                
                if (ImGui::BeginCombo("##PaymentSelect", 
                    selected_payment_match_index >= 0 && selected_payment_match_index < payment_matches.size() 
                        ? ("ID: " + std::to_string(payment_matches[selected_payment_match_index].payment_id) + 
                           " | " + payment_matches[selected_payment_match_index].date + 
                           " | " + payment_matches[selected_payment_match_index].doc_number).c_str()
                        : "Выберите платёж...")) {
                    for (int i = 0; i < payment_matches.size(); i++) {
                        char label[256];
                        snprintf(label, sizeof(label), "ID: %d | %s | %s | %.2f | %d%%",
                                 payment_matches[i].payment_id, payment_matches[i].date.c_str(),
                                 payment_matches[i].doc_number.c_str(), payment_matches[i].amount,
                                 payment_matches[i].match_score);
                        bool is_selected = (i == selected_payment_match_index);
                        if (ImGui::Selectable(label, is_selected)) {
                            selected_payment_match_index = i;
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Separator();
                if (ImGui::Button("Привязать")) {
                    if (selected_payment_match_index >= 0 && selected_payment_match_index < payment_matches.size()) {
                        int payment_id = payment_matches[selected_payment_match_index].payment_id;
                        if (dbManager && dbManager->linkBasePaymentDocumentToPayment(selectedDoc.id, payment_id)) {
                            selectedDoc.payment_id = payment_id;
                            // Обновляем локальный массив
                            for (auto& doc : documents) {
                                if (doc.id == selectedDoc.id) { doc.payment_id = payment_id; break; }
                            }
                            for (auto& doc : m_filtered_documents) {
                                if (doc.id == selectedDoc.id) { doc.payment_id = payment_id; break; }
                            }
                            show_payment_match_popup = false;
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Отмена")) {
                    show_payment_match_popup = false;
                }
            } else {
                ImGui::Text("Совпадений не найдено.");
                ImGui::Text("Попробуйте снять галку 'Обязательное совпадение по контрагенту'.");
                ImGui::Separator();
                if (ImGui::Button("Закрыть")) {
                    show_payment_match_popup = false;
                }
            }

            if (!open) {
                show_payment_match_popup = false;
            }

            ImGui::End();
        } else {
            show_payment_match_popup = false;
        }
    }

    // Popup группового автоподбора платежей
    if (show_auto_match_popup) {
        ImGui::OpenPopup("Групповой автоподбор платежей");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(450, 300), ImVec2(900, 600));

        bool open = true;
        if (ImGui::Begin("Групповой автоподбор платежей", &open,
                         ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("Документов для обработки: %zu", group_auto_match_docs.size());
            ImGui::Text("Минимальный процент совпадения: %d%%", group_auto_match_min_score);
            ImGui::Separator();

            // Прогресс-бар
            float progress = group_auto_match_docs.empty() ? 0.0f :
                (float)group_auto_match_total / (float)group_auto_match_docs.size();
            ImGui::ProgressBar(progress, ImVec2(-1, 20));
            ImGui::Separator();

            ImGui::Text("Привязано: %d | Не найдено: %d | Всего: %d",
                        group_auto_match_linked, group_auto_match_no_match,
                        group_auto_match_total);

            // Лог
            ImGui::Separator();
            ImGui::Text("Журнал:");
            if (ImGui::BeginChild("AutoMatchLog", ImVec2(0, 120), true)) {
                for (const auto& line : group_auto_match_log) {
                    ImGui::TextUnformatted(line.c_str());
                }
                if (group_auto_match_chunk_start >= group_auto_match_docs.size() &&
                    group_auto_match_total > 0) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();

            ImGui::Separator();
            if (group_auto_match_chunk_start >= group_auto_match_docs.size() &&
                group_auto_match_total > 0) {
                if (ImGui::Button("Закрыть")) {
                    show_auto_match_popup = false;
                    m_dataDirty = true;
                }
            }

            if (!open) {
                show_auto_match_popup = false;
            }

            ImGui::End();
        } else {
            show_auto_match_popup = false;
        }
    }

    // Обработка групповых операций
    if (current_operation != NONE) {
        if (current_operation == AUTO_MATCH_PAYMENTS) {
            ProcessAutoMatchPayments();
        } else {
            ProcessGroupOperation();
        }
    }
}

void BasePaymentsView::AutoMatchPayment() {
    if (!dbManager || selectedDoc.id == -1) return;

    payment_matches = dbManager->findMatchingPayments(selectedDoc, group_auto_match_require_counterparty);
    selected_payment_match_index = payment_matches.empty() ? -1 : 0;
    show_payment_match_popup = true;
}

void BasePaymentsView::ProcessGroupOperation() {
    if (!dbManager || current_operation == NONE || items_to_process.empty()) {
        current_operation = NONE;
        return;
    }

    const int items_per_frame = 20;
    int processed_in_frame = 0;

    while (processed_items < items_to_process.size() &&
           processed_in_frame < items_per_frame) {
        const auto &doc = items_to_process[processed_items];

        switch (current_operation) {
        case SET_FOR_CHECKING: {
            BasePaymentDocument updated = doc;
            updated.is_for_checking = true;
            dbManager->updateBasePaymentDocument(updated);
            break;
        }
        case UNSET_FOR_CHECKING: {
            BasePaymentDocument updated = doc;
            updated.is_for_checking = false;
            dbManager->updateBasePaymentDocument(updated);
            break;
        }
        case SET_CHECKED: {
            BasePaymentDocument updated = doc;
            updated.is_checked = true;
            dbManager->updateBasePaymentDocument(updated);
            break;
        }
        case UNSET_CHECKED: {
            BasePaymentDocument updated = doc;
            updated.is_checked = false;
            dbManager->updateBasePaymentDocument(updated);
            break;
        }
        case DELETE_DETAILS: {
            dbManager->deleteAllBasePaymentDocumentDetails(doc.id);
            break;
        }
        case CLEAR_PAYMENT_LINK: {
            BasePaymentDocument updated = doc;
            updated.payment_id = -1;
            dbManager->updateBasePaymentDocument(updated);
            for (auto& d : documents) {
                if (d.id == doc.id) { d.payment_id = -1; break; }
            }
            for (auto& d : m_filtered_documents) {
                if (d.id == doc.id) { d.payment_id = -1; break; }
            }
            break;
        }
        default:
            break;
        }

        processed_items++;
        processed_in_frame++;
    }

    if (processed_items >= items_to_process.size()) {
        current_operation = NONE;
        processed_items = 0;
        items_to_process.clear();
        m_dataDirty = true;
        RefreshData();
    }
}

void BasePaymentsView::ProcessAutoMatchPayments() {
    if (!dbManager || current_operation != AUTO_MATCH_PAYMENTS || group_auto_match_docs.empty()) {
        if (current_operation == AUTO_MATCH_PAYMENTS) {
            current_operation = NONE;
        }
        return;
    }

    const int chunk_size = 5;
    if (group_auto_match_chunk_start < group_auto_match_docs.size()) {
        int chunk_end = std::min(group_auto_match_chunk_start + chunk_size,
                                 (int)group_auto_match_docs.size());
        for (int i = group_auto_match_chunk_start; i < chunk_end; i++) {
            auto& doc = group_auto_match_docs[i];
            auto matches = dbManager->findMatchingPayments(doc, group_auto_match_require_counterparty);

            group_auto_match_total++;

            int best_payment_id = -1;
            int best_score = 0;
            for (const auto& m : matches) {
                if (m.match_score >= group_auto_match_min_score && m.match_score > best_score) {
                    best_payment_id = m.payment_id;
                    best_score = m.match_score;
                }
            }

            if (best_payment_id != -1) {
                dbManager->linkBasePaymentDocumentToPayment(doc.id, best_payment_id);
                for (auto& d : documents) {
                    if (d.id == doc.id) { d.payment_id = best_payment_id; break; }
                }
                for (auto& d : m_filtered_documents) {
                    if (d.id == doc.id) { d.payment_id = best_payment_id; break; }
                }
                group_auto_match_linked++;
                char buf[256];
                snprintf(buf, sizeof(buf), "[%d/%d] ДО #%d → ПП #%d (%d%%)",
                         group_auto_match_total, (int)group_auto_match_docs.size(),
                         doc.id, best_payment_id, best_score);
                group_auto_match_log.push_back(buf);
            } else {
                group_auto_match_no_match++;
                char buf[256];
                snprintf(buf, sizeof(buf), "[%d/%d] ДО #%d — не найдено совпадений",
                         group_auto_match_total, (int)group_auto_match_docs.size(),
                         doc.id);
                group_auto_match_log.push_back(buf);
            }
        }
        group_auto_match_chunk_start = chunk_end;
    }

    if (group_auto_match_chunk_start >= group_auto_match_docs.size()) {
        current_operation = NONE;
        m_dataDirty = true;
        RefreshData();
    }
}
