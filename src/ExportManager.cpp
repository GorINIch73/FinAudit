#include "ExportManager.h"
#include <fstream>
#include <iostream>

// Helper to escape CSV fields if they contain quotes or commas
static std::string escape_csv(const std::string& s) {
    if (s.find('"') != std::string::npos || s.find(',') != std::string::npos) {
        std::string escaped = "\"";
        for (char c : s) {
            if (c == '"') {
                escaped += "\"\"";
            } else {
                escaped += c;
            }
        }
        escaped += "\"";
        return escaped;
    }
    return s;
}


ExportManager::ExportManager(DatabaseManager* db) : dbManager(db) {}

int ExportManager::ExportContractsForChecking(const std::string& filepath) {
    if (!dbManager) {
        return 0;
    }

    std::vector<ContractExportData> contracts = dbManager->getContractsForExport();

    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filepath << std::endl;
        return 0;
    }
    
    // Set UTF-8 BOM for better Excel compatibility
    file << "\xEF\xBB\xBF";

    // Write header
    file << "\"N п/п\","
         << "\"Номер договора\","
         << "\"Дата договора\","
         << "\"Наименование контрагента\","
         << "\"Коды КОСГУ\","
         << "\"Признак усиленного контроля\","
         << "\"Примечание\","
         << "\"ИКЗ\"\n";

    int index = 1;
    for (const auto& contract : contracts) {
        file << index++ << ","
             << escape_csv("\xE2\x80\x8B" + contract.contract_number) << "," // Prepend ZWSP
             << escape_csv(contract.contract_date) << ","
             << escape_csv(contract.counterparty_name) << ","
             << escape_csv(contract.kosgu_codes) << ","
             << (contract.is_for_special_control ? "\"Да\"" : "\"Нет\"") << ","
             << escape_csv(contract.note) << ","
             << escape_csv("\xE2\x80\x8B" + contract.procurement_code) << "\n"; // Prepend ZWSP
    }

    file.close();
    return contracts.size();
}
