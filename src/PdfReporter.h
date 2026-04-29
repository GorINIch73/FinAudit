#pragma once

#include <string>
#include <vector>
#include "Settings.h"         // Added missing include
#include "ExportManager.h"    // Added missing include

class PdfReporter {
public:
    PdfReporter();

    // Placeholder method to generate a PDF from tabular data
    bool generatePdfFromTable(
        const std::string& filename,
        const std::string& title,
        const std::vector<std::string>& columns,
        const std::vector<std::vector<std::string>>& rows
    );

    bool generateContractsReport(const std::string& filename, const Settings& settings, const std::vector<ContractExportData>& contracts);
};
