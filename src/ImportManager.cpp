#include "ImportManager.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>


ImportManager::ImportManager() {}

// Helper to split a string by a delimiter
static std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;

    bool in_quotes = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            if (in_quotes && i + 1 < s.size() && s[i + 1] == '"') {
                token.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == delimiter && !in_quotes) {
            tokens.push_back(token);
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    tokens.push_back(token);
    return tokens;
}

static char detectDelimiter(const std::string &header_line) {
    size_t tab_count = std::count(header_line.begin(), header_line.end(), '\t');
    size_t semicolon_count =
        std::count(header_line.begin(), header_line.end(), ';');
    size_t comma_count = std::count(header_line.begin(), header_line.end(), ',');

    if (tab_count >= semicolon_count && tab_count >= comma_count && tab_count > 0) {
        return '\t';
    }
    if (semicolon_count >= comma_count && semicolon_count > 0) {
        return ';';
    }
    return ',';
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

static std::string normalizeAmountString(const std::string &amount_str) {
    std::string normalized;
    normalized.reserve(amount_str.size());

    bool decimal_added = false;
    for (size_t i = 0; i < amount_str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(amount_str[i]);

        if (std::isdigit(c)) {
            normalized.push_back(static_cast<char>(c));
            continue;
        }

        if (c == '-' && normalized.empty()) {
            normalized.push_back('-');
            continue;
        }

        if ((c == ',' || c == '.' || c == '=' ||
             (c == '-' && !normalized.empty())) &&
            !decimal_added) {
            normalized.push_back('.');
            decimal_added = true;
            continue;
        }

        if (std::isspace(c)) {
            continue;
        }

        // UTF-8 non-breaking spaces used in exported reports.
        if (i + 1 < amount_str.size() && c == 0xC2 &&
            static_cast<unsigned char>(amount_str[i + 1]) == 0xA0) {
            ++i;
            continue;
        }
        if (i + 2 < amount_str.size() && c == 0xE2 &&
            static_cast<unsigned char>(amount_str[i + 1]) == 0x80 &&
            static_cast<unsigned char>(amount_str[i + 2]) == 0xAF) {
            i += 2;
            continue;
        }
    }

    return normalized;
}

static bool parseAmount(const std::string &amount_str, double &amount) {
    bool negative_parentheses =
        amount_str.find('(') != std::string::npos &&
        amount_str.find(')') != std::string::npos;

    std::string normalized = normalizeAmountString(amount_str);
    if (normalized.empty() || normalized == "-" || normalized == ".") {
        return false;
    }

    try {
        amount = std::stod(normalized);
        if (negative_parentheses && amount > 0.0) {
            amount *= -1.0;
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

static std::string extractKosguCodeFromAccount(const std::string &account) {
    int digit_count = 0;
    std::string reversed_digits;

    for (auto it = account.rbegin(); it != account.rend(); ++it) {
        unsigned char c = static_cast<unsigned char>(*it);
        if (std::isdigit(c)) {
            reversed_digits.push_back(static_cast<char>(c));
            digit_count++;
            if (digit_count == 3) {
                std::reverse(reversed_digits.begin(), reversed_digits.end());
                return reversed_digits;
            }
        } else {
            digit_count = 0;
            reversed_digits.clear();
        }
    }

    return "";
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

    DatabaseManager::TransactionGuard transaction(*dbManager);
    if (!transaction.started()) {
        std::lock_guard<std::mutex> lock(message_mutex);
        message = "Ошибка: не удалось начать транзакцию импорта.";
        return false;
    }

    std::map<std::string, int> counterparty_cache;
    std::map<std::string, int> contract_cache;
    std::map<std::string, int> kosgu_cache;

    auto get_or_create_counterparty_id = [&](const std::string& name) {
        if (name.empty()) {
            return -1;
        }

        auto cache_it = counterparty_cache.find(name);
        if (cache_it != counterparty_cache.end()) {
            return cache_it->second;
        }

        int id = dbManager->getCounterpartyIdByName(name);
        if (id == -1) {
            Counterparty counterparty;
            counterparty.name = name;
            if (dbManager->addCounterparty(counterparty)) {
                id = counterparty.id;
            }
        }
        counterparty_cache[name] = id;
        return id;
    };

    auto get_or_create_contract_id = [&](const std::string& number,
                                         const std::string& date,
                                         int counterparty_id) {
        if (number.empty() || date.empty()) {
            return -1;
        }

        std::string key =
            number + "|" + date + "|" + std::to_string(counterparty_id);
        auto cache_it = contract_cache.find(key);
        if (cache_it != contract_cache.end()) {
            return cache_it->second;
        }

        int id = dbManager->getContractIdByNumberDate(number, date);
        if (id == -1) {
            Contract contract_obj{-1, number, date, counterparty_id};
            id = dbManager->addContract(contract_obj);
        }
        contract_cache[key] = id;
        return id;
    };

    auto get_or_create_kosgu_id = [&](const std::string& code) {
        if (code.empty()) {
            return -1;
        }

        auto cache_it = kosgu_cache.find(code);
        if (cache_it != kosgu_cache.end()) {
            return cache_it->second;
        }

        int id = dbManager->getKosguIdByCode(code);
        if (id == -1) {
            Kosgu new_kosgu{-1, code, "КОСГУ " + code};
            if (dbManager->addKosguEntry(new_kosgu)) {
                id = new_kosgu.id;
            }
        }
        kosgu_cache[code] = id;
        return id;
    };

    size_t line_num = 0;
    while (std::getline(file, line)) {
        // Check for cancellation
        if (cancel_flag) {
            transaction.rollback();
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

        std::string amount_str = get_value_from_row(row, mapping, "Сумма");
        if (!parseAmount(amount_str, payment.amount)) {
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


        std::string counterparty_name;
        if (payment.type) { // true is income
            counterparty_name = local_payer_name;
        } else {
            counterparty_name = payment.recipient;
        }
        int counterparty_id = get_or_create_counterparty_id(counterparty_name);
        payment.counterparty_id = counterparty_id;

        int current_contract_id = -1;
        std::smatch contract_matches;
        if (std::regex_search(payment.description, contract_matches,
                              contract_regex)) {
            if (contract_matches.size() >= 3) {
                std::string contract_number = contract_matches[1].str();
                std::string contract_date_db_format =
                    convertDateToDBFormat(contract_matches[2].str());
                current_contract_id = get_or_create_contract_id(
                    contract_number, contract_date_db_format, counterparty_id);
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
            std::regex special_kosgu_regex("К(\\d{3})=([\\d\\s,=.-]+)");
            auto details_begin = std::sregex_iterator(details_part.begin(), details_part.end(), special_kosgu_regex);
            auto details_end = std::sregex_iterator();
            
            std::vector<PaymentDetail> details_to_add;
            double total_details_amount = 0.0;
            
            if (std::distance(details_begin, details_end) > 0) {
                for (std::sregex_iterator i = details_begin; i != details_end; ++i) {
                    std::smatch match = *i;
                    std::string kosgu_code = match[1].str();
                    std::string amount_str = match[2].str();
                    
                    int kosgu_id = get_or_create_kosgu_id(kosgu_code);

                    double detail_amount = 0.0;
                    if (parseAmount(amount_str, detail_amount)) {
                        total_details_amount += detail_amount;
                        
                        PaymentDetail detail;
                        detail.payment_id = new_payment_id;
                        detail.kosgu_id = kosgu_id;
                        detail.contract_id = current_contract_id;
                        detail.amount = detail_amount;
                        details_to_add.push_back(detail);
                    } else {
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
                    kosgu_id_from_regex = get_or_create_kosgu_id(kosgu_code);
                }
            }
            detail.kosgu_id = kosgu_id_from_regex;
            // --- END FIX ---

            detail.contract_id = current_contract_id;
            detail.amount = payment.amount;
            dbManager->addPaymentDetail(detail);
        }
    }

    if (!transaction.commit()) {
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

    DatabaseManager::TransactionGuard transaction(*dbManager);
    if (!transaction.started()) {
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

    if (!transaction.commit()) {
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

JournalOrder4DryRunResult ImportManager::AnalyzeJournalOrder4FromTsv(
    const std::string& filepath,
    const ColumnMapping& mapping
) {
    JournalOrder4DryRunResult result;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        result.sample_errors.push_back("Не удалось открыть файл: " + filepath);
        return result;
    }

    std::string line;
    if (!std::getline(file, line)) {
        result.sample_errors.push_back("Файл пуст или нечитаем.");
        return result;
    }

    char delimiter = detectDelimiter(line);
    std::set<std::string> document_keys;
    std::set<std::string> duplicate_keys_seen;

    int line_num = 1;
    while (std::getline(file, line)) {
        line_num++;
        if (line.empty()) {
            continue;
        }

        result.total_rows++;
        std::vector<std::string> row = split(line, delimiter);

        std::string doc_date = get_jo4_value(row, mapping, "Дата документа");
        std::string doc_number =
            get_jo4_value(row, mapping, "Номер документа");
        std::string doc_name =
            get_jo4_value(row, mapping, "Наименование документа");
        std::string counterparty_name =
            get_jo4_value(row, mapping, "Наименование показателя");
        std::string debit_account =
            get_jo4_value(row, mapping, "Счет дебет");
        std::string amount_str = get_jo4_value(row, mapping, "Сумма");

        bool row_valid = true;
        if (doc_number.empty()) {
            result.missing_number_rows++;
            row_valid = false;
        }
        if (doc_date.empty()) {
            result.missing_date_rows++;
            row_valid = false;
        }

        double amount = 0.0;
        if (!parseAmount(amount_str, amount)) {
            result.invalid_amount_rows++;
            row_valid = false;
            if (result.sample_errors.size() < 8) {
                result.sample_errors.push_back(
                    "Строка " + std::to_string(line_num) +
                    ": неверная сумма '" + amount_str + "'");
            }
        }

        if (extractKosguCodeFromAccount(debit_account).empty()) {
            result.missing_kosgu_rows++;
        }

        std::string date_db =
            doc_date.length() >= 8 ? convertDateToDBFormat(doc_date) : doc_date;
        std::string key =
            doc_number + "|" + date_db + "|" + doc_name + "|" + counterparty_name;
        if (!doc_number.empty() && !date_db.empty()) {
            if (!document_keys.insert(key).second &&
                duplicate_keys_seen.insert(key).second) {
                result.duplicate_document_rows++;
            }
        }

        if (row_valid) {
            result.valid_rows++;
        }
    }

    result.document_keys = static_cast<int>(document_keys.size());
    return result;
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
    char delimiter = detectDelimiter(line);

    importedDocuments = 0;
    importedDetails = 0;
    errors.clear();
    size_t line_num = 0;

    // Кэши на время импорта, чтобы не делать одинаковые SELECT на каждой строке.
    std::map<std::string, int> doc_cache;
    std::map<std::string, int> kosgu_cache;

    auto get_or_create_kosgu_id = [&](const std::string& code) {
        if (code.empty()) {
            return -1;
        }

        auto cache_it = kosgu_cache.find(code);
        if (cache_it != kosgu_cache.end()) {
            return cache_it->second;
        }

        int id = dbManager->getKosguIdByCode(code);
        if (id == -1) {
            Kosgu new_kosgu{-1, code, "КОСГУ " + code};
            if (dbManager->addKosguEntry(new_kosgu)) {
                id = new_kosgu.id;
            }
        }
        kosgu_cache[code] = id;
        return id;
    };

    DatabaseManager::TransactionGuard transaction(*dbManager);
    if (!transaction.started()) {
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
            transaction.rollback();
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

        std::vector<std::string> row = split(line, delimiter);

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
        if (!parseAmount(amount_str, amount)) {
            errors.push_back("Строка " + std::to_string(line_num) + ": неверная сумма '" + amount_str + "'");
            continue;
        }

        // Поиск или создание документа основания
        int doc_id = -1;
        std::string cache_key =
            doc_number + "|" + date_db + "|" + doc_name + "|" + counterparty_name;

        auto cache_it = doc_cache.find(cache_key);
        if (cache_it != doc_cache.end()) {
            doc_id = cache_it->second;
        } else {
            doc_id = dbManager->getBasePaymentDocumentIdBySignature(
                doc_number, date_db, doc_name, counterparty_name);
            if (doc_id == -1 && !doc_name.empty() && !counterparty_name.empty()) {
                doc_id = dbManager->getBasePaymentDocumentIdByNumberDate(
                    doc_number, date_db);
            }
            if (doc_id != -1) {
                doc_cache[cache_key] = doc_id;
            }
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
                std::string kosgu_code = extractKosguCodeFromAccount(debit_account);
                if (!kosgu_code.empty()) {
                    new_detail.kosgu_id = get_or_create_kosgu_id(kosgu_code);
                }
            }

            int existing_detail_id =
                dbManager->getBasePaymentDocumentDetailIdBySignature(new_detail);
            if (existing_detail_id != -1) {
                continue;
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

    if (!transaction.commit()) {
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
