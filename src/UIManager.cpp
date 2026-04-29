#include "UIManager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "IconsFontAwesome6.h"
#include "ImGuiFileDialog.h"
#include "ImportManager.h"
#include "PdfReporter.h"
#include "imgui.h"
#include "views/BaseView.h"
#include <GLFW/glfw3.h>

#ifdef __linux__
#include <limits.h> // для PATH_MAX
#include <unistd.h> // для readlink
#endif

// Forward declaration
struct ImGuiIO;

// --- Поиск ресурсов ---
// Функция для поиска правильного пути к ресурсам (например, шрифтам)
std::string getAssetPath(const std::string &assetName) {
    std::vector<std::filesystem::path> searchPaths;

// 1. Путь для установленного приложения (наивысший приоритет)
#ifdef INSTALL_DATA_DIR
    searchPaths.push_back(std::filesystem::path(INSTALL_DATA_DIR) / assetName);
#endif

// 2. Пути для разработки (относительно исполняемого файла)
#ifdef __linux__
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count > 0) {
        std::filesystem::path exePath =
            std::filesystem::path(std::string(result, (size_t)count));
        std::filesystem::path exeDir = exePath.parent_path();
        // Для запуска из каталога 'build': <путь_к_exe>/../data/шрифт.ttf
        searchPaths.push_back(exeDir / ".." / "data" / assetName);
        // Для запуска из корня проекта (менее вероятно для сборок)
        searchPaths.push_back(exeDir / "data" / assetName);
    }
#endif

    // 3. Запасной вариант (относительно текущего рабочего каталога)
    searchPaths.push_back(std::filesystem::current_path() / "data" / assetName);
    searchPaths.push_back(std::filesystem::current_path() / ".." / "data" /
                          assetName);

    for (const auto &path : searchPaths) {
        if (std::filesystem::exists(path)) {
            // Возвращаем первый найденный путь, предварительно его очистив
            return std::filesystem::canonical(path).string();
        }
    }

    // Логирование, если ресурс не найден
    std::cerr << "Ошибка: Ресурс не найден: " << assetName << std::endl;
    std::cerr << "Поиск производился в следующих каталогах:" << std::endl;
    for (const auto &path : searchPaths) {
        // Выводим "очищенные" абсолютные пути для удобства отладки
        std::cerr << " - \"" << std::filesystem::weakly_canonical(path).string()
                  << "\"" << std::endl;
    }

    return ""; // Возвращаем пустую строку, если ничего не найдено
}
// --- /Поиск ресурсов ---

const size_t MAX_RECENT_PATHS = 10;
const std::string RECENT_PATHS_FILE = ".recent_dbs.txt";

UIManager::UIManager()
    : dbManager(nullptr),
      pdfReporter(nullptr),
      importManager(nullptr),
      window(nullptr),
      cancelImport(false) {
    LoadRecentDbPaths();
}

UIManager::~UIManager() {
    // Save changes in all visible views before destruction
    for (auto &view : allViews) {
        if (view->IsVisible) {
            view->OnDeactivate();
        }
    }
    SaveRecentDbPaths();
}

void UIManager::AddRecentDbPath(std::string path) {
    recentDbPaths.erase(
        std::remove(recentDbPaths.begin(), recentDbPaths.end(), path),
        recentDbPaths.end());
    recentDbPaths.insert(recentDbPaths.begin(), path);
    if (recentDbPaths.size() > MAX_RECENT_PATHS) {
        recentDbPaths.resize(MAX_RECENT_PATHS);
    }
    SaveRecentDbPaths();
}

void UIManager::LoadRecentDbPaths() {
    std::ifstream file(RECENT_PATHS_FILE);
    if (!file.is_open())
        return;

    recentDbPaths.clear();
    std::string path;
    while (std::getline(file, path)) {
        if (!path.empty()) {
            recentDbPaths.push_back(path);
        }
    }

    if (recentDbPaths.size() > MAX_RECENT_PATHS) {
        recentDbPaths.resize(MAX_RECENT_PATHS);
    }
}

void UIManager::SaveRecentDbPaths() {
    std::ofstream file(RECENT_PATHS_FILE);
    if (!file.is_open())
        return;
    for (const auto &path : recentDbPaths) {
        file << path << std::endl;
    }
}

void UIManager::SetDatabaseManager(DatabaseManager *manager) {
    dbManager = manager;
    for (auto &view : allViews) {
        view->SetDatabaseManager(manager);
    }
}

void UIManager::SetPdfReporter(PdfReporter *reporter) {
    pdfReporter = reporter;
    for (auto &view : allViews) {
        view->SetPdfReporter(reporter);
    }
}

void UIManager::SetImportManager(ImportManager *manager) {
    importManager = manager;
}

void UIManager::SetExportManager(ExportManager *manager) {
    exportManager = manager;
}

void UIManager::SetWindow(GLFWwindow *w) { window = w; }

bool UIManager::LoadDatabase(const std::string &path) {
    if (dbManager->open(path)) {
        currentDbPath = path;
        SetWindowTitle(currentDbPath);
        AddRecentDbPath(path);

        // Load settings and apply theme from the newly opened database
        Settings settings = dbManager->getSettings();
        ApplyTheme(settings.theme);
        ApplyFont(settings.font_size);

        return true;
    }
    return false;
}

void UIManager::SetWindowTitle(const std::string &db_path) {
    std::string title = "Financial Audit Application";
    if (!db_path.empty()) {
        title += " - [" + db_path + "]";
    }
    glfwSetWindowTitle(window, title.c_str());
}

void UIManager::ShowServiceView() {
    // This pattern is safer and avoids dangling pointers. It's the same pattern
    // used for the Settings view.
    for (const auto &view : allViews) {
        if (auto existing_view = dynamic_cast<ServiceView *>(view.get())) {
            existing_view->IsVisible = true;
            ImGui::SetWindowFocus(existing_view->GetTitle());
            return; // Found and handled
        }
    }

    // If we get here, the view doesn't exist, so create it.
    CreateView<ServiceView>();
}

void UIManager::SaveAllViews() {
    for (const auto& view : allViews) {
        view->ForceSave();
    }
}

SpecialQueryView *UIManager::CreateSpecialQueryView(const std::string &title,
                                                    const std::string &query) {
    auto view = std::make_unique<SpecialQueryView>(title, query);
    SpecialQueryView *viewPtr = view.get();
    view->SetDatabaseManager(dbManager);
    view->SetUIManager(this); // Set UIManager for SpecialQueryView

    std::string newTitle = title + "###" + std::to_string(viewIdCounter++);
    view->SetTitle(newTitle);
    view->IsVisible = true;
    allViews.push_back(std::move(view));
    return viewPtr;
}

void UIManager::ApplyTheme(int theme_index) {
    switch (theme_index) {
    case 0:
        ImGui::StyleColorsDark();
        break;
    case 1:
        ImGui::StyleColorsLight();
        break;
    case 2:
        ImGui::StyleColorsClassic();
        break;
    default:
        ImGui::StyleColorsDark();
        break; // Default to dark
    }
}

void UIManager::ApplyFont(int font_size) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();

    std::string robotoPath = getAssetPath("Roboto-Regular.ttf");
    std::string faPath = getAssetPath("fa-solid-900.otf");

    if (!robotoPath.empty()) {
        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas =
            false; // Важно, если вы управляете данными шрифта
        io.Fonts->AddFontFromFileTTF(robotoPath.c_str(), (float)font_size,
                                     &font_cfg,
                                     io.Fonts->GetGlyphRangesCyrillic());
    }

    if (!faPath.empty()) {
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
        io.Fonts->AddFontFromFileTTF(faPath.c_str(), (float)font_size, &config,
                                     icon_ranges);
    }

    io.Fonts->Build();
}

void UIManager::HandleFileDialogs() {
    if (ImGuiFileDialog::Instance()->Display("ChooseDbFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName =
                ImGuiFileDialog::Instance()->GetFilePathName();
            if (dbManager->createDatabase(filePathName)) {
                // Also treat creation as loading
                LoadDatabase(filePathName);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("OpenDbFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName =
                ImGuiFileDialog::Instance()->GetFilePathName();
            LoadDatabase(filePathName);
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SaveDbAsFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string newFilePath =
                ImGuiFileDialog::Instance()->GetFilePathName();
            if (!currentDbPath.empty() && newFilePath != currentDbPath) {
                try {
                    dbManager->backupTo(newFilePath);
                    currentDbPath = newFilePath;
                    AddRecentDbPath(newFilePath);
                    SetWindowTitle(currentDbPath);
                } catch (const std::filesystem::filesystem_error &e) {
                    // TODO: Show error message to user
                    std::cerr << "Error saving database: " << e.what()
                              << std::endl;
                }
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ImportTsvFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName =
                ImGuiFileDialog::Instance()->GetFilePathName();
            if (dbManager->is_open()) {
                CreateView<ImportMapView>()->Open(filePathName);
            } else {
                // TODO: Show error message that a database must be open first.
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ImportJO4FileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName =
                ImGuiFileDialog::Instance()->GetFilePathName();
            if (dbManager->is_open()) {
                CreateView<JO4ImportMapView>()->Open(filePathName);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SavePdfFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName =
                ImGuiFileDialog::Instance()->GetFilePathName();

            BaseView *focusedView = nullptr;
            for (const auto &view : allViews) {
                if (view->IsVisible &&
                    ImGui::IsWindowFocused(
                        ImGuiFocusedFlags_RootAndChildWindows)) {
                    // This is tricky. We need to check if the window with the
                    // view's title is focused. A simpler way for now is to find
                    // the last-focused window. Let's find any visible window
                    // for now.
                    focusedView = view.get();
                    break;
                }
            }

            if (focusedView) {
                auto data = focusedView->GetDataAsStrings();
                pdfReporter->generatePdfFromTable(filePathName,
                                                  focusedView->GetTitle(),
                                                  data.first, data.second);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // Export Dialogs
    if (ImGuiFileDialog::Instance()->Display("ExportKosguDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            ExportKosgu(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }
    if (ImGuiFileDialog::Instance()->Display("ExportSuspiciousWordsDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            ExportSuspiciousWords(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }
    if (ImGuiFileDialog::Instance()->Display("ExportRegexesDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            ExportRegexes(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // Import Dialogs
    if (ImGuiFileDialog::Instance()->Display("ImportKosguDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            ImportKosgu(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }
    if (ImGuiFileDialog::Instance()->Display("ImportSuspiciousWordsDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            ImportSuspiciousWords(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }
    if (ImGuiFileDialog::Instance()->Display("ImportRegexesDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            ImportRegexes(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // Service View Dialogs
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey_IKZ_Service")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
            // Find ServiceView and trigger import
            for (auto& view : allViews) {
                if (auto* serviceView = dynamic_cast<ServiceView*>(view.get())) {
                    serviceView->StartIKZImport(filePathName, importManager, dbManager,
                                               importProgress, importMessage, importMutex,
                                               isImporting);
                    break;
                }
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ExportContractsDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
            // Find ServiceView and trigger export
            for (auto& view : allViews) {
                if (auto* serviceView = dynamic_cast<ServiceView*>(view.get())) {
                    serviceView->StartContractsExport(filePathName, exportManager,
                                                     importProgress, importMessage, importMutex,
                                                     isImporting);
                    break;
                }
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

void UIManager::Render() {
    // Render all views
    for (auto &view : allViews) {
        view->Render();
    }

    // Remove closed views
    allViews.erase(std::remove_if(allViews.begin(), allViews.end(),
                                  [](const std::unique_ptr<BaseView> &view) {
                                      if (!view->IsVisible) {
                                          view->OnDeactivate();
                                          return true;
                                      }
                                      return false;
                                  }),
                   allViews.end());

    if (isImporting) {
        ImGui::OpenPopup("Importing...");
    }

    if (ImGui::BeginPopupModal("Importing...", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        {
            std::lock_guard<std::mutex> lock(importMutex);
            ImGui::Text("%s", importMessage.c_str());
        }
        ImGui::ProgressBar(importProgress, ImVec2(200, 0));

        // Add a Cancel button
        if (isImporting && ImGui::Button("Отмена")) {
            cancelImport = true;
        }

        if (!isImporting) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void UIManager::ExportKosgu(const std::string& path) {
    if (!dbManager) return;
    auto entries = dbManager->getKosguEntries();
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << path << std::endl;
        return;
    }

    file << "\"code\",\"name\",\"note\"\n";
    for (const auto& entry : entries) {
        file << "\"" << entry.code << "\",\"" << entry.name << "\",\"" << entry.note << "\"\n";
    }
}

void UIManager::ExportSuspiciousWords(const std::string& path) {
    if (!dbManager) return;
    auto entries = dbManager->getSuspiciousWords();
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << path << std::endl;
        return;
    }

    file << "\"word\"\n";
    for (const auto& entry : entries) {
        file << "\"" << entry.word << "\"\n";
    }
}

void UIManager::ExportRegexes(const std::string& path) {
    if (!dbManager) return;
    auto entries = dbManager->getRegexes();
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << path << std::endl;
        return;
    }

    file << "\"name\",\"pattern\"\n";
    for (const auto& entry : entries) {
        file << "\"" << entry.name << "\",\"" << entry.pattern << "\"\n";
    }
}

void UIManager::ExportContractsForCheckingPdf() {
    if (!dbManager || !pdfReporter) {
        std::cerr << "DatabaseManager or PdfReporter not set." << std::endl;
        return;
    }

    Settings settings = dbManager->getSettings();
    std::vector<ContractExportData> contracts = dbManager->getContractsForExport();

    std::string filename = "contracts.pdf";
    if (pdfReporter->generateContractsReport(filename, settings, contracts)) {
        // Open the generated PDF with the default system viewer
        std::string command = "xdg-open " + filename + " &";
        std::cout << "Attempting to open PDF with command: " << command << std::endl;
        // On Linux, xdg-open is common. On macOS, 'open', on Windows 'start'
        // For a cross-platform solution, a more robust approach would be needed.
        // For now, assuming xdg-open for Linux environment.
        int result = std::system(command.c_str());
        if (result != 0) {
            std::cerr << "Failed to open PDF with xdg-open. Error code: " << result << std::endl;
        }
    } else {
        std::cerr << "Failed to generate contracts PDF report." << std::endl;
    }
}

#include <sstream>

// Function to parse a single CSV line
static std::vector<std::string> parse_csv_line(const std::string &line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    char delimiter = ',';

    while (ss.good()) {
        char c = ss.get();
        if (c == '"') { // Quoted field
            std::string quoted_item;
            while (ss.good()) {
                char next_c = ss.get();
                if (next_c == '"') {
                    if (ss.peek() == '"') { // Escaped quote
                        quoted_item += '"';
                        ss.get(); // Skip the second quote
                    } else { // End of quoted field
                        break;
                    }
                } else if (next_c == std::char_traits<char>::eof()) {
                    break;
                } else {
                    quoted_item += next_c;
                }
            }
            result.push_back(quoted_item);
            if (ss.peek() == delimiter) {
                ss.get(); // Skip delimiter
            }
        } else { // Unquoted field
            std::string unquoted_item;
            unquoted_item += c;
            std::getline(ss, item, delimiter);
            unquoted_item += item;
            result.push_back(unquoted_item);
        }
    }
    return result;
}

void UIManager::ImportKosgu(const std::string& path) {
    if (!dbManager) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << path << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty() || line.find_first_not_of(" \t\n\v\f\r") == std::string::npos) {
            continue;
        }
        auto fields = parse_csv_line(line);
        if (fields.size() < 3) continue;

        Kosgu entry;
        entry.code = fields[0];
        entry.name = fields[1];
        entry.note = fields[2];

        int existing_id = dbManager->getKosguIdByCode(entry.code);
        if (existing_id != -1) {
            entry.id = existing_id;
            dbManager->updateKosguEntry(entry);
        } else {
            dbManager->addKosguEntry(entry);
        }
    }
}

void UIManager::ImportSuspiciousWords(const std::string& path) {
    if (!dbManager) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << path << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty() || line.find_first_not_of(" \t\n\v\f\r") == std::string::npos) {
            continue;
        }
        auto fields = parse_csv_line(line);
        if (fields.empty()) continue;

        std::string word_str = fields[0];
        
        if (dbManager->getSuspiciousWordIdByWord(word_str) == -1) {
            SuspiciousWord new_word;
            new_word.word = word_str;
            dbManager->addSuspiciousWord(new_word);
        }
    }
}

void UIManager::ImportRegexes(const std::string& path) {
    if (!dbManager) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << path << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty() || line.find_first_not_of(" \t\n\v\f\r") == std::string::npos) {
            continue;
        }
        auto fields = parse_csv_line(line);
        if (fields.size() < 2) continue;

        Regex new_regex;
        new_regex.name = fields[0];
        new_regex.pattern = fields[1];

        while (dbManager->getRegexIdByName(new_regex.name) != -1) {
            new_regex.name += "*";
        }
        dbManager->addRegex(new_regex);
    }
}

