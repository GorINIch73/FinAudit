#include "KosguView.h"
#include "../CustomWidgets.h"
#include "../IconsFontAwesome6.h"
#include "../UIManager.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>

KosguView::KosguView()
    : selectedKosguIndex(-1),
      showEditModal(false),
      isAdding(false),
      isDirty(false),
      scroll_to_item_index(-1) {
    Title = "Справочник КОСГУ";
    memset(filterText, 0, sizeof(filterText));
}

void KosguView::SetUIManager(UIManager *manager) { uiManager = manager; }

void KosguView::SetDatabaseManager(DatabaseManager *manager) {
    dbManager = manager;
}

void KosguView::SetPdfReporter(PdfReporter *reporter) {
    pdfReporter = reporter;
}

void KosguView::RefreshData() {
    if (dbManager) {
        kosguEntries = dbManager->getKosguEntries();
        suspiciousWordsForFilter = dbManager->getSuspiciousWords();
        selectedKosguIndex = -1;
        UpdateFilteredKosgu();
    }
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
KosguView::GetDataAsStrings() {
    std::vector<std::string> headers = {"ID", "Код", "Наименование",
                                        "Примечание", "Сумма"};
    std::vector<std::vector<std::string>> rows;
    for (const auto &entry : m_filtered_kosgu_entries) {
        rows.push_back({std::to_string(entry.id), entry.code, entry.name,
                        entry.note, std::to_string(entry.total_amount)});
    }
    return {headers, rows};
}

void KosguView::OnDeactivate() { SaveChanges(); }
void KosguView::ForceSave() { SaveChanges(); }

void KosguView::SaveChanges() {
    // Сравниваем поля редактора с оригиналом
    bool hasChanges = (selectedKosgu.id != -1) && (
        selectedKosgu.code != originalKosgu.code ||
        selectedKosgu.name != originalKosgu.name ||
        selectedKosgu.note != originalKosgu.note
    );

    if (!hasChanges) {
        return;
    }

    if (dbManager && selectedKosgu.id != -1) {
        dbManager->updateKosguEntry(selectedKosgu);
        // Обновляем из БД и применяем сортировку
        kosguEntries = dbManager->getKosguEntries();
        m_filtered_kosgu_entries = kosguEntries;
        UpdateFilteredKosgu();
        ApplyStoredSorting();
        // Находим обновлённую запись
        for (int i = 0; i < (int)m_filtered_kosgu_entries.size(); i++) {
            if (m_filtered_kosgu_entries[i].id == selectedKosgu.id) {
                selectedKosgu = m_filtered_kosgu_entries[i];
                break;
            }
        }
        originalKosgu = selectedKosgu;
    }

    isDirty = false;
}

void KosguView::UpdateFilteredKosgu() {
    if (!dbManager)
        return;

    // 1. Fetch all payment info once
    auto all_payment_info = dbManager->getAllKosguPaymentInfo();
    std::map<int, std::vector<KosguPaymentDetailInfo>> details_by_kosgu;
    for (const auto &info : all_payment_info) {
        details_by_kosgu[info.kosgu_id].push_back(info);
    }

    // 2. Text filter pass
    std::vector<Kosgu> text_filtered_entries;
    if (filterText[0] != '\0') {
        for (const auto &entry : kosguEntries) {
            bool kosgu_match = false;
            if (strcasestr(entry.code.c_str(), filterText) != nullptr ||
                strcasestr(entry.name.c_str(), filterText) != nullptr) {
                kosgu_match = true;
            }

            float filtered_amount = 0.0f;
            bool payment_match = false;
            auto it = details_by_kosgu.find(entry.id);
            if (it != details_by_kosgu.end()) {
                for (const auto &detail : it->second) {
                    bool detail_match = false;
                    if (strcasestr(detail.date.c_str(), filterText) !=
                            nullptr ||
                        strcasestr(detail.doc_number.c_str(), filterText) !=
                            nullptr ||
                        strcasestr(detail.counterparty_name.c_str(), filterText) !=
                            nullptr ||
                        strcasestr(detail.description.c_str(), filterText) !=
                            nullptr) {
                        detail_match = true;
                    }
                    char amount_str[32];
                    snprintf(amount_str, sizeof(amount_str), "%.2f",
                             detail.amount);
                    if (strcasestr(amount_str, filterText) != nullptr) {
                        detail_match = true;
                    }

                    if (detail_match) {
                        filtered_amount += detail.amount;
                        payment_match = true;
                    }
                }
            }

            if (kosgu_match || payment_match) {
                Kosgu filtered_entry = entry;
                filtered_entry.total_amount =
                    kosgu_match ? entry.total_amount : filtered_amount;
                text_filtered_entries.push_back(filtered_entry);
            }
        }
    } else {
        text_filtered_entries = kosguEntries;
    }

    // 3. Dropdown filter pass
    m_filtered_kosgu_entries.clear();
    if (m_filter_index == 0) { // "Все"
        m_filtered_kosgu_entries = text_filtered_entries;
    } else if (m_filter_index == 3) { // "Подозрительные слова"
        if (!suspiciousWordsForFilter.empty()) {
            for (const auto &entry : text_filtered_entries) {
                bool suspicious_found = false;
                auto it = details_by_kosgu.find(entry.id);
                if (it != details_by_kosgu.end()) {
                    for (const auto &detail : it->second) {
                        for (const auto &sw : suspiciousWordsForFilter) {
                            if (strcasestr(detail.description.c_str(),
                                           sw.word.c_str()) != nullptr) {
                                suspicious_found = true;
                                break;
                            }
                        }
                        if (suspicious_found)
                            break;
                    }
                }
                if (suspicious_found) {
                    m_filtered_kosgu_entries.push_back(entry);
                }
            }
        }
    } else { // "С платежами" или "Без платежей"
        for (const auto &entry : text_filtered_entries) {
            bool has_payments = (entry.total_amount > 0.001);
            if (m_filter_index == 1 && has_payments) {
                m_filtered_kosgu_entries.push_back(entry);
            } else if (m_filter_index == 2 && !has_payments) {
                m_filtered_kosgu_entries.push_back(entry);
            }
        }
    }
}

// Вспомогательная функция для сортировки
static void SortKosgu(std::vector<Kosgu> &kosguEntries,
                      const ImGuiTableSortSpecs *sort_specs) {
    std::sort(kosguEntries.begin(), kosguEntries.end(),
              [&](const Kosgu &a, const Kosgu &b) {
                  for (int i = 0; i < sort_specs->SpecsCount; i++) {
                      const ImGuiTableColumnSortSpecs *column_spec =
                          &sort_specs->Specs[i];
                      int delta = 0;
                      switch (column_spec->ColumnIndex) {
                      case 0:
                          delta = (a.id < b.id) ? -1 : (a.id > b.id) ? 1 : 0;
                          break;
                      case 1:
                          delta = a.code.compare(b.code);
                          break;
                      case 2:
                          delta = a.name.compare(b.name);
                          break;
                      case 3:
                          delta = (a.total_amount < b.total_amount)   ? -1
                                  : (a.total_amount > b.total_amount) ? 1
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

static std::string ConvertDateForSort(const std::string& date) {
    if (date.length() == 10 && date[2] == '.' && date[5] == '.') {
        return date.substr(6, 4) + "." + date.substr(3, 2) + "." + date.substr(0, 2);
    }
    return date;
}

// Вспомогательная функция для сортировки
static void SortPaymentInfo(std::vector<ContractPaymentInfo> &paymentInfo,
                      const ImGuiTableSortSpecs *sort_specs) {
    std::sort(paymentInfo.begin(), paymentInfo.end(),
              [&](const ContractPaymentInfo &a, const ContractPaymentInfo &b) {
                  for (int i = 0; i < sort_specs->SpecsCount; i++) {
                      const ImGuiTableColumnSortSpecs *column_spec =
                          &sort_specs->Specs[i];
                      int delta = 0;
                      switch (column_spec->ColumnIndex) {
                      case 0:
                          delta = ConvertDateForSort(a.date).compare(ConvertDateForSort(b.date));
                          break;
                      case 1:
                          delta = a.doc_number.compare(b.doc_number);
                          break;
                      case 2:
                          delta = (a.amount < b.amount)   ? -1
                                  : (a.amount > b.amount) ? 1
                                                                      : 0;
                          break;
                      case 3:
                          delta = a.counterparty_name.compare(b.counterparty_name);
                          break;
                      case 4:
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


void KosguView::Render() {
    if (!IsVisible) {
        if (isDirty) {
            SaveChanges();
        }
        return;
    }

    if (ImGui::Begin(GetTitle(), &IsVisible)) {

        if (dbManager && kosguEntries.empty()) {
            RefreshData();
        }

        // Панель управления
        if (ImGui::Button(ICON_FA_PLUS " Добавить")) {
            SaveChanges();
            // Генерируем временный уникальный код для новой записи
            auto t = std::time(nullptr);
            std::string temp_code = "NEW_" + std::to_string(t);
            Kosgu newKosgu{-1, temp_code, "", ""};
            int new_id = -1;
            if (dbManager) {
                dbManager->addKosguEntry(newKosgu);
                // Реальный ID записан в newKosgu.id внутри addKosguEntry
                new_id = newKosgu.id;
                kosguEntries = dbManager->getKosguEntries();
                m_filtered_kosgu_entries = kosguEntries;
                UpdateFilteredKosgu();
                ApplyStoredSorting();
            }
            // Находим новую запись в отсортированном списке
            selectedKosguIndex = -1;
            scroll_to_item_index = -1;
            scroll_pending = false;
            for (int i = 0; i < (int)m_filtered_kosgu_entries.size(); i++) {
                if (m_filtered_kosgu_entries[i].id == new_id) {
                    selectedKosguIndex = i;
                    scroll_to_item_index = i;
                    break;
                }
            }
            if (selectedKosguIndex == -1) {
                // Новая запись отфильтрована — добавляем вручную
                auto new_it = std::find_if(kosguEntries.begin(), kosguEntries.end(),
                    [&](const Kosgu &k) { return k.id == new_id; });
                if (new_it != kosguEntries.end()) {
                    m_filtered_kosgu_entries.push_back(*new_it);
                    ApplyStoredSorting();
                    for (int i = 0; i < (int)m_filtered_kosgu_entries.size(); i++) {
                        if (m_filtered_kosgu_entries[i].id == new_id) {
                            selectedKosguIndex = i;
                            scroll_to_item_index = i;
                            break;
                        }
                    }
                }
            }
            if (selectedKosguIndex != -1) {
                selectedKosgu = m_filtered_kosgu_entries[selectedKosguIndex];
                originalKosgu = selectedKosgu;
            }
            isAdding = false;
            isDirty = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " Удалить")) {
            if (selectedKosguIndex != -1) {
                kosgu_id_to_delete = kosguEntries[selectedKosguIndex].id;
                show_delete_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Обновить")) {
            SaveChanges();
            RefreshData();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LIST " Отчет по расшифровкам")) {
            if (!isAdding && selectedKosguIndex != -1 && dbManager) {
                std::string query = "SELECT k.code AS 'КОСГУ', p.date AS 'Дата', p.doc_number AS 'Номер док.', "
                                    "pd.amount AS 'Сумма', p.description AS 'Назначение', c.name AS 'Контрагент' "
                                    "FROM PaymentDetails pd "
                                    "JOIN Payments p ON pd.payment_id = p.id "
                                    "JOIN KOSGU k ON pd.kosgu_id = k.id "
                                    "LEFT JOIN Counterparties c ON p.counterparty_id = c.id "
                                    "WHERE pd.kosgu_id = " + std::to_string(kosguEntries[selectedKosguIndex].id) +
                                    (filterText[0] != '\0' ? " AND (LOWER(p.date) LIKE LOWER('%" + std::string(filterText) + "%') "
                                    "OR LOWER(p.doc_number) LIKE LOWER('%" + std::string(filterText) + "%') "
                                    "OR LOWER(pd.amount) LIKE LOWER('%" + std::string(filterText) + "%') "
                                    "OR LOWER(p.description) LIKE LOWER('%" + std::string(filterText) + "%') "
                                    "OR LOWER(c.name) LIKE LOWER('%" + std::string(filterText) + "%') "
                                    "OR LOWER(k.code) LIKE LOWER('%" + std::string(filterText) + "%'))" : "") +
                                    ";";
                if (uiManager) {
                    uiManager->CreateSpecialQueryView("Отчет по расшифровкам КОСГУ " + kosguEntries[selectedKosguIndex].code, query);
                }
            }
        }

        if (CustomWidgets::ConfirmationModal(
                "Подтверждение удаления##Kosgu", "Подтверждение удаления",
                "Вы уверены, что хотите удалить эту запись КОСГУ?\nЭто "
                "действие нельзя отменить.",
                "Да", "Нет", show_delete_popup)) {
            if (dbManager && kosgu_id_to_delete != -1) {
                dbManager->deleteKosguEntry(kosgu_id_to_delete);
                RefreshData();
                selectedKosgu = Kosgu{};
                originalKosgu = Kosgu{};
            }
            kosgu_id_to_delete = -1;
        }

        ImGui::Separator();

        bool filter_changed = false;
        if (ImGui::InputText("Фильтр", filterText, sizeof(filterText))) {
            filter_changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK)) {
            if (filterText[0] != '\0') {
                filterText[0] = '\0';
                filter_changed = true;
            }
        }

        ImGui::SameLine();
        float avail_width = ImGui::GetContentRegionAvail().x;
        ImGui::PushItemWidth(avail_width - ImGui::GetStyle().ItemSpacing.x);
        const char *filter_items[] = {"Все", "С платежами", "Без платежей",
                                      "Подозрительные слова"};
        if (ImGui::Combo("##FilterCombo", &m_filter_index, filter_items,
                         IM_ARRAYSIZE(filter_items))) {
            filter_changed = true;
        }
        ImGui::PopItemWidth();

        if (filter_changed) {
            SaveChanges();
            UpdateFilteredKosgu();
        }

        ImGui::BeginChild("KosguList", ImVec2(0, list_view_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("kosgu_table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Sortable)) {
            ImGui::TableSetupColumn("ID", 0, 0.0f, 0);
            ImGui::TableSetupColumn("Код", ImGuiTableColumnFlags_DefaultSort,
                                    0.0f, 1);
            ImGui::TableSetupColumn("Наименование", 0, 0.0f, 2);
            ImGui::TableSetupColumn("Сумма", 0, 0.0f, 3);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty) {
                    StoreSortSpecs(sort_specs);
                    SortKosgu(m_filtered_kosgu_entries, sort_specs);
                    sort_specs->SpecsDirty = false;
                }
            }

            // Прокрутка к новой записи: SetScrollY вызывается ОДИН раз
            if (scroll_to_item_index >= 0 && scroll_to_item_index < (int)m_filtered_kosgu_entries.size()) {
                if (!scroll_pending) {
                    float row_y = scroll_to_item_index * ImGui::GetTextLineHeightWithSpacing();
                    ImGui::SetScrollY(row_y);
                    scroll_pending = true;
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(m_filtered_kosgu_entries.size());

            bool need_to_break = false;
            while (clipper.Step() && !need_to_break) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd && !need_to_break;
                     ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    int kosgu_id = m_filtered_kosgu_entries[i].id;

                    bool is_selected = (selectedKosguIndex == i);
                    char label[256];
                    sprintf(label, "%d##%d", m_filtered_kosgu_entries[i].id, i);
                    if (ImGui::Selectable(
                            label, is_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
                        if (selectedKosguIndex != i) {
                            SaveChanges();
                            selectedKosguIndex = i;
                            selectedKosgu = m_filtered_kosgu_entries[i];
                            originalKosgu = selectedKosgu;
                            isAdding = false;
                            isDirty = false;
                            if (dbManager) {
                                payment_info =
                                    dbManager->getPaymentInfoForKosgu(
                                        selectedKosgu.id);
                                // Сразу обновляем отфильтрованные расшифровки
                                m_filtered_payment_info.clear();
                                std::vector<ContractPaymentInfo>* details_src = &payment_info;
                                std::vector<ContractPaymentInfo> suspicious_details;
                                if (m_filter_index == 3 && !suspiciousWordsForFilter.empty()) {
                                    for (const auto &info : payment_info) {
                                        for (const auto &word : suspiciousWordsForFilter) {
                                            if (strcasestr(info.description.c_str(), word.word.c_str()) != nullptr) {
                                                suspicious_details.push_back(info);
                                                break;
                                            }
                                        }
                                    }
                                    details_src = &suspicious_details;
                                }
                                if (filterText[0] != '\0') {
                                    for (const auto &info : *details_src) {
                                        bool match = strcasestr(info.date.c_str(), filterText) != nullptr ||
                                            strcasestr(info.doc_number.c_str(), filterText) != nullptr ||
                                            strcasestr(info.counterparty_name.c_str(), filterText) != nullptr ||
                                            strcasestr(info.description.c_str(), filterText) != nullptr;
                                        if (!match) {
                                            char amount_str[32];
                                            snprintf(amount_str, sizeof(amount_str), "%.2f", info.amount);
                                            match = strcasestr(amount_str, filterText) != nullptr;
                                        }
                                        if (match) m_filtered_payment_info.push_back(info);
                                    }
                                } else {
                                    m_filtered_payment_info = *details_src;
                                }
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
                        ImGui::Text("%s", m_filtered_kosgu_entries[i].code.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", m_filtered_kosgu_entries[i].name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f",
                                    m_filtered_kosgu_entries[i].total_amount);
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        CustomWidgets::HorizontalSplitter("h_splitter", &list_view_height);

        if ((selectedKosguIndex != -1 && selectedKosguIndex < (int)kosguEntries.size()) || isAdding) {
            ImGui::BeginChild("KosguEditor", ImVec2(editor_width, 0), true);

            if (isAdding) {
                ImGui::Text("Добавление новой записи КОСГУ");
            } else {
                ImGui::Text("Редактирование КОСГУ ID: %d", selectedKosgu.id);
            }
            char codeBuf[256];
            char nameBuf[256];

            snprintf(codeBuf, sizeof(codeBuf), "%s",
                     selectedKosgu.code.c_str());
            snprintf(nameBuf, sizeof(nameBuf), "%s",
                     selectedKosgu.name.c_str());

            if (ImGui::InputText("Код", codeBuf, sizeof(codeBuf))) {
                selectedKosgu.code = codeBuf;
                isDirty = true;
            }
            if (ImGui::InputText("Наименование", nameBuf, sizeof(nameBuf))) {
                selectedKosgu.name = nameBuf;
                isDirty = true;
            }

            // Используем вектор для динамического размера буфера
            std::vector<char> noteBuf(selectedKosgu.note.begin(),
                                      selectedKosgu.note.end());
            noteBuf.resize(noteBuf.size() + 1024); // Выделяем доп. место

            if (ImGui::InputTextMultiline(
                    "Примечание", noteBuf.data(), noteBuf.size(),
                    ImVec2(-1, ImGui::GetTextLineHeight() * 4))) {
                selectedKosgu.note = noteBuf.data();
                isDirty = true;
            }
            ImGui::EndChild();
            ImGui::SameLine();

            CustomWidgets::VerticalSplitter("v_splitter", &editor_width);

            ImGui::SameLine();

            ImGui::BeginChild("PaymentDetails", ImVec2(0, 0), true);
            ImGui::Text("Расшифровки платежей:");

            if (filter_changed) {
                std::vector<ContractPaymentInfo> details_to_show = payment_info;
                if (m_filter_index == 3 && !suspiciousWordsForFilter.empty()) {
                    std::vector<ContractPaymentInfo> suspicious_details;
                    for (const auto &info : payment_info) {
                        bool found = false;
                        for (const auto &word : suspiciousWordsForFilter) {
                            if (strcasestr(info.description.c_str(),
                                           word.word.c_str()) != nullptr) {
                                found = true;
                                break;
                            }
                        }
                        if (found) {
                            suspicious_details.push_back(info);
                        }
                    }
                    details_to_show = suspicious_details;
                }

                m_filtered_payment_info.clear();
                if (filterText[0] != '\0') {
                    for (const auto &info : details_to_show) {
                        bool match = false;
                        if (strcasestr(info.date.c_str(), filterText) != nullptr ||
                            strcasestr(info.doc_number.c_str(), filterText) !=
                                nullptr ||
                            strcasestr(info.counterparty_name.c_str(), filterText) !=
                                nullptr ||
                            strcasestr(info.description.c_str(), filterText) !=
                                nullptr) {
                            match = true;
                        }
                        char amount_str[32];
                        snprintf(amount_str, sizeof(amount_str), "%.2f",
                                 info.amount);
                        if (!match &&
                            strcasestr(amount_str, filterText) != nullptr) {
                            match = true;
                        }
                        if (match) {
                            m_filtered_payment_info.push_back(info);
                        }
                    }
                } else {
                    m_filtered_payment_info = details_to_show;
                }
            }

            if (ImGui::BeginTable(
                    "payment_details_table", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
                ImGui::TableSetupColumn("Дата", ImGuiTableColumnFlags_DefaultSort);
                ImGui::TableSetupColumn("Номер док.");
                ImGui::TableSetupColumn("Сумма");
                ImGui::TableSetupColumn("Наименование контрагента");
                ImGui::TableSetupColumn("Назначение");
                ImGui::TableHeadersRow();

                if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                    if (sort_specs->SpecsDirty) {
                        SortPaymentInfo(m_filtered_payment_info, sort_specs);
                        sort_specs->SpecsDirty = false;
                    }
                }

                ImGuiListClipper clipper;
                clipper.Begin(m_filtered_payment_info.size());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd;
                         ++i) {
                        const auto &info = m_filtered_payment_info[i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", info.date.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", info.doc_number.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f", info.amount);
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", info.counterparty_name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", info.description.c_str());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

        } else {
            ImGui::BeginChild("BottomPane", ImVec2(0, 0), true);
            ImGui::Text(
                "Выберите запись для редактирования или добавьте новую.");
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void KosguView::StoreSortSpecs(const ImGuiTableSortSpecs* sort_specs) {
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

void KosguView::ApplyStoredSorting() {
    if (m_stored_sort_specs.empty()) {
        return;
    }
    std::sort(m_filtered_kosgu_entries.begin(), m_filtered_kosgu_entries.end(),
        [&](const Kosgu &a, const Kosgu &b) {
            for (const auto& spec : m_stored_sort_specs) {
                int delta = 0;
                switch (spec.column_index) {
                    case 0: delta = (a.id < b.id) ? -1 : (a.id > b.id) ? 1 : 0; break;
                    case 1: delta = a.code.compare(b.code); break;
                    case 2: delta = a.name.compare(b.name); break;
                    default: break;
                }
                if (delta != 0) {
                    return (spec.sort_direction == ImGuiSortDirection_Ascending) ? (delta < 0) : (delta > 0);
                }
            }
            return false;
        });
}
