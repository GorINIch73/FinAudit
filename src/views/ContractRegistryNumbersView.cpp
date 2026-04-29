#include "ContractRegistryNumbersView.h"
#include "../UIManager.h"
#include "../ExportManager.h"
#include "../IconsFontAwesome6.h"
#include "imgui.h"
#include "ImGuiFileDialog.h"
#include <thread>

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
}

void ContractRegistryNumbersView::StartIKZImport(const std::string& filePath, ImportManager* importManager,
                                  DatabaseManager* dbManager, std::atomic<float>& progress,
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

    std::thread([this, filePath, importManager, dbManager, &progress, &message, &mutex, &isImporting, backupPath]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            progress = 0.0f;
            message = "Резервная копия создана: " + backupPath;
        }
        importManager->importIKZFromFile(
            filePath,
            dbManager,
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
        ImGui::TextUnformatted("Импорт реестровых номеров контрактов из файла (колонки: номер, дата, реестровый номер)");
        ImGui::Spacing();
        
        ImGui::BeginDisabled(uiManager->isImporting);

        if (ImGui::Button(ICON_FA_FILE_IMPORT " Импортировать реестровые номера")) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            config.countSelectionMax = 1;
            config.userDatas = IGFD::UserDatas(nullptr);
            ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey_IKZ_Service", "Выберите файл с реестровыми номерами контрактов", ".csv,.tsv", config);
        }

        ImGui::EndDisabled();

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
            config.path = ".";
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
