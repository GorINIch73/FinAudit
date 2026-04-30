#include "UIManager.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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
    if (!SaveAllViews()) {
        return false;
    }

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
    ShowError("Не удалось открыть базу данных: " + path);
    return false;
}

void UIManager::SetWindowTitle(const std::string &db_path) {
    std::string title = "Financial Audit Application";
    if (!db_path.empty()) {
        title += " - [" + db_path + "]";
    }
    glfwSetWindowTitle(window, title.c_str());
}

void UIManager::ShowContractRegistryNumbersView() {
    // Reuse the existing procurement registry-number import/export view.
    for (const auto &view : allViews) {
        if (auto existing_view = dynamic_cast<ContractRegistryNumbersView *>(view.get())) {
            existing_view->IsVisible = true;
            ImGui::SetWindowFocus(existing_view->GetTitle());
            return; // Found and handled
        }
    }

    // If we get here, the view doesn't exist, so create it.
    CreateView<ContractRegistryNumbersView>();
}

bool UIManager::SaveAllViews() {
    bool allSaved = true;
    std::string failedViews;
    for (const auto& view : allViews) {
        if (!view->ForceSave()) {
            std::cerr << "Failed to save view before operation: "
                      << view->GetTitle() << std::endl;
            if (!failedViews.empty()) {
                failedViews += ", ";
            }
            failedViews += view->GetTitle();
            allSaved = false;
        }
    }
    if (!allSaved) {
        ShowError("Не удалось сохранить изменения перед операцией. "
                  "Операция отменена, чтобы не потерять данные.\nФормы: " +
                  failedViews);
    }
    return allSaved;
}

bool UIManager::BackupCurrentDatabase(const std::string& reason,
                                      std::string& backupPath) {
    if (!dbManager || !dbManager->is_open() || currentDbPath.empty()) {
        return false;
    }

    if (!SaveAllViews()) {
        std::cerr << "Backup aborted: not all views were saved." << std::endl;
        return false;
    }

    std::string safeReason;
    safeReason.reserve(reason.size());
    for (unsigned char c : reason) {
        if (std::isalnum(c)) {
            safeReason.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == '-' || c == '_') {
            safeReason.push_back(static_cast<char>(c));
        }
    }
    if (safeReason.empty()) {
        safeReason = "operation";
    }

    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream timestamp;
    timestamp << std::put_time(&localTime, "%Y%m%d-%H%M%S");

    std::filesystem::path dbPath(currentDbPath);
    std::filesystem::path backup =
        dbPath.parent_path() /
        (dbPath.stem().string() + "." + safeReason + "." +
         timestamp.str() + ".bak" + dbPath.extension().string());

    backupPath = backup.string();
    return dbManager->backupTo(backupPath);
}

void UIManager::ShowError(const std::string& message) {
    lastErrorMessage = message;
    showErrorPopup = true;
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
    ImGuiStyle &style = ImGui::GetStyle();

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
    case 3: {
        ImGui::StyleColorsDark();
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.15f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.30f, 0.33f, 0.36f, 0.65f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.25f, 0.27f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.30f, 0.33f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.20f, 0.22f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.22f, 0.35f, 0.42f, 0.75f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.45f, 0.52f, 0.85f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.52f, 0.60f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.34f, 0.40f, 0.90f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.45f, 0.52f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.31f, 0.53f, 0.61f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.45f, 0.52f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.34f, 0.40f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.76f, 0.84f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.76f, 0.84f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.54f, 0.86f, 0.92f, 1.00f);
        break;
    }
    case 4: {
        ImGui::StyleColorsDark();
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.96f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.12f, 0.16f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.14f, 0.18f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.14f, 0.18f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.22f, 0.31f, 0.40f, 0.70f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.20f, 0.26f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.29f, 0.37f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.36f, 0.46f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.11f, 0.15f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.18f, 0.24f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.14f, 0.18f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.24f, 0.38f, 0.45f, 0.85f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.50f, 0.57f, 0.95f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.58f, 0.64f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.21f, 0.37f, 0.45f, 0.92f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.50f, 0.58f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.61f, 0.68f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.18f, 0.23f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.31f, 0.50f, 0.58f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.35f, 0.42f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.62f, 0.86f, 0.72f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.62f, 0.86f, 0.72f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.74f, 0.94f, 0.82f, 1.00f);
        break;
    }
    case 5: {
        ImGui::StyleColorsLight();
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.10f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.97f, 0.98f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.99f, 0.99f, 1.00f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.99f, 0.99f, 1.00f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.66f, 0.70f, 0.75f, 0.70f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.90f, 0.92f, 0.94f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.82f, 0.87f, 0.91f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.76f, 0.83f, 0.88f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.86f, 0.89f, 0.92f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.78f, 0.84f, 0.89f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.88f, 0.91f, 0.94f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.69f, 0.80f, 0.88f, 0.75f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.72f, 0.83f, 0.85f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.43f, 0.64f, 0.78f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.67f, 0.79f, 0.88f, 0.95f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.54f, 0.71f, 0.82f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.63f, 0.76f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.82f, 0.87f, 0.91f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.54f, 0.71f, 0.82f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.67f, 0.79f, 0.88f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.07f, 0.43f, 0.56f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.07f, 0.43f, 0.56f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.03f, 0.33f, 0.46f, 1.00f);
        break;
    }
    case 6: {
        ImGui::StyleColorsDark();
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.91f, 0.92f, 0.96f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.58f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.10f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.13f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.28f, 0.27f, 0.39f, 0.72f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.13f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.27f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.34f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.06f, 0.09f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.11f, 0.18f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.31f, 0.24f, 0.45f, 0.78f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.42f, 0.32f, 0.58f, 0.90f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.50f, 0.39f, 0.68f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.29f, 0.23f, 0.43f, 0.92f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.41f, 0.32f, 0.58f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.50f, 0.39f, 0.68f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.13f, 0.18f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.41f, 0.32f, 0.58f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.27f, 0.22f, 0.39f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.88f, 0.68f, 0.95f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.88f, 0.68f, 0.95f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.96f, 0.78f, 1.00f, 1.00f);
        break;
    }
    case 7: {
        ImGui::StyleColorsDark();
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.90f, 0.94f, 0.88f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.49f, 0.56f, 0.47f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.11f, 0.09f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.14f, 0.11f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.14f, 0.11f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.25f, 0.37f, 0.28f, 0.70f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.19f, 0.15f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.28f, 0.21f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.35f, 0.27f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.10f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.18f, 0.13f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.14f, 0.11f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.28f, 0.43f, 0.31f, 0.82f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.36f, 0.54f, 0.39f, 0.94f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.46f, 0.65f, 0.49f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.26f, 0.40f, 0.29f, 0.92f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.36f, 0.54f, 0.39f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.46f, 0.65f, 0.49f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.18f, 0.14f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.36f, 0.54f, 0.39f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.24f, 0.36f, 0.27f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.76f, 0.90f, 0.56f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.76f, 0.90f, 0.56f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.85f, 0.98f, 0.64f, 1.00f);
        break;
    }
    case 8: {
        ImGui::StyleColorsLight();
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.14f, 0.12f, 0.10f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.50f, 0.43f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.98f, 0.95f, 0.89f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 0.98f, 0.93f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 0.98f, 0.93f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.72f, 0.63f, 0.50f, 0.70f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.91f, 0.86f, 0.76f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.84f, 0.77f, 0.64f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.76f, 0.68f, 0.53f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.87f, 0.80f, 0.67f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.78f, 0.69f, 0.54f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.90f, 0.84f, 0.73f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.78f, 0.65f, 0.43f, 0.74f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.68f, 0.54f, 0.32f, 0.86f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.57f, 0.42f, 0.24f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.78f, 0.65f, 0.43f, 0.95f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.68f, 0.54f, 0.32f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.57f, 0.42f, 0.24f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.88f, 0.82f, 0.70f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.68f, 0.54f, 0.32f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.78f, 0.65f, 0.43f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.25f, 0.12f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.25f, 0.12f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.16f, 0.08f, 1.00f);
        break;
    }
    case 9: {
        ImGui::StyleColorsDark();
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.72f, 0.72f, 0.72f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.95f, 0.95f, 0.95f, 0.90f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.85f, 0.65f, 0.00f, 0.86f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(1.00f, 0.78f, 0.00f, 0.95f);
        colors[ImGuiCol_HeaderActive] = ImVec4(1.00f, 0.86f, 0.18f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.78f, 0.58f, 0.00f, 0.96f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 0.78f, 0.00f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 0.88f, 0.22f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(1.00f, 0.78f, 0.00f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.78f, 0.58f, 0.00f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.92f, 0.24f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 0.92f, 0.24f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 1.00f, 0.42f, 1.00f);
        break;
    }
    default:
        ImGui::StyleColorsDark();
        break; // Default to dark
    }

    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);

    style.Colors[ImGuiCol_Separator] = ImVec4(0.70f, 0.50f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_SeparatorHovered] =
        ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_SeparatorActive] =
        ImVec4(1.00f, 0.80f, 0.20f, 1.00f);
}

void UIManager::ApplyFont(int font_size) {
    font_size = std::clamp(font_size, 14, 40);
    if (appliedFontSize == font_size) {
        return;
    }

    LoadFonts();

    ImGuiStyle &style = ImGui::GetStyle();
    style.FontSizeBase = static_cast<float>(font_size);
    style._NextFrameFontSizeBase = style.FontSizeBase;
    appliedFontSize = font_size;
}

void UIManager::LoadFonts() {
    if (fontsLoaded) {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();

    std::string robotoPath = getAssetPath("Roboto-Regular.ttf");
    std::string faPath = getAssetPath("fa-solid-900.otf");

    bool loadedBaseFont = false;
    if (!robotoPath.empty()) {
        ImFontConfig font_cfg;
        loadedBaseFont =
            io.Fonts->AddFontFromFileTTF(robotoPath.c_str(), 0.0f,
                                         &font_cfg,
                                         io.Fonts->GetGlyphRangesCyrillic()) !=
            nullptr;
    }

    if (!loadedBaseFont) {
        io.Fonts->AddFontDefault();
    }

    if (!faPath.empty()) {
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
        io.Fonts->AddFontFromFileTTF(faPath.c_str(), 0.0f, &config,
                                     icon_ranges);
    }

    fontsLoaded = true;
}

void UIManager::HandleFileDialogs() {
    if (ImGuiFileDialog::Instance()->Display("ChooseDbFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName =
                ImGuiFileDialog::Instance()->GetFilePathName();
            if (dbManager->createDatabase(filePathName)) {
                // Also treat creation as loading
                LoadDatabase(filePathName);
            } else {
                ShowError("Не удалось создать базу данных: " + filePathName);
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
                    if (dbManager->backupTo(newFilePath)) {
                        currentDbPath = newFilePath;
                        AddRecentDbPath(newFilePath);
                        SetWindowTitle(currentDbPath);
                    } else {
                        ShowError("Не удалось сохранить копию базы данных: " +
                                  newFilePath);
                    }
                } catch (const std::filesystem::filesystem_error &e) {
                    std::cerr << "Error saving database: " << e.what()
                              << std::endl;
                    ShowError("Ошибка сохранения базы данных: " +
                              std::string(e.what()));
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
                ShowError("Перед импортом нужно открыть или создать базу данных.");
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

    // Contract registry-number import/export dialogs
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey_IKZ_Service")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
            // Find contract registry-number view and trigger import
            for (auto& view : allViews) {
                if (auto* registryView = dynamic_cast<ContractRegistryNumbersView*>(view.get())) {
                    registryView->StartIKZImport(filePathName, importManager, dbManager,
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
            // Find contract registry-number view and trigger export
            for (auto& view : allViews) {
                if (auto* registryView = dynamic_cast<ContractRegistryNumbersView*>(view.get())) {
                    registryView->StartContractsExport(filePathName, exportManager,
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

    if (showErrorPopup) {
        ImGui::OpenPopup("Ошибка операции");
        showErrorPopup = false;
    }

    if (ImGui::BeginPopupModal("Ошибка операции", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", lastErrorMessage.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            lastErrorMessage.clear();
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

    file << "\"kps\",\"code\",\"name\",\"note\"\n";
    for (const auto& entry : entries) {
        file << "\"" << entry.kps << "\",\"" << entry.code << "\",\""
             << entry.name << "\",\"" << entry.note << "\"\n";
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
        if (fields.size() >= 4) {
            entry.kps = fields[0];
            entry.code = fields[1];
            entry.name = fields[2];
            entry.note = fields[3];
        } else {
            entry.code = fields[0];
            entry.name = fields[1];
            entry.note = fields[2];
        }

        int existing_id =
            dbManager->getKosguIdByCodeAndKps(entry.code, entry.kps);
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
