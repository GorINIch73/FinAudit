#include "ImportManager.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>


ImportManager::ImportManager() {}

// Helper to split a string by a delimiter
static std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Helper to trim whitespace only
static std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\n\r"); // Exclude "
    if (std::string::npos == first)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r"); // Exclude "
    return str.substr(first, (last - first + 1));
}

// Helper to safely get a value from a row based on the mapping
static std::string get_value_from_row(const std::vector<std::string> &row,
                                      const ColumnMapping &mapping,
                                      const std::string &field_name) {
    auto it = mapping.find(field_name);
    if (it == mapping.end() || it->second == -1) {
        return ""; // Not mapped
    }
    int col_index = it->second;
    if (col_index >= row.size()) {
        return ""; // Index out of bounds
    }
    return trim(row[col_index]);
}

// Helper function to convert DD.MM.YY or DD.MM.YYYY to YYYY-MM-DD
static std::string convertDateToDBFormat(const std::string &date_str_in) {
    std::string date_str = date_str_in;
    
    // Удаляем время (разделители: пробел, 'T', точка с запятой и др.)
    size_t delim_pos = date_str.find_first_of(" \tT;");
    if (delim_pos != std::string::npos) {
        date_str = date_str.substr(0, delim_pos);
    }

    if (date_str.length() == 10 && date_str[2] == '.' &&
        date_str[5] == '.') { // DD.MM.YYYY
        return date_str.substr(6, 4) + "-" + date_str.substr(3, 2) + "-" +
               date_str.substr(0, 2);
    } else if (date_str.length() == 8 && date_str[2] == '.' &&
               date_str[5] == '.') { // DD.MM.YY
        std::string year_short = date_str.substr(6, 2);
        int year_int = std::stoi(year_short);
        std::string full_year = (year_int > 50)
                                    ? "19" + year_short
                                    : "20" + year_short; // Heuristic
        return full_year + "-" + date_str.substr(3, 2) + "-" +
               date_str.substr(0, 2);
    }
    return date_str; // Return as is if format is unexpected
}

bool ImportManager::ImportPaymentsFromTsv(const std::string &filepath,
                                          DatabaseManager *dbManager,
                                          const ColumnMapping &mapping,
                                          std::atomic<float> &progress,
                                          std::string &message,
                                          std::mutex &message_mutex,
                                          std::atomic<bool> &cancel_flag,
                                          const std::string& contract_regex_str,
                                          const std::string& kosgu_regex_str,
                                          bool force_income_type,
                                          bool is_return_import,
                                          const std::string& custom_note
                                          ) {
    if (!dbManager) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: Менеджер базы данных не инициализирован.";
        return false;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: Не удалось открыть TSV файл: " + filepath;
        return false;
    }

    // Get total lines for progress
    file.seekg(0, std::ios::beg);
    size_t total_lines = std::count(std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>(), '\n');
    file.clear();
    file.seekg(0, std::ios::beg);

    std::string line;
    std::getline(file, line); // Skip header line

    std::regex contract_regex;
    std::regex kosgu_regex;
    try {
        contract_regex = std::regex(contract_regex_str);
        kosgu_regex = std::regex(kosgu_regex_str);
    } catch (const std::regex_error &e) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка regex при импорте: " + std::string(e.what());
        return false;
    }
    std::regex amount_regex(
        "\\((\\d{3}-\\d{4}-\\d{10}-\\d{3}):\\s*([\\d=,]+)\\s*ЛС\\)");

    if (!dbManager->beginTransaction()) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: не удалось начать транзакцию импорта.";
        return false;
    }

    size_t line_num = 0;
    while (std::getline(file, line)) {
        // Check for cancellation
        if (cancel_flag) {
            dbManager->rollbackTransaction();
            std::lock_guard<std::mutex> lock(message_mutex);
            message = "Импорт отменен пользователем.";
            progress = 0.0f; // Reset progress
            return false; // Indicate cancellation
        }

        line_num++;
        progress = static_cast<float>(line_num) / total_lines;
        {
            std::lock_guard<std::mutex> lock(message_mutex);
            message = "Импорт строки " + std::to_string(line_num) + " из " +
                      std::to_string(total_lines);
        }

        if (line.empty())
            continue;

        std::vector<std::string> row = split(line, '\t');
        Payment payment;

        payment.date =
            convertDateToDBFormat(get_value_from_row(row, mapping, "Дата"));
        payment.doc_number = get_value_from_row(row, mapping, "Номер док.");
        std::string type_str_from_file = get_value_from_row(row, mapping, "Тип");
        std::transform(type_str_from_file.begin(), type_str_from_file.end(), type_str_from_file.begin(),
            [](unsigned char c){ return std::tolower(c); });
        payment.type = (type_str_from_file == "income" || type_str_from_file == "поступление" || type_str_from_file == "1");

        std::string local_payer_name =
            get_value_from_row(row, mapping, "Плательщик");
        payment.recipient = get_value_from_row(row, mapping, "Контрагент");
        payment.description = get_value_from_row(row, mapping, "Назначение");
        payment.note = get_value_from_row(row, mapping, "Примечание");

        if (!custom_note.empty()) {
            if (!payment.note.empty()) {
                payment.note = custom_note + " " + payment.note;
            } else {
                payment.note = custom_note;
            }
        }

        try {
            std::string amount_str = get_value_from_row(row, mapping, "Сумма");
            std::replace(amount_str.begin(), amount_str.end(), ',', '.');
            payment.amount = std::stod(amount_str);
        } catch (const std::exception &) {
            payment.amount = 0.0;
        }

        if (is_return_import) {
            payment.amount *= -1;
        }

        // Пропускаем строки с нулевой суммой
        if (std::abs(payment.amount) < 0.001) {
            continue;
        }
        
        if (type_str_from_file.empty()) {
            payment.type = payment.recipient.empty();
        }


        Counterparty counterparty;
        if (payment.type) { // true is income
            counterparty.name = local_payer_name;
        } else {
            counterparty.name = payment.recipient;
        }

        int counterparty_id = -1;
        if (!counterparty.name.empty()) {
            counterparty_id =
                dbManager->getCounterpartyIdByName(counterparty.name);
            if (counterparty_id == -1) {
                if (dbManager->addCounterparty(counterparty)) {
                    counterparty_id = counterparty.id;
                }
            }
        }
        payment.counterparty_id = counterparty_id;

        int current_contract_id = -1;
        std::smatch contract_matches;
        if (std::regex_search(payment.description, contract_matches,
                              contract_regex)) {
            if (contract_matches.size() >= 3) {
                std::string contract_number = contract_matches[1].str();
                std::string contract_date_db_format =
                    convertDateToDBFormat(contract_matches[2].str());
                current_contract_id = dbManager->getContractIdByNumberDate(
                    contract_number, contract_date_db_format);
                if (current_contract_id == -1) {
                    Contract contract_obj{-1, contract_number,
                                          contract_date_db_format,
                                          counterparty_id};
                    int new_contract_id = dbManager->addContract(contract_obj);
                    if (new_contract_id != -1) {
                        current_contract_id = new_contract_id;
                    }
                }
            }
        }

        // Apply force_income_type override if set
        if (force_income_type) {
            payment.type = true; // true is 'income'
        }

        if (!dbManager->addPayment(payment)) {
            continue;
        }
        int new_payment_id = payment.id;

        // --- Новая, более сложная логика обработки КОСГУ ---
        bool handled = false;

        // Сначала ищем шаблон "; в т.ч. KXXX=AMOUNT ..."
        std::string special_pattern_prefix = "; в т.ч.";
        size_t special_pos = payment.description.find(special_pattern_prefix);

        if (special_pos != std::string::npos) {
            std::string details_part = payment.description.substr(special_pos + special_pattern_prefix.length());
            std::regex special_kosgu_regex("К(\\d{3})=([\\d.]+)");
            auto details_begin = std::sregex_iterator(details_part.begin(), details_part.end(), special_kosgu_regex);
            auto details_end = std::sregex_iterator();
            
            std::vector<PaymentDetail> details_to_add;
            double total_details_amount = 0.0;
            
            if (std::distance(details_begin, details_end) > 0) {
                for (std::sregex_iterator i = details_begin; i != details_end; ++i) {
                    std::smatch match = *i;
                    std::string kosgu_code = match[1].str();
                    std::string amount_str = match[2].str();
                    
                    int kosgu_id = dbManager->getKosguIdByCode(kosgu_code);
                    if (kosgu_id == -1) {
                        Kosgu new_kosgu{-1, kosgu_code, "КОСГУ " + kosgu_code};
                        if (dbManager->addKosguEntry(new_kosgu)) {
                            kosgu_id = dbManager->getKosguIdByCode(kosgu_code);
                        }
                    }

                    try {
                        double detail_amount = std::stod(amount_str);
                        total_details_amount += detail_amount;
                        
                        PaymentDetail detail;
                        detail.payment_id = new_payment_id;
                        detail.kosgu_id = kosgu_id;
                        detail.contract_id = current_contract_id;
                        detail.amount = detail_amount;
                        details_to_add.push_back(detail);
                    } catch (const std::exception& e) {
                        total_details_amount = payment.amount + 1; // Force validation fail
                        break;
                    }
                }

                // ВАЖНО: Проверяем сумму с небольшой погрешностью
                if (total_details_amount > 0 && total_details_amount <= (payment.amount + 0.01)) {
                    for (auto& detail : details_to_add) {
                        dbManager->addPaymentDetail(detail);
                    }
                    handled = true;
                }
            }
        }
        
        // Если специальный шаблон не был обработан или обработан с ошибкой
        if (!handled) {
            PaymentDetail detail;
            detail.payment_id = new_payment_id;

            // --- FIX: Use the kosgu_regex if the special pattern fails ---
            int kosgu_id_from_regex = -1;
            std::smatch kosgu_matches;
            if (!kosgu_regex_str.empty() && std::regex_search(payment.description, kosgu_matches, kosgu_regex)) {
                if (kosgu_matches.size() > 1) { // Assuming the code is in the first capture group
                    std::string kosgu_code = kosgu_matches[1].str();
                    kosgu_id_from_regex = dbManager->getKosguIdByCode(kosgu_code);
                    if (kosgu_id_from_regex == -1) {
                        Kosgu new_kosgu{-1, kosgu_code, "КОСГУ " + kosgu_code};
                        if (dbManager->addKosguEntry(new_kosgu)) {
                            kosgu_id_from_regex = dbManager->getKosguIdByCode(kosgu_code);
                        }
                    }
                }
            }
            detail.kosgu_id = kosgu_id_from_regex;
            // --- END FIX ---

            detail.contract_id = current_contract_id;
            detail.amount = payment.amount;
            dbManager->addPaymentDetail(detail);
        }
    }

    if (!dbManager->commitTransaction()) {
        dbManager->rollbackTransaction();
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: не удалось зафиксировать транзакцию импорта.";
        progress = 0.0f;
        return false;
    }

    file.close();
    {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Импорт завершен.";
    }
    progress = 1.0f;
    return true; 
}


bool ImportManager::importIKZFromFile(
    const std::string& filepath,
    DatabaseManager* dbManager,
    std::vector<UnfoundContract>& unfoundContracts,
    int& successfulImports,
    std::atomic<float>& progress,
    std::string& message,
    std::mutex& message_mutex
) {
    if (!dbManager) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: Менеджер базы данных не инициализирован.";
        return false;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: Не удалось открыть файл: " + filepath;
        return false;
    }

    // 1. Get total lines and detect delimiter
    file.seekg(0, std::ios::beg);
    size_t total_lines = 0;
    char delimiter = ','; // Default to comma
    std::string first_line;
    if (std::getline(file, first_line)) {
        total_lines = 1 + std::count(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), '\n');
        
        size_t comma_count = std::count(first_line.begin(), first_line.end(), ',');
        size_t tab_count = std::count(first_line.begin(), first_line.end(), '\t');
        if (tab_count > comma_count) {
            delimiter = '\t';
        }
    } else {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Файл пуст или нечитаем.";
        return true; // Not a failure, just nothing to do
    }
    
    file.clear();
    file.seekg(0, std::ios::beg);

    // 2. Process file
    unfoundContracts.clear();
    successfulImports = 0;
    std::string line;
    std::getline(file, line); // Skip header line

    if (!dbManager->beginTransaction()) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: не удалось начать транзакцию импорта ИКЗ.";
        return false;
    }

    size_t line_num = 1; // Start at 1 because we already read the header
    while (std::getline(file, line)) {
        line_num++;
        progress = static_cast<float>(line_num) / total_lines;
        {
            std::lock_guard<std::mutex> lock(message_mutex);
            message = "Импорт строки " + std::to_string(line_num) + " из " + std::to_string(total_lines);
        }

        if (line.empty()) continue;

        std::vector<std::string> row = split(line, delimiter);

        if (row.size() < 3) continue; // Skip malformed lines

        std::string contract_number = trim(row[0]);
        std::string contract_date_raw = trim(row[1]);
        std::string ikz = trim(row[2]);

        // Функция для очистки невидимых символов из начала строки
        auto strip_invisible_chars = [](std::string& str) {
            while (!str.empty()) {
                unsigned char c0 = static_cast<unsigned char>(str[0]);
                // UTF-8 BOM (EF BB BF) - U+FEFF
                if (str.size() >= 3 && c0 == 0xEF &&
                    static_cast<unsigned char>(str[1]) == 0xBB &&
                    static_cast<unsigned char>(str[2]) == 0xBF) {
                    str.erase(0, 3);
                }
                // Неразрывный пробел U+00A0 (C2 A0)
                else if (str.size() >= 2 && c0 == 0xC2 &&
                         static_cast<unsigned char>(str[1]) == 0xA0) {
                    str.erase(0, 2);
                }
                // U+2000..U+200F (E2 80 80 .. E2 80 8F)
                else if (str.size() >= 3 && c0 == 0xE2 &&
                         static_cast<unsigned char>(str[1]) == 0x80 &&
                         static_cast<unsigned char>(str[2]) >= 0x80 &&
                         static_cast<unsigned char>(str[2]) <= 0x8F) {
                    str.erase(0, 3);
                }
                // U+202F NARROW NO-BREAK SPACE (E2 80 AF)
                else if (str.size() >= 3 && c0 == 0xE2 &&
                         static_cast<unsigned char>(str[1]) == 0x80 &&
                         static_cast<unsigned char>(str[2]) == 0xAF) {
                    str.erase(0, 3);
                }
                // U+205F MEDIUM MATHEMATICAL SPACE (E2 81 9F)
                else if (str.size() >= 3 && c0 == 0xE2 &&
                         static_cast<unsigned char>(str[1]) == 0x81 &&
                         static_cast<unsigned char>(str[2]) == 0x9F) {
                    str.erase(0, 3);
                }
                // Обычные ASCII пробелы
                else if (std::isspace(c0)) {
                    str.erase(0, 1);
                }
                else {
                    break;
                }
            }
        };

        // Очищаем номер договора и ИКЗ от невидимых символов
        strip_invisible_chars(contract_number);
        strip_invisible_chars(ikz);

        if (contract_number.empty() || contract_date_raw.empty() || ikz.empty()) {
            continue; // Skip lines with essential missing data
        }

        std::string contract_date = convertDateToDBFormat(contract_date_raw);

        int updated_count = dbManager->updateContractProcurementCode(contract_number, contract_date, ikz);
        if (updated_count > 0) {
            successfulImports += updated_count;
        } else {
            unfoundContracts.push_back({contract_number, contract_date_raw, ikz});
        }
    }

    if (!dbManager->commitTransaction()) {
        dbManager->rollbackTransaction();
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: не удалось зафиксировать транзакцию импорта ИКЗ.";
        progress = 0.0f;
        return false;
    }

    file.close();
    {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Импорт завершен. Обновлено: " + std::to_string(successfulImports) + ". Не найдено: " + std::to_string(unfoundContracts.size());
    }
    progress = 1.0f;
    return true;
}

// Вспомогательная функция для получения значения из строки по mapping
static std::string get_jo4_value(const std::vector<std::string>& row, const ColumnMapping& mapping, const std::string& field) {
    auto it = mapping.find(field);
    if (it == mapping.end()) return "";
    int col_index = it->second;
    if (col_index < 0 || col_index >= static_cast<int>(row.size())) return "";
    return trim(row[col_index]);
}

bool ImportManager::ImportJournalOrder4FromTsv(
    const std::string& filepath,
    DatabaseManager* dbManager,
    const ColumnMapping& mapping,
    std::atomic<float>& progress,
    std::string& message,
    std::mutex& message_mutex,
    std::atomic<bool>& cancel_flag,
    int& importedDocuments,
    int& importedDetails,
    std::vector<std::string>& errors
) {
    if (!dbManager) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: Менеджер базы данных не инициализирован.";
        return false;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: Не удалось открыть TSV файл: " + filepath;
        return false;
    }

    // Подсчёт строк для прогресса
    file.seekg(0, std::ios::beg);
    size_t total_lines = std::count(std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>(), '\n');
    file.clear();
    file.seekg(0, std::ios::beg);

    std::string line;
    std::getline(file, line); // Пропуск заголовка

    importedDocuments = 0;
    importedDetails = 0;
    errors.clear();
    size_t line_num = 0;

    // Карта для отслеживания созданных документов (key: number+date)
    std::map<std::string, int> doc_cache;

    if (!dbManager->beginTransaction()) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: не удалось начать транзакцию импорта ЖО4.";
        return false;
    }

    // Функция для очистки невидимых символов
    auto strip_invisible = [](std::string& str) {
        while (!str.empty()) {
            unsigned char c0 = static_cast<unsigned char>(str[0]);
            if (str.size() >= 3 && c0 == 0xEF &&
                static_cast<unsigned char>(str[1]) == 0xBB &&
                static_cast<unsigned char>(str[2]) == 0xBF) {
                str.erase(0, 3);
            } else if (str.size() >= 2 && c0 == 0xC2 &&
                       static_cast<unsigned char>(str[1]) == 0xA0) {
                str.erase(0, 2);
            } else if (str.size() >= 3 && c0 == 0xE2 &&
                       static_cast<unsigned char>(str[1]) == 0x80 &&
                       static_cast<unsigned char>(str[2]) >= 0x80 &&
                       static_cast<unsigned char>(str[2]) <= 0x8F) {
                str.erase(0, 3);
            } else if (str.size() >= 3 && c0 == 0xE2 &&
                       static_cast<unsigned char>(str[1]) == 0x80 &&
                       static_cast<unsigned char>(str[2]) == 0xAF) {
                str.erase(0, 3);
            } else if (std::isspace(c0)) {
                str.erase(0, 1);
            } else {
                break;
            }
        }
    };

    while (std::getline(file, line)) {
        if (cancel_flag) {
            dbManager->rollbackTransaction();
            std::lock_guard<std::mutex> lock(message_mutex);
            message = "Импорт ЖО4 отменен пользователем.";
            progress = 0.0f;
            return false;
        }

        line_num++;
        progress = static_cast<float>(line_num) / total_lines;
        {
            std::lock_guard<std::mutex> lock(message_mutex);
            message = "Импорт ЖО4 строки " + std::to_string(line_num) + " из " +
                      std::to_string(total_lines);
        }

        if (line.empty()) continue;

        std::vector<std::string> row = split(line, '\t');

        // Извлекаем поля
        std::string doc_date = get_jo4_value(row, mapping, "Дата документа");
        std::string doc_number = get_jo4_value(row, mapping, "Номер документа");
        std::string doc_name = get_jo4_value(row, mapping, "Наименование документа");
        std::string counterparty_name = get_jo4_value(row, mapping, "Наименование показателя");
        std::string operation_content = get_jo4_value(row, mapping, "Содержание операции");
        std::string debit_account = get_jo4_value(row, mapping, "Счет дебет");
        std::string credit_account = get_jo4_value(row, mapping, "Счет кредит");
        std::string amount_str = get_jo4_value(row, mapping, "Сумма");

        // Конвертация даты
        std::string date_db = doc_date;
        if (doc_date.length() >= 8) {
            date_db = convertDateToDBFormat(doc_date);
        }

        // Очистка от невидимых символов
        strip_invisible(doc_number);
        strip_invisible(doc_name);
        strip_invisible(counterparty_name);

        double amount = 0.0;
        try {
            // Заменяем запятую на точку
            std::replace(amount_str.begin(), amount_str.end(), ',', '.');
            amount = std::stod(amount_str);
        } catch (...) {
            errors.push_back("Строка " + std::to_string(line_num) + ": неверная сумма '" + amount_str + "'");
            continue;
        }

        // Поиск или создание контрагента
        int counterparty_id = -1;
        if (!counterparty_name.empty()) {
            counterparty_id = dbManager->getCounterpartyIdByName(counterparty_name);
            if (counterparty_id == -1) {
                Counterparty new_cp{-1, counterparty_name, ""};
                counterparty_id = dbManager->addCounterparty(new_cp);
            }
        }

        // Поиск или создание документа основания
        int doc_id = -1;
        std::string cache_key = doc_number + "|" + date_db;

        auto cache_it = doc_cache.find(cache_key);
        if (cache_it != doc_cache.end()) {
            doc_id = cache_it->second;
        } else {
            doc_id = dbManager->getBasePaymentDocumentIdByNumberDate(doc_number, date_db);
        }

        if (doc_id == -1 && !doc_number.empty() && !date_db.empty()) {
            BasePaymentDocument new_doc;
            new_doc.date = date_db;
            new_doc.number = doc_number;
            new_doc.document_name = doc_name;
            new_doc.counterparty_name = counterparty_name;
            new_doc.contract_id = -1;
            new_doc.payment_id = -1;

            doc_id = dbManager->addBasePaymentDocument(new_doc);
            if (doc_id != -1) {
                doc_cache[cache_key] = doc_id;
                importedDocuments++;
            }
        }

        // Создание расшифровки документа
        if (doc_id != -1) {
            BasePaymentDocumentDetail new_detail;
            new_detail.document_id = doc_id;
            new_detail.operation_content = operation_content;
            new_detail.debit_account = debit_account;
            new_detail.credit_account = credit_account;
            new_detail.amount = amount;

            // Автоопределение КОСГУ по счёту дебета
            if (!debit_account.empty()) {
                // Поиск КОСГУ по коду (например, "201" -> КОСГУ 201)
                std::string kosgu_code = debit_account.substr(0, 3);
                int kosgu_id = dbManager->getKosguIdByCode(kosgu_code);
                new_detail.kosgu_id = kosgu_id;
            }

            if (dbManager->addBasePaymentDocumentDetail(new_detail)) {
                importedDetails++;
            } else {
                errors.push_back("Строка " + std::to_string(line_num) + ": ошибка создания расшифровки");
            }
        } else {
            errors.push_back("Строка " + std::to_string(line_num) + ": не удалось создать документ");
        }
    }

    if (!dbManager->commitTransaction()) {
        dbManager->rollbackTransaction();
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: не удалось зафиксировать транзакцию импорта ЖО4.";
        progress = 0.0f;
        return false;
    }

    file.close();
    {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Импорт ЖО4 завершен. Документов: " + std::to_string(importedDocuments) +
                  ", Расшифровок: " + std::to_string(importedDetails);
        if (!errors.empty()) {
            message += ". Ошибок: " + std::to_string(errors.size());
        }
    }
    progress = 1.0f;
    return true;
}
