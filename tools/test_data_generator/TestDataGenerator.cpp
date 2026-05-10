#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path bankPath = "bank_test.tsv";
    std::filesystem::path jo4Path = "jo4_test.tsv";
    int count = 400;
    uint32_t seed = 0;
    bool seedProvided = false;
};

struct GeneratedRow {
    int index = 0;
    std::string paymentDate;
    std::string paymentDocNumber;
    std::string paymentType;
    double amount = 0.0;
    std::string counterparty;
    std::string contractNumber;
    std::string contractDateDmy;
    std::string contractDateDb;
    std::string documentName;
    std::string documentNumber;
    std::string documentDateDmy;
    std::string kosguCode;
    std::string kosguName;
    int descriptionVariant = 0;
    bool suspicious = false;
    bool advance = false;
    bool splitJo4 = false;
    bool misuse = false;
};

struct ContractTemplate {
    std::string counterparty;
    std::string number;
    std::string dateDmy;
    std::string dateDb;
};

const std::vector<std::string>& generalCounterparties() {
    static const std::vector<std::string> values = {
        "ООО Медснаб Регион",
        "АО Фармкомплект",
        "ООО Чистая Клиника",
        "ИП Петров Петр Петрович",
        "ООО Техсервис",
        "АО Лабораторные системы",
        "ООО Питание Плюс",
        "ООО Стройремонт",
        "ГБУЗ Городская больница N 7",
        "ООО Диагностика Север",
        "АО Медицинские технологии",
        "ООО Сервис стерилизации",
        "ООО Транспорт-Мед",
        "ИП Сидорова Анна Ивановна",
        "ПАО Сбербанк",
        "Банк ВТБ (ПАО)",
        "АО Газпромбанк",
        "АО Россельхозбанк",
        "ПАО Совкомбанк",
        "АО Альфа-Банк",
        "Управление Федерального казначейства",
        "Отделение Фонда пенсионного и социального страхования",
        "ПАО Ростелеком",
        "АО Почта России",
        "ООО Городские коммунальные системы",
        "АО Энергосбыт Плюс",
        "МУП Водоканал",
        "ООО Теплосеть-Сервис",
    };
    return values;
}

const std::vector<std::string>& bankCounterparties() {
    static const std::vector<std::string> values = {
        "ПАО Сбербанк",
        "Банк ВТБ (ПАО)",
        "АО Газпромбанк",
        "АО Россельхозбанк",
        "ПАО Совкомбанк",
        "АО Альфа-Банк",
    };
    return values;
}

const std::vector<std::string>& treasuryCounterparties() {
    static const std::vector<std::string> values = {
        "Управление Федерального казначейства",
        "Отделение Фонда пенсионного и социального страхования",
    };
    return values;
}

const std::vector<std::string>& telecomCounterparties() {
    static const std::vector<std::string> values = {
        "ПАО Ростелеком",
        "АО Почта России",
        "ООО Телеком-Мед",
        "АО СвязьИнформ",
    };
    return values;
}

const std::vector<std::string>& utilityCounterparties() {
    static const std::vector<std::string> values = {
        "ООО Городские коммунальные системы",
        "АО Энергосбыт Плюс",
        "МУП Водоканал",
        "ООО Теплосеть-Сервис",
    };
    return values;
}

const std::vector<std::pair<std::string, std::string>>& kosguEntries() {
    static const std::vector<std::pair<std::string, std::string>> values = {
        {"211", "Заработная плата"},
        {"213", "Начисления на выплаты по оплате труда"},
        {"221", "Услуги связи"},
        {"223", "Коммунальные услуги"},
        {"340", "Увеличение стоимости материальных запасов"},
        {"225", "Работы, услуги по содержанию имущества"},
        {"226", "Прочие работы, услуги"},
        {"310", "Увеличение стоимости основных средств"},
        {"341", "Лекарственные препараты"},
        {"342", "Продукты питания"},
        {"346", "Прочие оборотные запасы"},
        {"344", "Строительные материалы"},
        {"349", "Прочие материальные запасы однократного применения"},
    };
    return values;
}

std::filesystem::path withDefaultTsvExtension(std::filesystem::path path) {
    if (!path.has_extension()) {
        path += ".tsv";
    }
    return path;
}

bool parseInt(const std::string& text, int& value) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    int parsed = 0;
    auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last || parsed <= 0) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseUInt32(const std::string& text, uint32_t& value) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    uint32_t parsed = 0;
    auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    value = parsed;
    return true;
}

void printUsage(const char* exeName) {
    std::cout
        << "Usage:\n"
        << "  " << exeName << " [--bank BANK_NAME] [--jo4 JO4_NAME] [--count N] [--seed N]\n"
        << "  " << exeName << " BANK_NAME JO4_NAME [N]\n"
        << "  " << exeName << " BASE_NAME [N]\n\n"
        << "Defaults: BANK_NAME=bank_test.tsv, JO4_NAME=jo4_test.tsv, N=400\n"
        << "If an output name has no extension, .tsv is added automatically.\n"
        << "Use --seed to reproduce the same generated dataset.\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto needValue = [&](const std::string& option) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << option << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        if (arg == "--bank") {
            const char* value = needValue(arg);
            if (!value) return false;
            options.bankPath = value;
            continue;
        }
        if (arg == "--jo4") {
            const char* value = needValue(arg);
            if (!value) return false;
            options.jo4Path = value;
            continue;
        }
        if (arg == "-n" || arg == "--count") {
            const char* value = needValue(arg);
            if (!value) return false;
            if (!parseInt(value, options.count)) {
                std::cerr << "Invalid count: " << value << "\n";
                return false;
            }
            continue;
        }
        if (arg == "--seed") {
            const char* value = needValue(arg);
            if (!value) return false;
            if (!parseUInt32(value, options.seed)) {
                std::cerr << "Invalid seed: " << value << "\n";
                return false;
            }
            options.seedProvided = true;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
        positional.push_back(arg);
    }

    if (positional.size() == 1) {
        int count = 0;
        if (parseInt(positional[0], count)) {
            options.count = count;
        } else {
            std::filesystem::path base = positional[0];
            options.bankPath = base.string() + "_bank";
            options.jo4Path = base.string() + "_jo4";
        }
    } else if (positional.size() == 2) {
        int count = 0;
        if (parseInt(positional[1], count)) {
            std::filesystem::path base = positional[0];
            options.bankPath = base.string() + "_bank";
            options.jo4Path = base.string() + "_jo4";
            options.count = count;
        } else {
            options.bankPath = positional[0];
            options.jo4Path = positional[1];
        }
    } else if (positional.size() == 3) {
        options.bankPath = positional[0];
        options.jo4Path = positional[1];
        if (!parseInt(positional[2], options.count)) {
            std::cerr << "Invalid count: " << positional[2] << "\n";
            return false;
        }
    } else if (positional.size() > 3) {
        std::cerr << "Too many positional arguments.\n";
        return false;
    }

    options.bankPath = withDefaultTsvExtension(options.bankPath);
    options.jo4Path = withDefaultTsvExtension(options.jo4Path);
    return true;
}

int randomInt(std::mt19937& rng, int minValue, int maxValue) {
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(rng);
}

bool randomChance(std::mt19937& rng, int percent) {
    return randomInt(rng, 1, 100) <= percent;
}

template <typename T>
const T& pickRandom(std::mt19937& rng, const std::vector<T>& values) {
    return values[static_cast<size_t>(randomInt(rng, 0, static_cast<int>(values.size() - 1)))];
}

std::string twoDigits(int value) {
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << value;
    return out.str();
}

std::string formatDateDmy(int year, int month, int day) {
    return twoDigits(day) + "." + twoDigits(month) + "." + std::to_string(year);
}

std::string formatDateDb(int year, int month, int day) {
    return std::to_string(year) + "-" + twoDigits(month) + "-" + twoDigits(day);
}

std::string formatAmount(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    std::string text = out.str();
    std::replace(text.begin(), text.end(), '.', ',');
    return text;
}

std::string cleanTsvCell(std::string value) {
    for (char& ch : value) {
        if (ch == '\t' || ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return value;
}

void writeTsvRow(std::ofstream& file, const std::vector<std::string>& cells) {
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i > 0) {
            file << '\t';
        }
        file << cleanTsvCell(cells[i]);
    }
    file << '\n';
}

std::vector<ContractTemplate> makeContractPool(int count, std::mt19937& rng) {
    std::vector<ContractTemplate> contracts;
    int maxContractsPerCounterparty = std::clamp(count / 40 + 2, 2, 20);
    int serial = 1000;

    for (const auto& counterparty : generalCounterparties()) {
        int contractCount = randomInt(rng, 2, maxContractsPerCounterparty);
        for (int i = 0; i < contractCount; ++i) {
            int year = randomInt(rng, 2023, 2026);
            int month = randomInt(rng, 1, 12);
            int day = randomInt(rng, 1, 28);
            ContractTemplate contract;
            contract.counterparty = counterparty;
            contract.number =
                "Д-" + std::to_string(year) + "-" + std::to_string(serial++);
            contract.dateDmy = formatDateDmy(year, month, day);
            contract.dateDb = formatDateDb(year, month, day);
            contracts.push_back(contract);
        }
    }

    return contracts;
}

std::vector<const ContractTemplate*> filterContractsByCounterparties(
    const std::vector<ContractTemplate>& contracts,
    const std::vector<std::string>& counterparties) {
    std::vector<const ContractTemplate*> result;
    for (const auto& contract : contracts) {
        if (std::find(counterparties.begin(), counterparties.end(),
                      contract.counterparty) != counterparties.end()) {
            result.push_back(&contract);
        }
    }
    return result;
}

const ContractTemplate& pickContract(
    std::mt19937& rng, const std::vector<ContractTemplate>& contracts,
    const std::vector<std::string>& preferredCounterparties) {
    std::vector<const ContractTemplate*> preferred =
        filterContractsByCounterparties(contracts, preferredCounterparties);
    if (!preferred.empty()) {
        return *preferred[static_cast<size_t>(
            randomInt(rng, 0, static_cast<int>(preferred.size() - 1)))];
    }
    return contracts[static_cast<size_t>(
        randomInt(rng, 0, static_cast<int>(contracts.size() - 1)))];
}

GeneratedRow makeRow(int index, std::mt19937& rng,
                     const std::vector<ContractTemplate>& contracts,
                     int documentPoolSize) {
    static const std::vector<std::string> docTypes = {
        "УПД", "Акт", "Счет", "Документ о приемке", "Накладная"};

    int year = randomInt(rng, 2024, 2026);
    int month = randomInt(rng, 1, 12);
    int day = randomInt(rng, 1, 28);
    int docMonth = randomChance(rng, 75) ? month : randomInt(rng, 1, 12);
    int docDay = randomInt(rng, 1, 28);

    const auto& kosguEntry = pickRandom(rng, kosguEntries());
    int documentId = randomInt(rng, 1, std::max(1, documentPoolSize));
    const ContractTemplate* contract = nullptr;
    if (kosguEntry.first == "211") {
        contract = &pickContract(rng, contracts, bankCounterparties());
    } else if (kosguEntry.first == "213") {
        contract = &pickContract(rng, contracts, treasuryCounterparties());
    } else if (kosguEntry.first == "221") {
        contract = &pickContract(rng, contracts, telecomCounterparties());
    } else if (kosguEntry.first == "223") {
        contract = &pickContract(rng, contracts, utilityCounterparties());
    } else {
        contract = &contracts[static_cast<size_t>(
            randomInt(rng, 0, static_cast<int>(contracts.size() - 1)))];
    }

    GeneratedRow row;
    row.index = index;
    row.paymentDate = formatDateDmy(year, month, day);
    row.paymentDocNumber = "ПП-" + std::to_string(100000 + index);
    row.paymentType = randomChance(rng, 8) ? "Поступление" : "Расход";
    row.amount = randomInt(rng, 30000, 95000000) / 100.0;
    if (row.paymentType == "Поступление") {
        row.amount = randomInt(rng, 100000, 6000000) / 100.0;
    }
    row.counterparty = contract->counterparty;
    row.contractNumber = contract->number;
    row.contractDateDmy = contract->dateDmy;
    row.contractDateDb = contract->dateDb;
    row.documentName = pickRandom(rng, docTypes);
    row.documentNumber = "ДОК-" + std::to_string(50000 + documentId);
    row.documentDateDmy = formatDateDmy(year, docMonth, docDay);
    row.kosguCode = kosguEntry.first;
    row.kosguName = kosguEntry.second;
    row.descriptionVariant = randomInt(rng, 0, 4);
    row.suspicious = randomChance(rng, 4);
    row.advance = randomChance(rng, 7);
    row.splitJo4 = randomChance(rng, 12);
    row.misuse = randomChance(rng, 5);
    if (row.misuse) {
        row.suspicious = true;
    }
    return row;
}

std::vector<GeneratedRow> makeRows(int count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<GeneratedRow> rows;
    rows.reserve(static_cast<size_t>(count));
    std::vector<ContractTemplate> contracts = makeContractPool(count, rng);
    int documentPoolSize = std::max(30, count * 4 / 5);
    for (int i = 1; i <= count; ++i) {
        rows.push_back(makeRow(i, rng, contracts, documentPoolSize));
    }
    std::shuffle(rows.begin(), rows.end(), rng);
    return rows;
}

std::string paymentDescription(const GeneratedRow& row) {
    std::ostringstream out;
    out << "(000-0000-0000000000-244: 4788=35 ЛС 105700347) К"
        << row.kosguCode << " ";
    if (row.misuse) {
        static const std::vector<std::string> misuseTexts = {
            "капитальный ремонт помещений административного корпуса",
            "ремонт кабинета главного врача и закупка мебели",
            "благоустройство территории и укладка плитки",
            "монтаж системы видеонаблюдения вне утвержденной сметы",
            "приобретение строительных материалов для капитального ремонта",
        };
        const std::string& misuseText =
            misuseTexts[static_cast<size_t>(row.index) % misuseTexts.size()];
        out << "Оплата: " << misuseText << "; договор N "
            << row.contractNumber << " от " << row.contractDateDmy << "; "
            << row.documentName << " N " << row.documentNumber << " от "
            << row.documentDateDmy << "; КОСГУ " << row.kosguCode
            << "; возможное нецелевое использование";
        return out.str();
    }

    switch (row.descriptionVariant) {
    case 1:
        out << row.documentName << " " << row.documentNumber << " от "
            << row.documentDateDmy << ", договор " << row.contractNumber
            << " от " << row.contractDateDmy << ", КОСГУ " << row.kosguCode;
        break;
    case 2:
        out << "За " << row.kosguName << " по дог. N " << row.contractNumber
            << " от " << row.contractDateDmy << "; документ о приемке N "
            << row.documentNumber << " от " << row.documentDateDmy;
        break;
    case 3:
        out << "Оплата счета " << row.documentNumber << " от "
            << row.documentDateDmy << " к контракту " << row.contractNumber
            << " от " << row.contractDateDmy << "; " << row.counterparty;
        break;
    case 4:
        out << "Возмещение расходов, " << row.documentName << " N "
            << row.documentNumber << ", договор N " << row.contractNumber
            << " от " << row.contractDateDmy << ", статья " << row.kosguCode;
        break;
    default:
        out << "Оплата по договору N " << row.contractNumber << " от "
            << row.contractDateDmy << "; " << row.documentName << " N "
            << row.documentNumber << " от " << row.documentDateDmy
            << "; КОСГУ " << row.kosguCode;
        break;
    }
    out << "; тестовая нагрузочная строка " << row.index;
    if (row.suspicious) {
        out << "; срочно проверить";
    }
    if (row.advance) {
        out << "; аванс";
    }
    if (row.kosguCode == "211") {
        out << "; заработная плата через банк";
    } else if (row.kosguCode == "213") {
        out << "; страховые взносы через казначейство";
    } else if (row.kosguCode == "221") {
        out << "; услуги связи";
    } else if (row.kosguCode == "223") {
        out << "; коммунальные услуги";
    }
    return out.str();
}

std::string debitAccount(const GeneratedRow& row) {
    return "401.20." + row.kosguCode;
}

std::string creditAccount(int index) {
    return "302." + std::to_string(20 + (index % 9)) + ".730";
}

bool ensureParentDirectory(const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    return !ec;
}

bool generateBankFile(const std::filesystem::path& path,
                      const std::vector<GeneratedRow>& rows) {
    if (!ensureParentDirectory(path)) {
        std::cerr << "Cannot create directory for " << path << "\n";
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot write " << path << "\n";
        return false;
    }

    writeTsvRow(file, {"Дата", "Номер док.", "Тип", "Сумма", "Контрагент",
                       "Назначение", "Примечание"});
    for (const auto& row : rows) {
        writeTsvRow(file,
                    {row.paymentDate,
                     row.paymentDocNumber,
                     row.paymentType,
                     formatAmount(row.amount),
                     row.counterparty,
                     paymentDescription(row),
                     "test-bank-row-" + std::to_string(row.index)});
    }
    return true;
}

bool generateJo4File(const std::filesystem::path& path,
                     const std::vector<GeneratedRow>& rows) {
    if (!ensureParentDirectory(path)) {
        std::cerr << "Cannot create directory for " << path << "\n";
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot write " << path << "\n";
        return false;
    }

    writeTsvRow(file, {"Дата документа", "Номер документа",
                       "Наименование документа", "Наименование показателя",
                       "Содержание операции", "Счет дебет", "Счет кредит",
                       "Сумма"});
    for (const auto& row : rows) {
        std::ostringstream content;
        content << row.documentName << " N " << row.documentNumber
                << " по договору N " << row.contractNumber << " от "
                << row.contractDateDmy << "; " << row.kosguName;

        writeTsvRow(file,
                    {row.documentDateDmy,
                     row.documentNumber,
                     row.documentName,
                     row.counterparty,
                         content.str(),
                         debitAccount(row),
                         creditAccount(row.index),
                         formatAmount(row.amount)});

        if (row.splitJo4) {
            GeneratedRow extra = row;
            extra.amount = row.amount * (row.index % 2 == 0 ? 0.15 : 0.25);
            writeTsvRow(file,
                        {row.documentDateDmy,
                         row.documentNumber,
                         row.documentName,
                         row.counterparty,
                         "Дополнительная строка детализации; КОСГУ " +
                             row.kosguCode,
                         debitAccount(row),
                         creditAccount(row.index + 1000),
                         formatAmount(extra.amount)});
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        return argc > 1 && (std::string(argv[1]) == "-h" ||
                            std::string(argv[1]) == "--help")
                   ? 0
                   : 1;
    }

    if (!options.seedProvided) {
        options.seed = static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count());
    }

    std::vector<GeneratedRow> rows = makeRows(options.count, options.seed);
    bool bankOk = generateBankFile(options.bankPath, rows);
    bool jo4Ok = generateJo4File(options.jo4Path, rows);

    if (!bankOk || !jo4Ok) {
        return 1;
    }

    std::cout << "Generated bank TSV: " << options.bankPath.string() << "\n";
    std::cout << "Generated JO4 TSV:  " << options.jo4Path.string() << "\n";
    std::cout << "Rows per file:      " << options.count << "\n";
    std::cout << "Seed:               " << options.seed << "\n";
    return 0;
}
