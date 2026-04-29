#pragma once

#include "imgui.h"
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <memory> // Required for std::unique_ptr
#include <type_traits> // Required for std::is_same_v

#include "Kosgu.h"
#include "DatabaseManager.h"
#include "PdfReporter.h"
#include "views/BaseView.h"
#include "views/PaymentsView.h"
#include "views/KosguView.h"
#include "views/CounterpartiesView.h"
#include "views/ContractsView.h"
#include "views/BasePaymentsView.h"
#include "views/JO4ImportMapView.h"
#include "views/ReconciliationView.h"
#include "views/SqlQueryView.h"
#include "views/SettingsView.h"
#include "views/ImportMapView.h"
#include "views/RegexesView.h"
#include "views/SelectiveCleanView.h"
#include "views/SuspiciousWordsView.h"
#include "views/SpecialQueryView.h"
#include "views/ContractRegistryNumbersView.h"

struct GLFWwindow;
class ImportManager;
class ExportManager;

class UIManager {
public:
    UIManager();
    ~UIManager();
    void Render();
    void SetDatabaseManager(DatabaseManager* dbManager);
    void SetPdfReporter(PdfReporter* pdfReporter);
    void SetImportManager(ImportManager* importManager);
    void SetExportManager(ExportManager* exportManager);
    void SetWindow(GLFWwindow* window);
    bool LoadDatabase(const std::string& path); // New method
    void AddRecentDbPath(std::string path);
    void HandleFileDialogs();
    void SetWindowTitle(const std::string& db_path);
    void ShowContractRegistryNumbersView();
    void SaveAllViews();
    bool BackupCurrentDatabase(const std::string& reason, std::string& backupPath);
    SpecialQueryView* CreateSpecialQueryView(const std::string& title, const std::string& query);
    void ApplyTheme(int theme_index);
    void ApplyFont(int font_size);

    // Import/Export methods
    void ExportKosgu(const std::string& path);
    void ExportSuspiciousWords(const std::string& path);
    void ExportRegexes(const std::string& path);
    void ExportContractsForCheckingPdf();
    void ImportKosgu(const std::string& path);
    void ImportSuspiciousWords(const std::string& path);
    void ImportRegexes(const std::string& path);


    template<typename T>
    T* CreateView() {
        auto view = std::make_unique<T>();
        T* viewPtr = view.get();
        view->SetDatabaseManager(dbManager);
        view->SetPdfReporter(pdfReporter);

        if constexpr (std::is_same_v<T, ImportMapView> || std::is_same_v<T, JO4ImportMapView> || std::is_same_v<T, PaymentsView> || std::is_same_v<T, ContractsView> || std::is_same_v<T, KosguView> || std::is_same_v<T, CounterpartiesView> || std::is_same_v<T, SettingsView> || std::is_same_v<T, ContractRegistryNumbersView>) {
            viewPtr->SetUIManager(this);
        }
        
        if constexpr (std::is_same_v<T, ContractRegistryNumbersView>) {
            viewPtr->SetExportManager(exportManager);
        }

        std::string title = std::string(view->GetTitle());
        // Only add a unique ID if it's not a singleton view like Settings or ImportMap
        if constexpr (!std::is_same_v<T, SettingsView> && !std::is_same_v<T, ImportMapView> && !std::is_same_v<T, JO4ImportMapView> && !std::is_same_v<T, SelectiveCleanView> && !std::is_same_v<T, SuspiciousWordsView> && !std::is_same_v<T, ContractRegistryNumbersView>) {
             title += "###" + std::to_string(viewIdCounter++);
        }
        view->SetTitle(title);
        view->IsVisible = true;
        allViews.push_back(std::move(view));
        return viewPtr;
    }

    std::vector<std::string> recentDbPaths;
    std::string currentDbPath;

    std::atomic<bool> isImporting{false};
    std::atomic<float> importProgress{0.0f};
    std::string importMessage;
    std::mutex importMutex;
    std::atomic<bool> cancelImport;

    ImportManager* importManager = nullptr;
    ExportManager* exportManager = nullptr;
    std::vector<std::unique_ptr<BaseView>> allViews;

private:
    void LoadRecentDbPaths();
    void SaveRecentDbPaths();

    DatabaseManager* dbManager;
    PdfReporter* pdfReporter;
    GLFWwindow* window;
    int viewIdCounter = 0;
};
