#include "ContractRegistryNumbersView.h"
#include "../UIManager.h"
#include "../ExportManager.h"
#include "../IconsFontAwesome6.h"
#include "../PlatformUtils.h"
#include "imgui.h"
#include "ImGuiFileDialog.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

static std::vector<std::string> split_registry_row(const std::string& s, char delimiter) {
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

static char detect_registry_delimiter(const std::string& header_line) {
    size_t tab_count = std::count(header_line.begin(), header_line.end(), '\t');
    size_t semicolon_count = std::count(header_line.begin(), header_line.end(), ';');
    size_t comma_count = std::count(header_line.begin(), header_line.end(), ',');

    if (tab_count >= semicolon_count && tab_count >= comma_count && tab_count > 0) {
        return '\t';
    }
    if (semicolon_count >= comma_count && semicolon_count > 0) {
        return ';';
    }
    return ',';
}

static std::string normalize_registry_header(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }

    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char c : value) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '.' ||
            c == '-' || c == '_' || c == '/' || c == '\\') {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    return normalized;
}

static void apply_registry_auto_mapping(const std::vector<std::string>& headers,
                                        ColumnMapping& mapping) {
    std::map<std::string, std::vector<std::string>> aliases = {
        {"Номер договора",
         {"Номер договора", "Номер контракта", "номер договора",
          "номер контракта", "номер"}},
        {"Дата договора",
         {"Дата договора", "Дата контракта", "дата договора",
          "дата контракта", "дата"}},
        {"Реестровый номер",
         {"Реестровый номер", "Реестровый номер контракта", "ИКЗ",
          "Код закупки", "Номер закупки", "реестровый номер",
          "реестровый номер контракта", "икз", "код закупки",
          "номер закупки"}},
        {"Сумма договора",
         {"Сумма договора", "Сумма контракта", "сумма договора",
          "сумма контракта", "сумма"}},
    };

    std::vector<std::string> normalized_headers;
    normalized_headers.reserve(headers.size());
    for (const auto& header : headers) {
        normalized_headers.push_back(normalize_registry_header(header));
    }

    for (const auto& [target, target_aliases] : aliases) {
        if (mapping[target] != -1) {
            continue;
        }
        for (const auto& alias : target_aliases) {
            std::string normalized_alias = normalize_registry_header(alias);
            auto it = std::find(normalized_headers.begin(), normalized_headers.end(),
                                normalized_alias);
            if (it != normalized_headers.end()) {
                mapping[target] =
                    static_cast<int>(std::distance(normalized_headers.begin(), it));
                break;
            }
        }
    }
}

ContractRegistryNumbersView::ContractRegistryNumbersView() {
    Title = "Реестровые номера контрактов";
    Reset();
}

void ContractRegistryNumbersView::SetUIManager(UIManager* manager) {
    uiManager = manager;
}

void ContractRegistryNumbersView::SetExportManager(ExportManager* manager) {
    exportManager = manager;
}

void ContractRegistryNumbersView::Reset() {
    m_unfoundContracts.clear();
    m_successfulImports = 0;
    m_ikzImportStarted = false;
    m_showUnfoundContracts = false;
    m_lastExportCount = -1;
    m_importFilePath.clear();
    m_fileHeaders.clear();
    m_sampleData.clear();
    m_currentMapping.clear();
    for (const auto& field : m_targetFields) {
        m_currentMapping[field] = -1;
    }
    m_showImportMapping = false;
}

std::string ContractRegistryNumbersView::GetDatabaseDirectory() const {
    if (!uiManager || uiManager->currentDbPath.empty()) {
        return ".";
    }

    std::filesystem::path dbPath(uiManager->currentDbPath);
    std::filesystem::path parent = dbPath.parent_path();
    if (parent.empty()) {
        return ".";
    }

    return parent.string();
}

void ContractRegistryNumbersView::StartIKZImport(const std::string& filePath, ImportManager* importManager,
                                  DatabaseManager* dbManager, const ColumnMapping& mapping,
                                  std::atomic<float>& progress,
                                  std::string& message, std::mutex& mutex, std::atomic<bool>& isImporting) {
    std::string backupPath;
    if (!uiManager || !uiManager->BackupCurrentDatabase("import_registry_numbers", backupPath)) {
        std::lock_guard<std::mutex> lock(mutex);
        progress = 0.0f;
        message = "Ошибка: не удалось создать резервную копию перед импортом реестровых номеров.";
        return;
    }

    m_ikzImportStarted = true;
    isImporting = true;
    m_showUnfoundContracts = true;
    m_unfoundContracts.clear();
    m_successfulImports = 0;

    ColumnMapping importMapping = mapping;
    std::thread([this, filePath, importManager, dbManager, importMapping, &progress, &message, &mutex, &isImporting, backupPath]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            progress = 0.0f;
            message = "Резервная копия создана: " + backupPath;
        }
        importManager->importIKZFromFile(
            filePath,
            dbManager,
            importMapping,
            m_unfoundContracts,
            m_successfulImports,
            progress,
            message,
            mutex
        );
        m_ikzImportStarted = false;
        isImporting = false;
    }).detach();
}

void ContractRegistryNumbersView::OpenImportMapping(const std::string& filePath) {
    m_importFilePath = filePath;
    m_fileHeaders.clear();
    m_sampleData.clear();
    m_currentMapping.clear();
    for (const auto& field : m_targetFields) {
        m_currentMapping[field] = -1;
    }
    ReadPreviewData();
    m_showImportMapping = true;
    IsVisible = true;
}

void ContractRegistryNumbersView::ReadPreviewData() {
    if (m_importFilePath.empty()) {
        return;
    }

    std::ifstream file;
    platformOpenInputFile(file, m_importFilePath);
    if (!file.is_open()) {
        return;
    }

    std::string headerLine;
    char delimiter = '\t';
    if (std::getline(file, headerLine)) {
        delimiter = detect_registry_delimiter(headerLine);
        m_fileHeaders = split_registry_row(headerLine, delimiter);
        apply_registry_auto_mapping(m_fileHeaders, m_currentMapping);
    }

    int lines_to_read = 20;
    if (dbManager) {
        lines_to_read = dbManager->getSettings().import_preview_lines;
    }

    std::string dataLine;
    int line_count = 0;
    while (std::getline(file, dataLine) && line_count < lines_to_read) {
        m_sampleData.push_back(split_registry_row(dataLine, delimiter));
        line_count++;
    }
}

void ContractRegistryNumbersView::RenderImportMapping() {
    if (!m_showImportMapping) {
        return;
    }

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Файл: %s", m_importFilePath.c_str());
    ImGui::TextUnformatted("Укажите соответствие столбцов файла полям реестровых номеров.");

    if (ImGui::BeginTable("registry_mapping_table", 2, ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Поле в программе", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Столбец из файла");
        ImGui::TableHeadersRow();

        for (const auto& targetField : m_targetFields) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(targetField.c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(targetField.c_str());

            const char* current_item =
                (m_currentMapping[targetField] >= 0 &&
                 m_currentMapping[targetField] < static_cast<int>(m_fileHeaders.size()))
                    ? m_fileHeaders[m_currentMapping[targetField]].c_str()
                    : "Не выбрано";

            if (ImGui::BeginCombo("", current_item)) {
                bool is_selected = (m_currentMapping[targetField] == -1);
                if (ImGui::Selectable("Не выбрано", is_selected)) {
                    m_currentMapping[targetField] = -1;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }

                for (int i = 0; i < static_cast<int>(m_fileHeaders.size()); ++i) {
                    is_selected = (m_currentMapping[targetField] == i);
                    if (ImGui::Selectable(m_fileHeaders[i].c_str(), is_selected)) {
                        m_currentMapping[targetField] = i;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Предпросмотр данных (первые %d строк):", static_cast<int>(m_sampleData.size()));
    if (ImGui::BeginChild("RegistryPreviewScrollRegion", ImVec2(0, 180), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        if (!m_fileHeaders.empty() &&
            ImGui::BeginTable("registry_preview_table", static_cast<int>(m_fileHeaders.size()) + 1,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            for (const auto& header : m_fileHeaders) {
                ImGui::TableSetupColumn(header.c_str());
            }
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(m_sampleData.size()); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);
                for (const auto& cell : m_sampleData[i]) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(cell.c_str());
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    bool can_import = m_currentMapping["Номер договора"] >= 0 &&
                      m_currentMapping["Дата договора"] >= 0 &&
                      m_currentMapping["Реестровый номер"] >= 0;

    ImGui::BeginDisabled(!can_import || uiManager->isImporting);
    if (ImGui::Button(ICON_FA_FILE_IMPORT " Начать импорт")) {
        StartIKZImport(m_importFilePath, uiManager->importManager, dbManager,
                       m_currentMapping, uiManager->importProgress,
                       uiManager->importMessage, uiManager->importMutex,
                       uiManager->isImporting);
        m_showImportMapping = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Отмена импорта")) {
        m_showImportMapping = false;
        m_importFilePath.clear();
        m_fileHeaders.clear();
        m_sampleData.clear();
    }
}

void ContractRegistryNumbersView::StartContractsExport(const std::string& filePath, ExportManager* exportManager,
                                        std::atomic<float>& progress, std::string& message,
                                        std::mutex& mutex, std::atomic<bool>& isImporting) {
    isImporting = true;

    std::thread([this, filePath, exportManager, &progress, &message, &mutex, &isImporting]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            progress = 0.5f;
            message = "Экспорт реестровых номеров контрактов...";
        }

        int exportedCount = 0;
        if (exportManager) {
            exportedCount = exportManager->ExportContractsForChecking(filePath);
        }
        m_lastExportCount = exportedCount;

        {
            std::lock_guard<std::mutex> lock(mutex);
            progress = 1.0f;
            message = "Экспорт завершен.";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        isImporting = false;
    }).detach();
}

void ContractRegistryNumbersView::Render() {
    if (!IsVisible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(700, 650), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Title.c_str(), &IsVisible)) {
        // --- Procurement registry number import section ---
        ImGui::TextUnformatted("Импорт реестровых номеров контрактов из файла");
        ImGui::Spacing();
        
        ImGui::BeginDisabled(uiManager->isImporting);

        if (ImGui::Button(ICON_FA_FILE_IMPORT " Импортировать реестровые номера")) {
            IGFD::FileDialogConfig config;
            config.path = GetDatabaseDirectory();
            config.countSelectionMax = 1;
            config.userDatas = IGFD::UserDatas(nullptr);
            ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey_IKZ_Service", "Выберите файл с реестровыми номерами контрактов", ".csv,.tsv", config);
        }

        ImGui::EndDisabled();

        RenderImportMapping();

        if (m_showUnfoundContracts) {
            ImGui::Spacing();
            ImGui::Separator();
            
            if (ImGui::Button("Очистить результаты импорта")) {
                m_showUnfoundContracts = false;
                m_unfoundContracts.clear();
                m_successfulImports = 0;
            }

            if (!m_ikzImportStarted && !uiManager->isImporting) {
                ImGui::Text("Импортировано реестровых номеров: %d", m_successfulImports);
                ImGui::Text("Не найдено контрактов: %zu", m_unfoundContracts.size());
            }

            if (!m_unfoundContracts.empty()) {
                ImGui::Text("Ненайденные контракты:");
                if (ImGui::BeginTable("unfound_contracts_table_service", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
                    ImGui::TableSetupColumn("Номер контракта");
                    ImGui::TableSetupColumn("Дата контракта");
                    ImGui::TableSetupColumn("Реестровый номер из файла");
                    ImGui::TableHeadersRow();

                    for (const auto& contract : m_unfoundContracts) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(contract.number.c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(contract.date.c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(contract.ikz.c_str());
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::Spacing();
        }
        
        ImGui::Separator();
        ImGui::Spacing();

        // --- Procurement registry number export section ---
        ImGui::TextUnformatted("Экспорт реестровых номеров контрактов");
        ImGui::Spacing();

        ImGui::BeginDisabled(uiManager->isImporting);

        if (ImGui::Button(ICON_FA_FILE_EXPORT " Экспортировать реестровые номера")) {
            m_lastExportCount = -1; // Reset on new export
            IGFD::FileDialogConfig config;
            config.path = GetDatabaseDirectory();
            config.countSelectionMax = 1;
            config.userDatas = IGFD::UserDatas(nullptr);
            ImGuiFileDialog::Instance()->OpenDialog("ExportContractsDlgKey", "Экспорт реестровых номеров контрактов", ".csv", config);
        }
        
        if (m_lastExportCount != -1) {
            ImGui::SameLine();
            ImGui::Text("Экспортировано %d контрактов.", m_lastExportCount);
        }

        ImGui::EndDisabled();
    }
    ImGui::End();
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> ContractRegistryNumbersView::GetDataAsStrings() {
    // This view doesn't present tabular data for export, so return empty.
    return {};
}
