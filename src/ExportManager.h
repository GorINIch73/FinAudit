#pragma once

#include "DatabaseManager.h"
#include <string>
#include <vector>

struct ContractExportData {
    std::string contract_number;
    std::string contract_date;
    std::string counterparty_name;
    std::string kosgu_codes; // Comma-separated list
    bool is_for_special_control;
    std::string note;
    std::string procurement_code; // ИКЗ
};

class ExportManager {
public:
    ExportManager(DatabaseManager* db);
    int ExportContractsForChecking(const std::string& filepath);

private:
    DatabaseManager* dbManager;
};
