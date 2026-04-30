#include "PaymentsView.h"
#include "../Contract.h"
#include "../IconsFontAwesome6.h"
#include "../BasePaymentDocument.h"
#include "../UIManager.h"
#include "CustomWidgets.h"
#include <algorithm> // для std::sort
#include <cstring>   // Для strcasestr и memset
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "../UIManager.h"

static std::string normalize_payment_doc_number(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            result.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return result;
}

static bool is_reliable_payment_doc_ref(const std::string &normalized_ref) {
    if (normalized_ref.size() < 3) {
        return false;
    }

    bool has_alpha = false;
    bool all_digits = true;
    for (unsigned char c : normalized_ref) {
        if (std::isalpha(c)) {
            has_alpha = true;
        }
        if (!std::isdigit(c)) {
            all_digits = false;
        }
    }

    if (has_alpha) {
        return true;
    }

    if (all_digits) {
        if (normalized_ref.size() <= 3) {
            return false;
        }
        int value = 0;
        try {
            value = std::stoi(normalized_ref);
        } catch (...) {
            return false;
        }
        if (value >= 1900 && value <= 2100) {
            return false;
        }
    }

    return true;
}

static bool payment_text_contains_doc_number(const std::string &doc_number,
                                             const std::string &text) {
    std::string normalized_doc = normalize_payment_doc_number(doc_number);
    if (!is_reliable_payment_doc_ref(normalized_doc)) {
        return false;
    }

    std::string normalized_text = normalize_payment_doc_number(text);
    return normalized_text.find(normalized_doc) != std::string::npos;
}

struct PaymentDocumentReference {
    std::string number;
    std::string date;
};

static std::string normalize_payment_reference_date(const std::string &value) {
    std::smatch match;
    try {
        std::regex iso_date_regex("(\\d{4})-(\\d{1,2})-(\\d{1,2})");
        if (std::regex_search(value, match, iso_date_regex) &&
            match.size() >= 4) {
            int year = std::stoi(match[1].str());
            int month = std::stoi(match[2].str());
            int day = std::stoi(match[3].str());
            if (day >= 1 && day <= 31 && month >= 1 && month <= 12 &&
                year >= 1900 && year <= 2100) {
                char buffer[11];
                snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year,
                         month, day);
                return buffer;
            }
        }

        std::regex date_regex("(\\d{1,2})[./-](\\d{1,2})[./-](\\d{2,4})");
        if (!std::regex_search(value, match, date_regex) || match.size() < 4) {
            return "";
        }
    } catch (const std::regex_error &) {
        return "";
    }

    int day = std::stoi(match[1].str());
    int month = std::stoi(match[2].str());
    int year = std::stoi(match[3].str());
    if (year < 100) {
        year += 2000;
    }
    if (day < 1 || day > 31 || month < 1 || month > 12 ||
        year < 1900 || year > 2100) {
        return "";
    }

    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
    return buffer;
}

static std::vector<PaymentDocumentReference> extract_payment_document_references(
    const std::string &text) {
    std::map<std::string, PaymentDocumentReference> refs;

    try {
        std::regex context_regex(
            "(упд|акт|счет|сч\\.?|накладн\\w*|документ\\s+о\\s+приемк\\w*|"
            "приемк\\w*)"
            "[^0-9a-zA-Zа-яА-Я]{0,20}"
            "([0-9a-zA-Zа-яА-Я/_.-]{1,40}"
            "(?:\\s*(?:,|;|/|\\\\|\\+|и)\\s*[0-9a-zA-Zа-яА-Я/_.-]{1,40})*)",
            std::regex_constants::icase);

        auto begin = std::sregex_iterator(text.begin(), text.end(), context_regex);
        auto end = std::sregex_iterator();
        std::regex token_regex("[0-9a-zA-Zа-яА-Я][0-9a-zA-Zа-яА-Я/_.-]{0,39}");

        for (auto it = begin; it != end; ++it) {
            if ((*it).size() < 3) {
                continue;
            }

            std::string numbers = (*it)[2].str();
            size_t suffix_start = static_cast<size_t>((*it).position()) +
                                  static_cast<size_t>((*it).length());
            std::string suffix = text.substr(
                suffix_start, std::min<size_t>(80, text.size() - suffix_start));
            std::string ref_date = normalize_payment_reference_date(suffix);
            auto token_begin =
                std::sregex_iterator(numbers.begin(), numbers.end(), token_regex);
            for (auto token_it = token_begin; token_it != end; ++token_it) {
                std::string normalized =
                    normalize_payment_doc_number((*token_it).str());
                if (is_reliable_payment_doc_ref(normalized)) {
                    auto &ref = refs[normalized];
                    ref.number = normalized;
                    if (ref.date.empty()) {
                        ref.date = ref_date;
                    }
                }
            }
        }
    } catch (const std::regex_error &) {
        return {};
    }

    std::vector<PaymentDocumentReference> result;
    for (const auto &entry : refs) {
        result.push_back(entry.second);
    }
    return result;
}

static std::set<std::string> extract_payment_reference_dates(
    const std::string &text) {
    std::set<std::string> dates;
    try {
        std::regex date_regex(
            "(\\d{4}-\\d{1,2}-\\d{1,2}|\\d{1,2}[./-]\\d{1,2}[./-]\\d{2,4})");
        auto begin = std::sregex_iterator(text.begin(), text.end(), date_regex);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string normalized =
                normalize_payment_reference_date((*it)[1].str());
            if (!normalized.empty()) {
                dates.insert(normalized);
            }
        }
    } catch (const std::regex_error &) {
        return dates;
    }
    return dates;
}

PaymentsView::PaymentsView()
    : selectedPaymentIndex(-1),
      isAdding(false),
      isDirty(false),
      selectedDetailIndex(-1),
      isAddingDetail(false),
      isDetailDirty(false),
      scroll_to_item_index(-1) {
    Title = "Справочник 'Банк' (Платежи)";
    memset(filterText, 0, sizeof(filterText)); // Инициализация filterText
    memset(counterpartyFilter, 0, sizeof(counterpartyFilter));
    memset(kosguFilter, 0, sizeof(kosguFilter));
    memset(contractFilter, 0, sizeof(contractFilter));
    memset(invoiceFilter, 0, sizeof(invoiceFilter));
    memset(groupKosguFilter, 0, sizeof(groupKosguFilter));
    memset(groupContractFilter, 0, sizeof(groupContractFilter));
    memset(groupInvoiceFilter, 0, sizeof(groupInvoiceFilter));
}

void PaymentsView::SetUIManager(UIManager *manager) { uiManager = manager; }

void PaymentsView::SetDatabaseManager(DatabaseManager *manager) {
    dbManager = manager;
}

void PaymentsView::SetPdfReporter(PdfReporter *reporter) {
    pdfReporter = reporter;
}

void PaymentsView::RefreshData() {
    if (dbManager) {
        payments = dbManager->getPayments();
        UpdateFilteredPayments(); // Ensure the filter is applied to the new
                                  // data
        selectedPaymentIndex = -1;
        paymentDetails.clear();
        selectedDetailIndex = -1;
        InvalidateJo4Cache();
    }
}

void PaymentsView::RefreshDropdownData() {
    if (dbManager) {
        counterpartiesForDropdown = dbManager->getCounterparties();
        kosguForDropdown = dbManager->getKosguEntries();
        contractsForDropdown = dbManager->getContracts();
        baseDocsForDropdown = dbManager->getBasePaymentDocuments();
        suspiciousWordsForFilter = dbManager->getSuspiciousWords();
        InvalidateJo4Cache();
    }
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
PaymentsView::GetDataAsStrings() {
    std::vector<std::string> headers = {"Дата",  "Номер",      "Тип",
                                        "Сумма", "Получатель", "Назначение"};
    std::vector<std::vector<std::string>> rows; // Declared here

    for (const auto &p : payments) {
        rows.push_back({p.date, p.doc_number,
                        (p.type ? "поступление" : "расход"),
                        std::to_string(p.amount), p.recipient, p.description});
    }
    return {headers, rows};
}

void PaymentsView::OnDeactivate() {
    SaveChanges();
    SaveDetailChanges();
}
bool PaymentsView::ForceSave() {
    bool paymentSaved = SaveChanges();
    bool detailSaved = SaveDetailChanges();
    return paymentSaved && detailSaved;
}

bool PaymentsView::SaveChanges() {
    if (!isDirty)
        return true;

    if (dbManager && selectedPayment.id != -1) {
        selectedPayment.description = descriptionBuffer;
        selectedPayment.note = noteBuffer;
        if (!dbManager->updatePayment(selectedPayment)) {
            return false;
        }

        // Обновляем из БД и применяем сортировку
        payments = dbManager->getPayments();
        m_filtered_payments = payments;
        UpdateFilteredPayments();
        ApplyStoredSorting();
        // Находим обновлённую запись
        for (int i = 0; i < (int)m_filtered_payments.size(); i++) {
            if (m_filtered_payments[i].id == selectedPayment.id) {
                selectedPaymentIndex = i;
                selectedPayment = m_filtered_payments[i];
                break;
            }
        }
        originalPayment = selectedPayment;
        descriptionBuffer = selectedPayment.description;
        noteBuffer = selectedPayment.note;
    } else {
        return false;
    }
    isDirty = false;
    return true;
}

bool PaymentsView::SaveDetailChanges() {
    if (!isDetailDirty)
        return true;

    if (dbManager && selectedPayment.id != -1 && selectedDetail.id != -1) {
        if (!dbManager->updatePaymentDetail(selectedDetail)) {
            return false;
        }

        auto it = std::find_if(
            paymentDetails.begin(), paymentDetails.end(),
            [&](const PaymentDetail &d) { return d.id == selectedDetail.id; });
        if (it != paymentDetails.end()) {
            *it = selectedDetail;
        }
    } else {
        return false;
    }

    isDetailDirty = false;
    return true;
}

void PaymentsView::SortPayments(const ImGuiTableSortSpecs *sort_specs) {
    // Сохраняем текущую сортировку
    StoreSortSpecs(sort_specs);

    std::sort(m_filtered_payments.begin(), m_filtered_payments.end(),
              [&](const Payment &a, const Payment &b) {
                  for (int i = 0; i < sort_specs->SpecsCount; i++) {
                      const ImGuiTableColumnSortSpecs *column_spec =
                          &sort_specs->Specs[i];
                      int delta = 0;
                      switch (column_spec->ColumnIndex) {
                      case 0:
                          delta = a.date.compare(b.date);
                          break;
                      case 1:
                          delta = a.doc_number.compare(b.doc_number);
                          break;
                      case 2:
                          delta = (a.amount < b.amount)   ? -1
                                  : (a.amount > b.amount) ? 1
                                                          : 0;
                          break;
                      case 3: {
                          std::string a_cp_name = " ";
                          std::string b_cp_name = " ";
                          auto it_a = std::find_if(
                              counterpartiesForDropdown.begin(),
                              counterpartiesForDropdown.end(),
                              [&](const Counterparty &cp) {
                                  return cp.id == a.counterparty_id;
                              });
                          if (it_a != counterpartiesForDropdown.end())
                              a_cp_name = it_a->name;
                          auto it_b = std::find_if(
                              counterpartiesForDropdown.begin(),
                              counterpartiesForDropdown.end(),
                              [&](const Counterparty &cp) {
                                  return cp.id == b.counterparty_id;
                              });
                          if (it_b != counterpartiesForDropdown.end())
                              b_cp_name = it_b->name;
                          delta = a_cp_name.compare(b_cp_name);
                          break;
                      }
                      case 4:
                          delta = a.description.compare(b.description);
                          break;
                      case 5:
                          delta = a.note.compare(b.note);
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

void PaymentsView::StoreSortSpecs(const ImGuiTableSortSpecs* sort_specs) {
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

void PaymentsView::ApplyStoredSorting() {
    if (m_stored_sort_specs.empty()) {
        return;
    }
    std::vector<ImGuiTableColumnSortSpecs> fake_specs(m_stored_sort_specs.size());
    for (size_t i = 0; i < m_stored_sort_specs.size(); i++) {
        fake_specs[i].ColumnIndex = m_stored_sort_specs[i].column_index;
        fake_specs[i].SortDirection = static_cast<ImGuiSortDirection>(m_stored_sort_specs[i].sort_direction);
    }
    ImGuiTableSortSpecs wrapped_specs;
    wrapped_specs.Specs = fake_specs.data();
    wrapped_specs.SpecsCount = fake_specs.size();
    SortPayments(&wrapped_specs);
}

void PaymentsView::Render() {
    if (!IsVisible) {
        if (isDirty) {
            SaveChanges();
            SaveDetailChanges();
        }
        return;
    }

    // Handle chunked group operation processing
    if (current_operation != NONE) {
        ProcessGroupOperation();
    }

    if (ImGui::Begin(GetTitle(), &IsVisible)) {

        if (dbManager && payments.empty()) {
            RefreshData();
            RefreshDropdownData();
            UpdateFilteredPayments(); // Initial filter
        }

        // --- Progress Bar Popup ---
        if (current_operation != NONE) {
            ImGui::OpenPopup("Выполнение операции...");
        }
        if (ImGui::BeginPopupModal("Выполнение операции...", NULL,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoMove)) {
            if (current_operation == NONE) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::Text("Обработка %d из %zu...", processed_items,
                        items_to_process.size());
            float progress =
                (items_to_process.empty())
                    ? 0.0f
                    : (float)processed_items / (float)items_to_process.size();
            ImGui::ProgressBar(progress, ImVec2(250.0f, 0.0f));
            ImGui::EndPopup();
        }

        // --- Панель управления ---
        if (ImGui::Button(ICON_FA_PLUS " Добавить")) {
            SaveChanges();
            SaveDetailChanges();

            Payment newPayment{};
            newPayment.type = false;
            auto t = std::time(nullptr);
            auto tm = *std::localtime(&t);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d");
            newPayment.date = oss.str();

            int new_id = -1;
            if (dbManager) {
                dbManager->addPayment(newPayment);
                new_id = newPayment.id;
                payments = dbManager->getPayments();
                m_filtered_payments = payments;
                UpdateFilteredPayments();
                ApplyStoredSorting();
            }

            // Находим новую запись в отсортированном списке
            selectedPaymentIndex = -1;
            scroll_to_item_index = -1;
            scroll_pending = false;
            for (int i = 0; i < (int)m_filtered_payments.size(); i++) {
                if (m_filtered_payments[i].id == new_id) {
                    selectedPaymentIndex = i;
                    scroll_to_item_index = i;
                    break;
                }
            }
            if (selectedPaymentIndex == -1) {
                // Новая запись отфильтрована — добавляем вручную
                auto new_it = std::find_if(payments.begin(), payments.end(),
                    [&](const Payment &p) { return p.id == new_id; });
                if (new_it != payments.end()) {
                    m_filtered_payments.push_back(*new_it);
                    ApplyStoredSorting();
                    for (int i = 0; i < (int)m_filtered_payments.size(); i++) {
                        if (m_filtered_payments[i].id == new_id) {
                            selectedPaymentIndex = i;
                            scroll_to_item_index = i;
                            break;
                        }
                    }
                }
            }
            if (selectedPaymentIndex != -1) {
                selectedPayment = m_filtered_payments[selectedPaymentIndex];
                originalPayment = selectedPayment;
            }
            descriptionBuffer = selectedPayment.description;
            noteBuffer = selectedPayment.note;
            paymentDetails = dbManager ? dbManager->getPaymentDetails(new_id > 0 ? new_id : selectedPayment.id) : std::vector<PaymentDetail>();
            isAdding = false;
            isDirty = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " Удалить")) {
            if (selectedPaymentIndex != -1) {
                payment_id_to_delete = payments[selectedPaymentIndex].id;
                show_delete_payment_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Обновить")) {
            SaveChanges();
            SaveDetailChanges();
            RefreshData();
            RefreshDropdownData();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LIST " Отчет по расшифровкам")) {
            if (uiManager) {
                std::string query =
                    "SELECT p.date AS 'Дата', p.doc_number AS 'Номер док.', "
                    "p.type AS 'Тип', p.amount AS 'Сумма платежа', "
                    "pd.amount AS 'Сумма расшифровки', "
                    "k.code AS 'КОСГУ', k.name AS 'Наименование КОСГУ', "
                    "c.name AS 'Контрагент', "
                    "p.description AS 'Назначение' "
                    "FROM Payments p "
                    "JOIN PaymentDetails pd ON p.id = pd.payment_id "
                    "LEFT JOIN KOSGU k ON pd.kosgu_id = k.id "
                    "LEFT JOIN Counterparties c ON p.counterparty_id = c.id ";

                // Добавляем WHERE для фильтрации по выбранным платежам
                if (filterText[0] != '\0') {
                    query += "WHERE (LOWER(p.date) LIKE LOWER('%" +
                             std::string(filterText) +
                             "%') "
                             "OR LOWER(p.doc_number) LIKE LOWER('%" +
                             std::string(filterText) +
                             "%') "
                             "OR LOWER(p.amount) LIKE LOWER('%" +
                             std::string(filterText) +
                             "%') "
                             "OR LOWER(p.description) LIKE LOWER('%" +
                             std::string(filterText) +
                             "%') "
                             "OR LOWER(p.recipient) LIKE LOWER('%" +
                             std::string(filterText) +
                             "%') "
                             "OR LOWER(k.code) LIKE LOWER('%" +
                             std::string(filterText) +
                             "%') "
                             "OR LOWER(k.name) LIKE LOWER('%" +
                             std::string(filterText) +
                             "%') "
                             "OR LOWER(c.name) LIKE LOWER('%" +
                             std::string(filterText) + "%'))";
                }
                query += ";";

                uiManager->CreateSpecialQueryView(
                    "Отчет по расшифровкам платежей", query);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Сверка с банком")) {
            if (uiManager) {
                // std::string query = "SELECT * FROM payments;"; // Placeholder
                // /
                std::string query =
                    "SELECT STRFTIME('%Y', p.date) AS payment_year,k.code AS "
                    "kosgu_code,p.type AS payment_type, k.name AS kosgu_name, "
                    "SUM(pd.amount) AS total_amount FROM PaymentDetails AS pd "
                    "JOIN Payments AS p  ON pd.payment_id = p.id JOIN KOSGU AS "
                    "k ON pd.kosgu_id = k.id WHERE k.code IS NOT NULL AND "
                    "k.code != '' GROUP BY payment_year, payment_type, "
                    "kosgu_code ORDER BY payment_year, "
                    "payment_type, kosgu_code;";
                uiManager->CreateSpecialQueryView("Сверка с банком", query);
            }
        }

        // --- Delete Confirmation Popups ---
        if (CustomWidgets::ConfirmationModal(
                "Подтверждение удаления", "Подтверждение удаления",
                "Вы уверены, что хотите удалить этот платеж?\nЭто действие "
                "нельзя отменить.",
                "Да", "Нет", show_delete_payment_popup)) {
            if (dbManager && payment_id_to_delete != -1) {
                dbManager->deletePayment(payment_id_to_delete);
                RefreshData();
                selectedPayment = Payment{};
                originalPayment = Payment{};
                descriptionBuffer.clear();
                paymentDetails.clear();
                isDirty = false;
            }
            payment_id_to_delete = -1;
        }

        char group_delete_message[256];
        snprintf(group_delete_message, sizeof(group_delete_message),
                 "Вы уверены, что хотите удалить расшифровки для %zu платежей?",
                 m_filtered_payments.size());

        if (CustomWidgets::ConfirmationModal(
                "Удалить все расшифровки?", "Удалить все расшифровки?",
                group_delete_message, "Да", "Нет", show_group_delete_popup)) {
            if (!m_filtered_payments.empty() && current_operation == NONE) {
                StartGroupOperation(DELETE_DETAILS);
            }
        }

        ImGui::Separator();

        bool filter_changed = false;
        if (ImGui::InputText("Фильтр", filterText, sizeof(filterText))) {
            filter_changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK)) {
            filterText[0] = '\0';
            filter_changed = true;
        }

        ImGui::SameLine();
        float avail_width = ImGui::GetContentRegionAvail().x;
        ImGui::PushItemWidth(avail_width - ImGui::GetStyle().ItemSpacing.x);
        const char *filter_items[] = {
            "Все",           "Без КОСГУ",       "Без Договора",
            "Без Накладной", "Без расшифровок", "Подозрительные слова",
            "Поступления",   "С примечанием"};
        if (ImGui::Combo("Фильтр по расшифровкам", &missing_info_filter_index,
                         filter_items, IM_ARRAYSIZE(filter_items))) {
            filter_changed = true;
        }
        ImGui::PopItemWidth();

        if (filter_changed) {
            SaveChanges();
            UpdateFilteredPayments();
        }

        if (ImGui::CollapsingHeader("Групповые операции")) {

            if (ImGui::Button("Добавить расшифровку по КОСГУ")) {
                if (!m_filtered_payments.empty() && current_operation == NONE) {
                    show_add_kosgu_popup = true;
                    groupKosguId = -1;
                    memset(groupKosguFilter, 0, sizeof(groupKosguFilter));
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Заменить")) {
                if (!m_filtered_payments.empty() && current_operation == NONE) {
                    show_replace_popup = true;
                    // Reset state for the popup
                    replacement_target = 0;
                    replacement_kosgu_id = -1;
                    replacement_contract_id = -1;
                    replacement_base_document_id = -1;
                    memset(replacement_kosgu_filter, 0,
                           sizeof(replacement_kosgu_filter));
                    memset(replacement_contract_filter, 0,
                           sizeof(replacement_contract_filter));
                    memset(replacement_invoice_filter, 0,
                           sizeof(replacement_invoice_filter));
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Определить по regex и проставить")) {
                if (!m_filtered_payments.empty() && current_operation == NONE) {
                    show_apply_regex_popup = true;
                    // Reset state
                    regex_target = 0;
                    selected_regex_id = -1;
                    memset(regex_filter, 0, sizeof(regex_filter));
                    if (dbManager) {
                        regexesForDropdown = dbManager->getRegexes();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Удалить расшифровки")) {
                if (!m_filtered_payments.empty() && current_operation == NONE) {
                    show_group_delete_popup = true;
                }
            }
        }

        if (show_add_kosgu_popup) {
            ImGui::OpenPopup("Добавление расшифровки по КОСГУ");
        }

        if (show_apply_regex_popup) {
            ImGui::OpenPopup("Определить по regex");
        }
        if (ImGui::BeginPopupModal("Определить по regex",
                                   &show_apply_regex_popup,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Применить regex для %zu отфильтрованных платежей:",
                        m_filtered_payments.size());
            ImGui::Text("Будет обновлена первая расшифровка без установленного "
                        "значения.\nДля КОСГУ будет добавлена расшифровка по "
                        "остатку если нужно.");

            ImGui::Separator();
            ImGui::RadioButton("Договор", &regex_target, 0);
            ImGui::SameLine();
            ImGui::RadioButton("КОСГУ", &regex_target, 1);
            ImGui::Separator();

            std::vector<CustomWidgets::ComboItem> regexItems;
            for (const auto &r : regexesForDropdown) {
                regexItems.push_back({r.id, r.name});
            }
            CustomWidgets::ComboWithFilter("Выбор Regex", selected_regex_id,
                                           regexItems, regex_filter,
                                           sizeof(regex_filter), 0);

            ImGui::Separator();
            if (ImGui::Button("Найти и проставить", ImVec2(120, 0))) {
                if (dbManager && selected_regex_id != -1 &&
                    !m_filtered_payments.empty() && current_operation == NONE) {
                    StartGroupOperation(APPLY_REGEX);
                }
                show_apply_regex_popup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Отмена", ImVec2(120, 0))) {
                show_apply_regex_popup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Добавление расшифровки по КОСГУ",
                                   &show_add_kosgu_popup,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Добавить расшифровку с КОСГУ для %zu отфильтрованных "
                        "платежей:",
                        m_filtered_payments.size());
            ImGui::Separator();

            std::vector<CustomWidgets::ComboItem> kosguItems;
            for (const auto &k : kosguForDropdown) {
                kosguItems.push_back({k.id, k.code + " " + k.name});
            }
            CustomWidgets::ComboWithFilter("КОСГУ", groupKosguId, kosguItems,
                                           groupKosguFilter,
                                           sizeof(groupKosguFilter), 0);

            ImGui::Separator();

            if (ImGui::Button("ОК", ImVec2(120, 0))) {
                if (dbManager && groupKosguId != -1 &&
                    !m_filtered_payments.empty() && current_operation == NONE) {
                    StartGroupOperation(ADD_KOSGU);
                }
                show_add_kosgu_popup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Отмена", ImVec2(120, 0))) {
                show_add_kosgu_popup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (show_replace_popup) {
            ImGui::OpenPopup("Замена в расшифровках");
        }

        if (ImGui::BeginPopupModal("Замена в расшифровках", &show_replace_popup,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Заменить во всех расшифровках для %zu отфильтрованных "
                        "платежей:",
                        m_filtered_payments.size());

            ImGui::Separator();

            ImGui::RadioButton("КОСГУ", &replacement_target, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Договор", &replacement_target, 1);

            ImGui::Separator();

            if (replacement_target == 0) {
                std::vector<CustomWidgets::ComboItem> kosguItems;
                for (const auto &k : kosguForDropdown) {
                    kosguItems.push_back({k.id, k.code + " " + k.name});
                }
                CustomWidgets::ComboWithFilter(
                    "Новый КОСГУ", replacement_kosgu_id, kosguItems,
                    replacement_kosgu_filter, sizeof(replacement_kosgu_filter),
                    0);
            } else {
                std::vector<CustomWidgets::ComboItem> contractItems;
                for (const auto &c : contractsForDropdown) {
                    contractItems.push_back({c.id, c.number + " " + c.date});
                }
                CustomWidgets::ComboWithFilter(
                    "Новый Договор", replacement_contract_id, contractItems,
                    replacement_contract_filter,
                    sizeof(replacement_contract_filter), 0);
            }

            ImGui::Separator();

            if (ImGui::Button("ОК", ImVec2(120, 0))) {
                if (dbManager && !m_filtered_payments.empty() &&
                    current_operation == NONE) {

                    int new_id = -1;
                    if (replacement_target == 0)
                        new_id = replacement_kosgu_id;
                    else if (replacement_target == 1)
                        new_id = replacement_contract_id;
                    else
                        new_id = replacement_base_document_id;

                    if (new_id != -1) {
                        StartGroupOperation(REPLACE);
                    }
                }
                show_replace_popup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Отмена", ImVec2(120, 0))) {
                show_replace_popup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // --- Totals Display ---
        ImGui::Separator();
        ImGui::Text("платежей: %zu", m_filtered_payments.size());
        // ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 750);
        ImGui::SameLine();
        ImGui::Text("Сумма: %.2f", total_filtered_amount);
        ImGui::SameLine();
        // ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 400);
        ImGui::Text("// %.2f", total_filtered_details_amount);

        // --- Список платежей ---
        ImGui::BeginChild("PaymentsList", ImVec2(0, list_view_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("payments_table", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Sortable |
                                  ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn(
                "Дата",
                ImGuiTableColumnFlags_DefaultSort |
                    ImGuiTableColumnFlags_PreferSortDescending,
                0.0f, 0);
            ImGui::TableSetupColumn("Номер", 0, 0.0f, 1);
            ImGui::TableSetupColumn("Сумма", 0, 0.0f, 2);
            ImGui::TableSetupColumn("Контрагент", 0, 0.0f, 3);
            ImGui::TableSetupColumn(
                "Назначение", ImGuiTableColumnFlags_WidthFixed, 600.0f, 4);
            ImGui::TableSetupColumn(
                "Примечание", ImGuiTableColumnFlags_WidthFixed, 300.0f, 5);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty) {
                    SortPayments(sort_specs);
                    sort_specs->SpecsDirty = false;
                }
            }

            // Прокрутка к новой записи: SetScrollY вызывается ОДИН раз
            if (scroll_to_item_index >= 0 && scroll_to_item_index < (int)m_filtered_payments.size()) {
                if (!scroll_pending) {
                    float row_y = scroll_to_item_index * ImGui::GetTextLineHeightWithSpacing();
                    ImGui::SetScrollY(row_y);
                    scroll_pending = true;
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(m_filtered_payments.size());
            bool need_to_break = false;
            while (clipper.Step() && !need_to_break) {
                for (int i = clipper.DisplayStart;
                     i < clipper.DisplayEnd && !need_to_break; ++i) {
                    const auto &payment = m_filtered_payments[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    // Find the original index in the main 'payments' vector
                    int original_index = -1;
                    for (size_t j = 0; j < payments.size(); ++j) {
                        if (payments[j].id == payment.id) {
                            original_index = j;
                            break;
                        }
                    }

                    bool is_selected = (selectedPaymentIndex == i);
                    char label[128];
                    sprintf(label, "%s##%d", payment.date.c_str(), payment.id);
                    if (ImGui::Selectable(
                            label, is_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
                        if (selectedPaymentIndex != i) {
                            SaveChanges();
                            SaveDetailChanges();
                            selectedPaymentIndex = i;
                            selectedPayment = m_filtered_payments[i];
                            originalPayment = m_filtered_payments[i];
                            descriptionBuffer = selectedPayment.description;
                            noteBuffer = selectedPayment.note;
                            if (dbManager) {
                                paymentDetails =
                                    dbManager->getPaymentDetails(
                                        selectedPayment.id);
                            }
                            isAdding = false;
                            isDirty = false;
                            selectedDetailIndex = -1;
                            selectedJo4DocumentId = -1;
                            InvalidateJo4Cache();
                            isDetailDirty = false;
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
                        ImGui::Text("%s", payment.doc_number.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f", payment.amount);

                        ImGui::TableNextColumn();
                        auto cp_it = std::find_if(
                            counterpartiesForDropdown.begin(),
                            counterpartiesForDropdown.end(),
                            [&](const Counterparty &cp) {
                                return cp.id == payment.counterparty_id;
                            });
                        if (cp_it != counterpartiesForDropdown.end()) {
                            ImGui::Text("%s", cp_it->name.c_str());
                        } else {
                            ImGui::Text("N/A");
                        }

                        ImGui::TableNextColumn();
                        ImGui::Text("%s", payment.description.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", payment.note.c_str());
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        CustomWidgets::HorizontalSplitter("h_splitter", &list_view_height);

        // --- Редактор платежей и расшифровок ---
        ImGui::BeginChild("Editors", ImVec2(0, 0), false);

        ImGui::BeginChild("PaymentEditor", ImVec2(editor_width, 0), true);
        if ((selectedPaymentIndex != -1 &&
             selectedPaymentIndex < (int)payments.size()) ||
            isAdding) {
            if (isAdding) {
                ImGui::Text("Добавление нового платежа");
            } else {
                ImGui::Text("Редактирование платежа ID: %d",
                            selectedPayment.id);
            }

            if (CustomWidgets::InputDate("Дата", selectedPayment.date)) {
                isDirty = true;
            }

            char docNumBuf[256];
            snprintf(docNumBuf, sizeof(docNumBuf), "%s",
                     selectedPayment.doc_number.c_str());
            if (ImGui::InputText("Номер док.", docNumBuf, sizeof(docNumBuf))) {
                selectedPayment.doc_number = docNumBuf;
                isDirty = true;
            }

            ImGui::Text("Тип:");
            ImGui::SameLine();
            if (ImGui::RadioButton("Расход", !selectedPayment.type)) {
                selectedPayment.type = false;
                isDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Поступление", selectedPayment.type)) {
                selectedPayment.type = true;
                isDirty = true;
            }

            if (CustomWidgets::AmountInput("Сумма", selectedPayment.amount)) {
                isDirty = true;
            }

            char recipientBuf[256];
            snprintf(recipientBuf, sizeof(recipientBuf), "%s",
                     selectedPayment.recipient.c_str());
            if (ImGui::InputText("Получатель", recipientBuf,
                                 sizeof(recipientBuf))) {
                selectedPayment.recipient = recipientBuf;
                isDirty = true;
            }

            if (CustomWidgets::InputTextMultilineWithWrap(
                    "Назначение", &descriptionBuffer,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8))) {
                isDirty = true;
            }

            if (CustomWidgets::InputTextMultilineWithWrap(
                    "Примечание", &noteBuffer,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 2))) {
                isDirty = true;
            }

            ImGui::BeginDisabled(selectedPaymentIndex == -1);
            if (ImGui::Button("Создать из назначения...")) {
                show_create_from_desc_popup = true;
                entity_to_create = 0; // Reset to contract
                extracted_number.clear();
                extracted_date.clear();
                existing_entity_id = -1;

                if (dbManager) {
                    regexesForCreatePopup = dbManager->getRegexes();
                }
                selectedRegexIdForCreatePopup = -1;
                selected_regex_name = "";
                editableRegexPatternForCreate[0] = '\0';
                regexFilterForCreatePopup[0] = '\0';
            }
            ImGui::EndDisabled();

            if (show_create_from_desc_popup) {
                ImGui::OpenPopup("Создать из назначения платежа");
            }

            if (ImGui::BeginPopupModal("Создать из назначения платежа",
                                       &show_create_from_desc_popup,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {

                auto TestRegexAndExtract = [&](const char *pattern) {
                    if (pattern[0] != '\0' && dbManager) {
                        try {
                            std::regex re(pattern);
                            std::smatch match;
                            if (std::regex_search(selectedPayment.description,
                                                  match, re) &&
                                match.size() > 2) {
                                extracted_number = match[1].str();
                                extracted_date = match[2].str();

                                extracted_number.erase(
                                    extracted_number.find_last_not_of(
                                        " \n\r\t") +
                                    1);
                                extracted_number.erase(
                                    0, extracted_number.find_first_not_of(
                                           " \n\r\t"));
                                extracted_date.erase(
                                    extracted_date.find_last_not_of(" \n\r\t") +
                                    1);
                                extracted_date.erase(
                                    0, extracted_date.find_first_not_of(
                                           " \n\r\t"));

                                if (entity_to_create == 0) {
                                    existing_entity_id =
                                        dbManager->getContractIdByNumberDate(
                                            extracted_number, extracted_date);
                                } else {
                                    existing_entity_id = -1;
                                }

                            } else {
                                extracted_number = "Не найдено";
                                extracted_date = "Не найдено";
                                existing_entity_id = -1;
                            }
                        } catch (const std::regex_error &e) {
                            extracted_number = "Ошибка Regex";
                            extracted_date = e.what();
                            existing_entity_id = -1;
                        }
                    }
                };

                ImGui::TextWrapped("Назначение: %s",
                                   selectedPayment.description.c_str());
                ImGui::Separator();
                std::vector<CustomWidgets::ComboItem> regexItems;
                for (const auto &r : regexesForCreatePopup) {
                    regexItems.push_back({r.id, r.name});
                }

                if (CustomWidgets::ComboWithFilter(
                        "Выбор Regex", selectedRegexIdForCreatePopup,
                        regexItems, regexFilterForCreatePopup,
                        sizeof(regexFilterForCreatePopup), 0)) {
                    auto it = std::find_if(
                        regexesForCreatePopup.begin(),
                        regexesForCreatePopup.end(), [&](const Regex &r) {
                            return r.id == selectedRegexIdForCreatePopup;
                        });
                    if (it != regexesForCreatePopup.end()) {
                        strncpy(editableRegexPatternForCreate,
                                it->pattern.c_str(),
                                sizeof(editableRegexPatternForCreate) - 1);
                        editableRegexPatternForCreate
                            [sizeof(editableRegexPatternForCreate) - 1] = '\0';
                        TestRegexAndExtract(editableRegexPatternForCreate);
                        selected_regex_name = it->name;
                    }
                }

                if (ImGui::InputText("Шаблон Regex",
                                     editableRegexPatternForCreate,
                                     sizeof(editableRegexPatternForCreate))) {
                    TestRegexAndExtract(editableRegexPatternForCreate);
                }

                if (ImGui::Button("Сохранить как новый")) {
                    if (editableRegexPatternForCreate[0] != '\0') {

                        Regex newRegex;
                        newRegex.name = selected_regex_name;
                        newRegex.name += "*";
                        newRegex.pattern = editableRegexPatternForCreate;

                        bool saved = dbManager->addRegex(newRegex);
                        while (!saved) {
                            newRegex.name += "*";
                            if (newRegex.name.length() > 120) {
                                saved = false; // Ensure we don't enter the
                                               // success block
                                break;
                            }
                            saved = dbManager->addRegex(newRegex);
                        }

                        if (saved) {
                            regexesForCreatePopup = dbManager->getRegexes();
                            selectedRegexIdForCreatePopup = newRegex.id;
                            // show_save_regex_popup = false;
                        }
                        // show_save_regex_popup = true;
                    }
                }

                ImGui::Separator();

                ImGui::RadioButton("Договор", &entity_to_create, 0);
                ImGui::Separator();

                ImGui::Text("Номер и дата договора (можно редактировать):");
                ImGui::SetNextItemWidth(200);
                CustomWidgets::InputText("##extracted_number", &extracted_number);
                ImGui::SetNextItemWidth(120);
                CustomWidgets::InputText("##extracted_date", &extracted_date);
                
                // При ручном изменении проверяем существование договора
                if (dbManager && !extracted_number.empty() && !extracted_date.empty() &&
                    extracted_number != "Не найдено" && extracted_number != "Ошибка Regex") {
                    existing_entity_id = dbManager->getContractIdByNumberDate(
                        extracted_number, extracted_date);
                }

                if (existing_entity_id != -1) {
                    ImGui::TextColored(
                        ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                        "Подсказка: Такой договор уже существует.");
                }

                if (ImGui::Button("Создать") && !extracted_number.empty() &&
                    !extracted_date.empty() &&
                    extracted_number != "Не найдено" &&
                    extracted_number != "Ошибка Regex") {
                    if (dbManager && entity_to_create == 0) {
                        Contract new_contract = {
                            -1, extracted_number, extracted_date,
                            selectedPayment.counterparty_id, 0.0};
                        dbManager->addContract(new_contract);
                    }

                    show_create_from_desc_popup = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if (ImGui::Button("Отмена")) {
                    show_create_from_desc_popup = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            if (!counterpartiesForDropdown.empty()) {
                std::vector<CustomWidgets::ComboItem> counterpartyItems;
                for (const auto &cp : counterpartiesForDropdown) {
                    counterpartyItems.push_back({cp.id, cp.name});
                }
                if (CustomWidgets::ComboWithFilter(
                        "Контрагент", selectedPayment.counterparty_id,
                        counterpartyItems, counterpartyFilter,
                        sizeof(counterpartyFilter), 0)) {
                    isDirty = true;
                }
            }
        } else {
            ImGui::Text("Выберите платеж для редактирования.");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        CustomWidgets::VerticalSplitter("v_splitter", &editor_width);

        ImGui::SameLine();

        // --- Расшифровка платежа ---
        ImGui::BeginChild("PaymentDetailsContainer", ImVec2(0, 0), true);
        if (CustomWidgets::ConfirmationModal(
                "Удалить расшифровку?", "Удалить расшифровку?",
                "Вы уверены, что хотите удалить эту расшифровку?", "Да", "Нет",
                show_delete_detail_popup)) {
            if (dbManager && detail_id_to_delete != -1) {
                dbManager->deletePaymentDetail(detail_id_to_delete);
                paymentDetails =
                    dbManager->getPaymentDetails(selectedPayment.id);
                selectedDetailIndex = -1;
                isDetailDirty = false;
            }
            detail_id_to_delete = -1;
        }

        ImGui::Text("Расшифровки: ");

        if (selectedPaymentIndex != -1) {
            double details_sum = 0.0;
            for (const auto &detail : paymentDetails) {
                details_sum += detail.amount;
            }
            ImGui::SameLine();
            ImGui::Text("Сумма: %.2f", details_sum);
            ImGui::SameLine();
            ImGui::TextDisabled("(Платеж: %.2f / Разница: %.2f)",
                                selectedPayment.amount,
                                selectedPayment.amount - details_sum);

            if (ImGui::Button(ICON_FA_PLUS " Добавить деталь")) {
                SaveDetailChanges();
                // Сразу создаём расшифровку в БД с реальным ID
                double sum_of_existing_details = 0.0;
                for (const auto &detail : paymentDetails) {
                    sum_of_existing_details += detail.amount;
                }
                double remaining_amount = selectedPayment.amount - sum_of_existing_details;

                PaymentDetail newDetail{-1, selectedPayment.id, -1, -1, -1, remaining_amount};
                if (dbManager) {
                    dbManager->addPaymentDetail(newDetail);
                    paymentDetails.push_back(newDetail);
                }

                selectedDetailIndex = -1;
                for (int i = 0; i < (int)paymentDetails.size(); i++) {
                    if (paymentDetails[i].id == newDetail.id) {
                        selectedDetailIndex = i;
                        break;
                    }
                }
                selectedDetail = newDetail;
                originalDetail = newDetail;
                isAddingDetail = false;  // Уже сохранена!
                isDetailDirty = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TRASH " Удалить деталь") &&
                selectedDetailIndex != -1) {
                SaveDetailChanges();
                detail_id_to_delete = paymentDetails[selectedDetailIndex].id;
                show_delete_detail_popup = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Обновить детали") &&
                dbManager) {
                SaveDetailChanges();
                paymentDetails = dbManager->getPaymentDetails(
                    selectedPayment.id); // Refresh details
                selectedDetailIndex = -1;
                isAddingDetail = false;
                isDetailDirty = false;
            }

            ImGui::BeginChild(
                "PaymentDetailsList",
                ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 4), true,
                ImGuiWindowFlags_HorizontalScrollbar);
            if (ImGui::BeginTable("payment_details_table", 4,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Сумма");
                ImGui::TableSetupColumn("КОСГУ");
                ImGui::TableSetupColumn("Договор");
                ImGui::TableSetupColumn("Накладная");
                ImGui::TableHeadersRow();

                bool need_to_break = false;
                for (int i = 0;
                     i < (int)paymentDetails.size() && !need_to_break; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    int detail_id = paymentDetails[i].id;

                    // Find original index
                    int original_index = -1;
                    for (size_t j = 0; j < paymentDetails.size(); ++j) {
                        if (paymentDetails[j].id == detail_id) {
                            original_index = j;
                            break;
                        }
                    }

                    bool is_detail_selected =
                        (selectedDetailIndex == original_index);
                    char detail_label[128];
                    sprintf(detail_label, "%.2f##detail_%d",
                            paymentDetails[i].amount, paymentDetails[i].id);
                    if (ImGui::Selectable(
                            detail_label, is_detail_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
                        if (selectedDetailIndex != original_index &&
                            original_index != -1) {
                            SaveDetailChanges();
                            // Refresh payment details
                            paymentDetails = dbManager->getPaymentDetails(
                                selectedPayment.id);
                            // Re-find by ID
                            int new_index = -1;
                            for (size_t j = 0; j < paymentDetails.size(); ++j) {
                                if (paymentDetails[j].id == detail_id) {
                                    new_index = j;
                                    break;
                                }
                            }
                            if (new_index != -1 &&
                                new_index < (int)paymentDetails.size()) {
                                selectedDetailIndex = new_index;
                                selectedDetail = paymentDetails[new_index];
                                originalDetail = paymentDetails[new_index];
                                isAddingDetail = false;
                                isDetailDirty = false;
                            }
                            need_to_break = true;
                        }
                    }
                    if (!need_to_break && is_detail_selected) {
                        ImGui::SetItemDefaultFocus();
                    }

                    if (!need_to_break) {
                        ImGui::TableNextColumn();
                        const char *kosguCode = "N/A";
                        for (const auto &k : kosguForDropdown) {
                            if (k.id == paymentDetails[i].kosgu_id) {
                                kosguCode = k.code.c_str();
                                break;
                            }
                        }
                        ImGui::Text("%s", kosguCode);

                        ImGui::TableNextColumn();
                        const char *contractNumber = "N/A";
                        for (const auto &c : contractsForDropdown) {
                            if (c.id == paymentDetails[i].contract_id) {
                                contractNumber = c.number.c_str();
                                break;
                            }
                        }
                        ImGui::Text("%s", contractNumber);

                        ImGui::TableNextColumn();
                        const char *docNumber = "N/A";
                        for (const auto &doc : baseDocsForDropdown) {
                            if (doc.id == paymentDetails[i].base_document_id) {
                                docNumber = doc.number.c_str();
                                break;
                            }
                        }
                        ImGui::Text("%s", docNumber);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            if (isAddingDetail || selectedDetailIndex != -1) {
                ImGui::Separator();
                ImGui::Text(isAddingDetail ? "Добавить новую расшифровку"
                                           : "Редактировать расшифровку ID: %d",
                            selectedDetail.id);
                if (CustomWidgets::AmountInput("Сумма##detail",
                                               selectedDetail.amount)) {
                    isDetailDirty = true;
                }

                // Dropdown for KOSGU
                std::vector<CustomWidgets::ComboItem> kosguItems;
                for (const auto &k : kosguForDropdown) {
                    kosguItems.push_back({k.id, k.code});
                }
                if (CustomWidgets::ComboWithFilter(
                        "КОСГУ##detail", selectedDetail.kosgu_id, kosguItems,
                        kosguFilter, sizeof(kosguFilter), 0)) {
                    isDetailDirty = true;
                }

                // Dropdown for Contract
                std::vector<CustomWidgets::ComboItem> contractItems;
                for (const auto &c : contractsForDropdown) {
                    std::string display = c.number + "  " + c.date;
                    contractItems.push_back({c.id, display});
                }
                if (CustomWidgets::ComboWithFilter(
                        "Договор##detail", selectedDetail.contract_id,
                        contractItems, contractFilter, sizeof(contractFilter),
                        0)) {
                    isDetailDirty = true;
                }

                // Dropdown for Invoice
                std::vector<CustomWidgets::ComboItem> docItems;
                docItems.push_back({-1, "Не выбрано"});
                for (const auto &d : baseDocsForDropdown) {
                    std::string display = d.number + "  " + d.date;
                    if (!d.document_name.empty()) display += " (" + d.document_name + ")";
                    docItems.push_back({d.id, display});
                }
                if (CustomWidgets::ComboWithFilter("Документ Основания##detail",
                                                   selectedDetail.base_document_id,
                                                   docItems, invoiceFilter,
                                                   sizeof(invoiceFilter), 0)) {
                    isDetailDirty = true;
                }
            }

            ImGui::Separator();
            ImGui::Checkbox("Показывать ЖО4", &showJo4Panel);
            if (showJo4Panel) {
                RenderJo4DocumentsPanel();
            }
        } else {
            ImGui::Text("Выберите платеж для просмотра расшифровок.");
        }
        ImGui::EndChild();
        ImGui::EndChild();
    }
    ImGui::End();
}

void PaymentsView::UpdateFilteredPayments() {
    total_filtered_amount = 0.0;
    total_filtered_details_amount = 0.0;

    std::map<int, std::vector<PaymentDetail>> details_by_payment;
    if (dbManager) {
        auto all_details = dbManager->getAllPaymentDetails();
        for (const auto &detail : all_details) {
            details_by_payment[detail.payment_id].push_back(detail);
        }
    }

    // Create filtered list
    std::vector<Payment> text_filtered_payments;
    if (filterText[0] != '\0') {
        std::string filter_str(filterText);
        std::stringstream ss(filter_str);
        std::string term;
        std::vector<std::string> search_terms;
        while (std::getline(ss, term, ',')) {
            size_t first = term.find_first_not_of(" \t");
            if (std::string::npos == first)
                continue;
            size_t last = term.find_last_not_of(" \t");
            search_terms.push_back(term.substr(first, (last - first + 1)));
        }

        if (!search_terms.empty()) {
            for (const auto &p : payments) {
                bool all_terms_match = true;
                for (const auto &current_term : search_terms) {
                    bool term_found_in_payment = false;
                    if (strcasestr(p.date.c_str(), current_term.c_str()) !=
                        nullptr)
                        term_found_in_payment = true;
                    if (!term_found_in_payment &&
                        strcasestr(p.doc_number.c_str(),
                                   current_term.c_str()) != nullptr)
                        term_found_in_payment = true;
                    if (!term_found_in_payment &&
                        strcasestr(p.description.c_str(),
                                   current_term.c_str()) != nullptr)
                        term_found_in_payment = true;
                    if (!term_found_in_payment &&
                        strcasestr(p.recipient.c_str(), current_term.c_str()) !=
                            nullptr)
                        term_found_in_payment = true;
                    if (!term_found_in_payment &&
                        strcasestr(p.note.c_str(), current_term.c_str()) !=
                            nullptr)
                        term_found_in_payment = true;
                    if (!term_found_in_payment) {
                        char amount_str[32];
                        snprintf(amount_str, sizeof(amount_str), "%.2f",
                                 p.amount);
                        if (strcasestr(amount_str, current_term.c_str()) !=
                            nullptr)
                            term_found_in_payment = true;
                    }

                    if (!term_found_in_payment) {
                        all_terms_match = false;
                        break;
                    }
                }
                if (all_terms_match) {
                    text_filtered_payments.push_back(p);
                }
            }
        } else {
            text_filtered_payments = payments;
        }
    } else {
        text_filtered_payments = payments;
    }

    // Missing info filter
    m_filtered_payments.clear();
    if (missing_info_filter_index == 0) {
        m_filtered_payments = text_filtered_payments;
    } else if (missing_info_filter_index == 5) { // "Подозрительные слова"
        if (!suspiciousWordsForFilter.empty()) {
            for (const auto &p : text_filtered_payments) {
                bool suspicious_found = false;
                for (const auto &sw : suspiciousWordsForFilter) {
                    if (strcasestr(p.description.c_str(), sw.word.c_str()) !=
                        nullptr) {
                        suspicious_found = true;
                        break;
                    }
                }
                if (suspicious_found) {
                    m_filtered_payments.push_back(p);
                }
            }
        }
    } else if (missing_info_filter_index == 6) { // "Поступления"
        for (const auto &p : text_filtered_payments) {
            if (p.type) { // If payment type is true (receipt)
                m_filtered_payments.push_back(p);
            }
        }
    } else if (missing_info_filter_index == 7) { // "С примечанием"
        for (const auto &p : text_filtered_payments) {
            if (!p.note.empty()) {
                m_filtered_payments.push_back(p);
            }
        }
    } else {
        for (const auto &p : text_filtered_payments) {
            auto it = details_by_payment.find(p.id);

            if (missing_info_filter_index == 4) {     // "Без расшифровок"
                if (it == details_by_payment.end()) { // has no details
                    m_filtered_payments.push_back(p);
                }
            } else { // Filters for missing info inside details (index 1, 2, 3)
                if (it != details_by_payment.end()) {     // has details
                    if (missing_info_filter_index == 2) { // "Без Договора"
                        bool has_detail_without_contract = false;
                        for (const auto &detail : it->second) {
                            if (detail.contract_id == -1) {
                                has_detail_without_contract = true;
                                break;
                            }
                        }

                        if (has_detail_without_contract) {
                            auto cp_it = std::find_if(
                                counterpartiesForDropdown.begin(),
                                counterpartiesForDropdown.end(),
                                [&](const Counterparty &cp) {
                                    return cp.id == p.counterparty_id;
                                });

                            bool contract_is_required = true;
                            if (cp_it != counterpartiesForDropdown.end()) {
                                if (cp_it->is_contract_optional) {
                                    contract_is_required = false;
                                }
                            }

                            if (contract_is_required) {
                                m_filtered_payments.push_back(p);
                            }
                        }
                    } else { // Handle other missing info filters
                        bool missing_found = false;
                        for (const auto &detail : it->second) {
                            if ((missing_info_filter_index == 1 &&
                                 detail.kosgu_id == -1) ||
                                (missing_info_filter_index == 3 &&
                                 detail.base_document_id == -1)) {
                                missing_found = true;
                                break;
                            }
                        }
                        if (missing_found) {
                            m_filtered_payments.push_back(p);
                        }
                    }
                }
            }
        }
    }

    // Calculate totals for the final filtered list
    for (const auto &payment : m_filtered_payments) {
        total_filtered_amount += payment.amount;
        auto it = details_by_payment.find(payment.id);
        if (it != details_by_payment.end()) {
            for (const auto &detail : it->second) {
                total_filtered_details_amount += detail.amount;
            }
        }
    }
}

void PaymentsView::InvalidateJo4Cache() {
    jo4CacheDirty = true;
    cachedJo4PaymentId = -1;
    cachedJo4Links.clear();
    cachedJo4Candidates.clear();
    cachedJo4ReferencedCandidates.clear();
    cachedJo4RefsPreview.clear();
    cachedJo4RefsCount = 0;
    cachedJo4DetailsDocId = -1;
    cachedJo4Details.clear();
}

bool PaymentsView::IsSelectedPaymentContractOptional() const {
    auto cp_it = std::find_if(counterpartiesForDropdown.begin(),
                              counterpartiesForDropdown.end(),
                              [&](const Counterparty &cp) {
                                  return cp.id == selectedPayment.counterparty_id;
                              });
    return cp_it != counterpartiesForDropdown.end() &&
           cp_it->is_contract_optional;
}

void PaymentsView::RebuildJo4Cache() {
    if (!dbManager || selectedPayment.id <= 0) {
        InvalidateJo4Cache();
        return;
    }

    if (!jo4CacheDirty && cachedJo4PaymentId == selectedPayment.id) {
        return;
    }

    cachedJo4PaymentId = selectedPayment.id;
    jo4CacheDirty = false;
    cachedJo4Links = dbManager->getPaymentBaseDocumentLinksForPayment(selectedPayment.id);
    cachedJo4Candidates.clear();
    cachedJo4ReferencedCandidates.clear();
    cachedJo4RefsPreview.clear();
    cachedJo4RefsCount = 0;

    if (IsSelectedPaymentContractOptional()) {
        return;
    }

    std::set<int> linked_doc_ids;
    for (const auto &link : cachedJo4Links) {
        if (link.match_status != "rejected") {
            linked_doc_ids.insert(link.base_document_id);
        }
    }

    std::vector<PaymentDocumentReference> payment_doc_refs =
        extract_payment_document_references(selectedPayment.description);
    std::set<std::string> payment_doc_dates =
        extract_payment_reference_dates(selectedPayment.description);
    cachedJo4RefsCount = payment_doc_refs.size();
    for (size_t i = 0; i < payment_doc_refs.size() && i < 8; ++i) {
        if (i > 0) {
            cachedJo4RefsPreview += ", ";
        }
        cachedJo4RefsPreview += payment_doc_refs[i].number;
        if (!payment_doc_refs[i].date.empty()) {
            cachedJo4RefsPreview += " от " + payment_doc_refs[i].date;
        }
    }

    const std::string normalized_payment_date =
        normalize_payment_reference_date(selectedPayment.date);

    for (const auto &doc : baseDocsForDropdown) {
        if (linked_doc_ids.count(doc.id) > 0) {
            continue;
        }

        int score = 0;
        std::string reason;
        bool referenced_in_payment = false;
        bool amount_match = false;
        bool date_match = false;
        bool date_from_description_match = false;
        std::string normalized_doc_date =
            normalize_payment_reference_date(doc.date);
        std::string normalized_doc_number =
            normalize_payment_doc_number(doc.number);

        if (!normalized_doc_date.empty() &&
            payment_doc_dates.count(normalized_doc_date) > 0) {
            date_match = true;
            date_from_description_match = true;
        }

        if (is_reliable_payment_doc_ref(normalized_doc_number)) {
            for (const auto &ref : payment_doc_refs) {
                if (ref.number == normalized_doc_number ||
                    normalized_doc_number.find(ref.number) != std::string::npos ||
                    ref.number.find(normalized_doc_number) != std::string::npos) {
                    referenced_in_payment = true;
                    if (!ref.date.empty() && ref.date == normalized_doc_date) {
                        date_match = true;
                        date_from_description_match = true;
                    }
                    break;
                }
            }
        }

        if (referenced_in_payment) {
            score += 85;
            reason += "номер после счет/акт/УПД/накладная; ";
            if (date_from_description_match) {
                score += 30;
                reason += "дата документа из назначения; ";
            }
        } else if (!doc.number.empty() &&
                   payment_text_contains_doc_number(doc.number,
                                                    selectedPayment.description)) {
            referenced_in_payment = true;
            score += 45;
            reason += "номер встречается в назначении; ";
        }

        if (std::abs(std::abs(doc.total_amount) -
                     std::abs(selectedPayment.amount)) <= 0.01) {
            amount_match = true;
            score += 25;
            reason += "сумма; ";
        }

        if (!date_match && !normalized_doc_date.empty() &&
            normalized_doc_date == normalized_payment_date) {
            date_match = true;
            score += 10;
            reason += "дата платежа; ";
        }

        if (amount_match) {
            Jo4Candidate candidate;
            candidate.doc_id = doc.id;
            candidate.score = score;
            candidate.reason = reason;
            candidate.referenced_in_payment = referenced_in_payment;
            candidate.amount_match = amount_match;
            candidate.date_match = date_match;
            candidate.date_from_description_match = date_from_description_match;
            candidate.number_strong_match = amount_match && referenced_in_payment;
            candidate.date_strong_match =
                amount_match && date_from_description_match;
            cachedJo4Candidates.push_back(candidate);
            if (referenced_in_payment) {
                cachedJo4ReferencedCandidates.push_back(candidate);
            }
        }
    }

    std::sort(cachedJo4Candidates.begin(), cachedJo4Candidates.end(),
              [](const Jo4Candidate &a, const Jo4Candidate &b) {
                  if (a.number_strong_match != b.number_strong_match) {
                      return a.number_strong_match > b.number_strong_match;
                  }
                  if (a.date_strong_match != b.date_strong_match) {
                      return a.date_strong_match > b.date_strong_match;
                  }
                  if (a.referenced_in_payment != b.referenced_in_payment) {
                      return a.referenced_in_payment > b.referenced_in_payment;
                  }
                  return a.score > b.score;
              });

    auto doc_date_for_candidate = [&](const Jo4Candidate &candidate) {
        auto it = std::find_if(baseDocsForDropdown.begin(), baseDocsForDropdown.end(),
                               [&](const BasePaymentDocument &doc) {
                                   return doc.id == candidate.doc_id;
                               });
        return it == baseDocsForDropdown.end() ? std::string() : it->date;
    };
    std::sort(cachedJo4ReferencedCandidates.begin(),
              cachedJo4ReferencedCandidates.end(),
              [&](const Jo4Candidate &a, const Jo4Candidate &b) {
                  return doc_date_for_candidate(a) < doc_date_for_candidate(b);
              });
}

void PaymentsView::RenderJo4DocumentsPanel() {
    if (!dbManager || selectedPayment.id <= 0) {
        return;
    }

    ImGui::Separator();
    ImGui::Text("ЖО4 / документы основания");
    RebuildJo4Cache();

    std::set<int> linked_doc_ids;
    for (const auto &link : cachedJo4Links) {
        if (link.match_status != "rejected") {
            linked_doc_ids.insert(link.base_document_id);
        }
    }

    auto find_doc = [&](int doc_id) -> const BasePaymentDocument* {
        auto it = std::find_if(baseDocsForDropdown.begin(), baseDocsForDropdown.end(),
                               [&](const BasePaymentDocument &doc) {
                                   return doc.id == doc_id;
                               });
        return it == baseDocsForDropdown.end() ? nullptr : &(*it);
    };

    if (ImGui::BeginTable("linked_jo4_docs_table", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Сумма");
        ImGui::TableSetupColumn("Дата");
        ImGui::TableSetupColumn("Номер");
        ImGui::TableSetupColumn("Документ");
        ImGui::TableSetupColumn("Контрагент ЖО4", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("Статус");
        ImGui::TableSetupColumn("Причина");
        ImGui::TableHeadersRow();

        for (const auto &link : cachedJo4Links) {
            if (link.match_status == "rejected") {
                continue;
            }
            const BasePaymentDocument *doc = find_doc(link.base_document_id);
            if (!doc) {
                continue;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", doc->total_amount);
            ImGui::TableNextColumn();
            char label[128];
            snprintf(label, sizeof(label), "%s##linked_jo4_%d",
                     doc->date.c_str(), doc->id);
            if (ImGui::Selectable(label, selectedJo4DocumentId == doc->id,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selectedJo4DocumentId = doc->id;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc->number.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc->document_name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc->counterparty_name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", link.match_status.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", link.match_reason.c_str());
        }
        ImGui::EndTable();
    }

    if (selectedJo4DocumentId != -1) {
        const BasePaymentDocument *doc = find_doc(selectedJo4DocumentId);
        if (doc) {
            ImGui::Text("Строки ЖО4: %s от %s, %s",
                        doc->number.c_str(), doc->date.c_str(),
                        doc->counterparty_name.c_str());
            if (cachedJo4DetailsDocId != doc->id) {
                cachedJo4Details =
                    dbManager->getBasePaymentDocumentDetails(doc->id);
                cachedJo4DetailsDocId = doc->id;
            }
            if (ImGui::BeginTable("selected_jo4_doc_details", 5,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_ScrollX)) {
                ImGui::TableSetupColumn("Содержание", ImGuiTableColumnFlags_WidthFixed, 380.0f);
                ImGui::TableSetupColumn("Дебет");
                ImGui::TableSetupColumn("Кредит");
                ImGui::TableSetupColumn("КОСГУ");
                ImGui::TableSetupColumn("Сумма");
                ImGui::TableHeadersRow();

                for (const auto &detail : cachedJo4Details) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", detail.operation_content.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", detail.debit_account.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", detail.credit_account.c_str());
                    ImGui::TableNextColumn();
                    const char *kosguCode = "N/A";
                    for (const auto &k : kosguForDropdown) {
                        if (k.id == detail.kosgu_id) {
                            kosguCode = k.code.c_str();
                            break;
                        }
                    }
                    ImGui::Text("%s", kosguCode);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", detail.amount);
                }
                ImGui::EndTable();
            }
        }
    }

    if (cachedJo4RefsCount > 0) {
        ImGui::Text("Ссылки в назначении: %zu", cachedJo4RefsCount);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", cachedJo4RefsPreview.c_str());
    }

    if (IsSelectedPaymentContractOptional()) {
        ImGui::TextDisabled(
            "Кандидаты ЖО4 не подбираются: у контрагента отмечен необязательный договор.");
        return;
    }

    if (cachedJo4ReferencedCandidates.size() > 1) {
        double group_sum = 0.0;
        for (const auto &candidate : cachedJo4ReferencedCandidates) {
            const BasePaymentDocument *doc = find_doc(candidate.doc_id);
            if (doc) {
                group_sum += doc->total_amount;
            }
        }

        ImGui::Text("Группа по ссылкам: документов %zu, сумма %.2f, разница %.2f",
                    cachedJo4ReferencedCandidates.size(), group_sum,
                    selectedPayment.amount - group_sum);
    }

    ImGui::Text("Кандидаты ЖО4");
    ImGui::SameLine();
    ImGui::TextDisabled("показаны только совпадения по сумме");
    if (ImGui::BeginTable("jo4_candidates_table", 8,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollX)) {
        ImGui::TableSetupColumn("Балл");
        ImGui::TableSetupColumn("Сумма");
        ImGui::TableSetupColumn("Дата");
        ImGui::TableSetupColumn("Номер");
        ImGui::TableSetupColumn("Документ");
        ImGui::TableSetupColumn("Контрагент ЖО4", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("Причина", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("Совп.");
        ImGui::TableHeadersRow();

        int shown = 0;
        for (const auto &candidate : cachedJo4Candidates) {
            if (shown++ >= 12) {
                break;
            }

            const BasePaymentDocument *doc = find_doc(candidate.doc_id);
            if (!doc) {
                continue;
            }
            ImGui::TableNextRow();
            if (candidate.number_strong_match) {
                ImU32 color =
                    ImGui::GetColorU32(ImVec4(0.05f, 0.48f, 0.16f, 0.75f));
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, color);
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg1, color);
            } else if (candidate.date_strong_match) {
                ImU32 color =
                    ImGui::GetColorU32(ImVec4(0.38f, 0.58f, 0.30f, 0.55f));
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, color);
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg1, color);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", candidate.score);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", doc->total_amount);
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc->date.c_str());
            ImGui::TableNextColumn();
            char label[128];
            snprintf(label, sizeof(label), "%s##candidate_jo4_%d",
                     doc->number.c_str(), doc->id);
            if (ImGui::Selectable(label, selectedJo4DocumentId == doc->id,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selectedJo4DocumentId = doc->id;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc->document_name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", doc->counterparty_name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", candidate.reason.c_str());
            ImGui::TableNextColumn();
            std::string match_flags = "Сумма";
            if (candidate.number_strong_match) {
                match_flags += ", Номер";
            }
            if (candidate.date_strong_match) {
                match_flags += ", Дата";
            }
            ImGui::Text("%s", match_flags.c_str());
        }
        ImGui::EndTable();
    }
}

bool PaymentsView::StartGroupOperation(GroupOperationType operation) {
    if (!dbManager || current_operation != NONE || m_filtered_payments.empty()) {
        return false;
    }

    if (uiManager) {
        std::string backupPath;
        if (!uiManager->BackupCurrentDatabase("group_payments", backupPath)) {
            return false;
        }
    }

    items_to_process = m_filtered_payments;
    processed_items = 0;
    group_transaction_active = false;
    current_operation = operation;
    return true;
}

void PaymentsView::ProcessGroupOperation() {
    if (!dbManager || current_operation == NONE || items_to_process.empty()) {
        if (group_transaction_active && dbManager) {
            dbManager->rollbackTransaction();
            group_transaction_active = false;
        }
        return;
    }

    if (!group_transaction_active && processed_items == 0) {
        if (!dbManager->beginTransaction()) {
            current_operation = NONE;
            processed_items = 0;
            items_to_process.clear();
            return;
        }
        group_transaction_active = true;
    }

    const int items_per_frame = 20;
    int processed_in_frame = 0;

    while (processed_items < items_to_process.size() &&
           processed_in_frame < items_per_frame) {
        const auto &payment = items_to_process[processed_items];

        switch (current_operation) {
        case ADD_KOSGU: {
            auto details = dbManager->getPaymentDetails(payment.id);
            double sum_of_details = 0.0;
            for (const auto &detail : details) {
                sum_of_details += detail.amount;
            }
            double remaining_amount = payment.amount - sum_of_details;
            if (remaining_amount > 0.009 && groupKosguId != -1) {
                PaymentDetail newDetail;
                newDetail.payment_id = payment.id;
                newDetail.amount = remaining_amount;
                newDetail.kosgu_id = groupKosguId;
                newDetail.contract_id = -1;
                newDetail.base_document_id = -1;
                dbManager->addPaymentDetail(newDetail);
            }
            break;
        }
        case REPLACE: {
            std::string field_to_update;
            int new_id = -1;
            if (replacement_target == 0) {
                field_to_update = "kosgu_id";
                new_id = replacement_kosgu_id;
            } else {
                field_to_update = "contract_id";
                new_id = replacement_contract_id;
            }

            if (new_id != -1) {
                dbManager->bulkUpdatePaymentDetails({payment.id},
                                                    field_to_update, new_id);
            }
            break;
        }
        case DELETE_DETAILS: {
            dbManager->deleteAllPaymentDetails(payment.id);
            break;
        }
        case APPLY_REGEX: {
            auto it = std::find_if(
                regexesForDropdown.begin(), regexesForDropdown.end(),
                [&](const Regex &r) { return r.id == selected_regex_id; });
            if (it != regexesForDropdown.end()) {
                try {
                    std::regex re(it->pattern);
                    std::smatch match;
                    if (std::regex_search(payment.description, match, re) &&
                        match.size() >
                            1) { // Changed to > 1 as KOSGU only needs 1 group

                        if (regex_target == 1) { // Target is KOSGU
                            std::string kosgu_code = match[1].str();
                            kosgu_code.erase(
                                kosgu_code.find_last_not_of(" \n\r\t") + 1);
                            kosgu_code.erase(
                                0, kosgu_code.find_first_not_of(" \n\r\t"));

                            int kosgu_id =
                                dbManager->getKosguIdByCode(kosgu_code);
                            if (kosgu_id == -1) {
                                Kosgu new_kosgu{-1, kosgu_code,
                                                "КОСГУ " + kosgu_code};
                                if (dbManager->addKosguEntry(new_kosgu)) {
                                    kosgu_id =
                                        dbManager->getKosguIdByCode(kosgu_code);
                                }
                            }

                            if (kosgu_id != -1) {
                                auto details =
                                    dbManager->getPaymentDetails(payment.id);
                                double total_existing_details_amount = 0.0;
                                for (const auto &detail : details) {
                                    total_existing_details_amount +=
                                        detail.amount;
                                }

                                if (details.empty()) {
                                    PaymentDetail newDetail;
                                    newDetail.payment_id = payment.id;
                                    newDetail.amount = payment.amount;
                                    newDetail.kosgu_id = kosgu_id;
                                    newDetail.contract_id = -1;
                                    dbManager->addPaymentDetail(newDetail);
                                } else if (total_existing_details_amount <
                                           payment.amount) {
                                    double amount_to_add =
                                        payment.amount -
                                        total_existing_details_amount;
                                    PaymentDetail newDetail;
                                    newDetail.payment_id = payment.id;
                                    newDetail.amount = amount_to_add;
                                    newDetail.kosgu_id = kosgu_id;
                                    newDetail.contract_id = -1;
                                    dbManager->addPaymentDetail(newDetail);
                                } else {
                                    bool updated_existing = false;
                                    for (auto &detail : details) {
                                        if (detail.kosgu_id == -1) {
                                            detail.kosgu_id = kosgu_id;
                                            dbManager->updatePaymentDetail(
                                                detail);
                                            updated_existing = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        } else if (regex_target == 0 && match.size() >
                                   2) { // Contract, requires 2 groups (number and date)
                            std::string number = match[1].str();
                            std::string date = match[2].str();

                            number.erase(number.find_last_not_of(" \n\r\t") +
                                         1);
                            number.erase(0,
                                         number.find_first_not_of(" \n\r\t"));
                            date.erase(date.find_last_not_of(" \n\r\t") + 1);
                            date.erase(0, date.find_first_not_of(" \n\r\t"));

                            int id_to_set = -1;
                            id_to_set =
                                dbManager->getContractIdByNumberDate(number,
                                                                     date);
                            if (id_to_set == -1) {
                                Contract new_contract = {
                                    -1, number, date,
                                    payment.counterparty_id, 0.0};
                                id_to_set =
                                    dbManager->addContract(new_contract);
                            }

                            if (id_to_set != -1) {
                                auto details =
                                    dbManager->getPaymentDetails(payment.id);
                                if (details.empty()) {
                                    PaymentDetail newDetail;
                                    newDetail.payment_id = payment.id;
                                    newDetail.amount = payment.amount;
                                    newDetail.contract_id = id_to_set;
                                    dbManager->addPaymentDetail(newDetail);
                                } else {
                                    for (auto &detail : details) {
                                        if (detail.contract_id == -1) {
                                            detail.contract_id = id_to_set;
                                            dbManager->updatePaymentDetail(
                                                detail);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                } catch (const std::regex_error &e) {
                    // Ignore regex errors in batch processing
                }
            }
            break;
        }
        case NONE:
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

        // Refresh details of currently selected payment if any
        if (selectedPaymentIndex != -1) {
            paymentDetails = dbManager->getPaymentDetails(selectedPayment.id);
        }
    }
}
