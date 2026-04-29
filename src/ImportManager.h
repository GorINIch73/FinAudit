#pragma once

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include "DatabaseManager.h"

// Represents the mapping from a target field name (e.g., "Дата") 
// to the index of the column in the source file.
using ColumnMapping = std::map<std::string, int>;

struct UnfoundContract {
    std::string number;
    std::string date;
    std::string ikz;
};

class ImportManager {
public:
    ImportManager();

    bool importIKZFromFile(
        const std::string& filepath,
        DatabaseManager* dbManager,
        std::vector<UnfoundContract>& unfoundContracts,
        int& successfulImports,
        std::atomic<float>& progress,
        std::string& message,
        std::mutex& message_mutex
    );

    // Imports payments from a TSV file using a user-defined column mapping.
        bool ImportPaymentsFromTsv(
            const std::string& filepath,
            DatabaseManager* dbManager,
            const ColumnMapping& mapping,
            std::atomic<float>& progress,
            std::string& message,
            std::mutex& message_mutex,
            std::atomic<bool>& cancel_flag, // New parameter
            const std::string& contract_regex,
            const std::string& kosgu_regex,
            bool force_income_type,
            bool is_return_import,
            const std::string& custom_note
        );

    // Импорт журнала ордера №4 из TSV
    bool ImportJournalOrder4FromTsv(
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
    );
};
