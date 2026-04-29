#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>

#include "AboutDialog.h" // Include AboutDialog
#include "DatabaseManager.h"
#include "ExportManager.h"
#include "IconsFontAwesome6.h"
#include "ImGuiFileDialog.h"
#include "ImportManager.h"
#include "PdfReporter.h"
#include "Settings.h" // Include Settings.h
#include "UIManager.h"

// Функция обратного вызова для ошибок GLFW
static void glfw_error_callback(int error, const char *description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

int main(int, char **) {
    // Установка обработчика ошибок GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // Задаём версию OpenGL (3.3 Core)
    const char *glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Для macOS

    // Создание окна
    GLFWwindow *window = glfwCreateWindow(
        1280, 720, "Financial Audit Application", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Включить V-Sync

    // Переменная состояния для диалога "О программе"
    bool showAboutDialog = false;

    // --- Настройка ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Включить навигацию с клавиатуры
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Включить докинг

    // Установка стиля ImGui
    // ImGui::StyleColorsDark(); // This will be set by the theme loader

    // Подсветка разделителей для докинга
    ImGuiStyle &style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Separator] = ImVec4(0.70f, 0.50f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_SeparatorHovered] =
        ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(1.00f, 0.80f, 0.20f, 1.00f);

    // Загрузка шрифта будет обработана UIManager

    // Инициализация бэкендов для GLFW и OpenGL
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // --- Создание менеджеров ---
    UIManager uiManager;
    DatabaseManager dbManager;
    ImportManager importManager;
    ExportManager exportManager(&dbManager);
    PdfReporter pdfReporter;

    uiManager.SetDatabaseManager(&dbManager);
    uiManager.SetPdfReporter(&pdfReporter);
    uiManager.SetImportManager(&importManager);
    uiManager.SetExportManager(&exportManager);
    uiManager.SetWindow(window);

    // Load initial settings and apply theme
    if (!uiManager.recentDbPaths.empty()) {
        uiManager.LoadDatabase(uiManager.recentDbPaths.front());
    } else {
        // Database not opened yet, use default theme and font
        uiManager.ApplyTheme(0);
        uiManager.ApplyFont(24);
    }

    // Установка стиля ImGui
    // ImGui::StyleColorsDark(); // Removed hardcoded theme setting

    // Главный цикл приложения
    while (!glfwWindowShouldClose(window)) {
        // Обработка событий
        glfwPollEvents();
        // glfwWaitEventsTimeout(0.033); // ~30 FPS
        // Начало нового кадра ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Включаем возможность докинга
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // --- Рендеринг главного меню ---
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu(ICON_FA_FILE " Файл")) {
                if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS
                                    " Создать новую базу")) {

                    uiManager.SaveAllViews();
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "ChooseDbFileDlgKey", "Выберите файл для новой базы",
                        ".db");
                }
                if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN
                                    " Открыть базу данных")) {

                    uiManager.SaveAllViews();
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "OpenDbFileDlgKey", "Выберите файл базы данных", ".db");
                }
                if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK
                                    " Сохранить базу как...")) {

                    uiManager.SaveAllViews();
                    if (!uiManager.currentDbPath.empty()) {
                        IGFD::FileDialogConfig config;
                        config.filePathName = uiManager.currentDbPath;
                        ImGuiFileDialog::Instance()->OpenDialog(
                            "SaveDbAsFileDlgKey", "Сохранить базу как...",
                            ".db", config);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_ARROW_RIGHT_FROM_BRACKET
                                    " Выход")) {
                    uiManager.SaveAllViews();
                    glfwSetWindowShouldClose(window, true);
                }
                ImGui::Separator();
                if (ImGui::BeginMenu(ICON_FA_CLOCK_ROTATE_LEFT
                                     " Недавние файлы")) {
                    for (const auto &path : uiManager.recentDbPaths) {
                        if (ImGui::MenuItem(path.c_str())) {

                            uiManager.SaveAllViews();
                            uiManager.LoadDatabase(path);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_BOOK " Справочники")) {
                if (ImGui::MenuItem(ICON_FA_HASHTAG " КОСГУ")) {
                    uiManager.CreateView<KosguView>();
                }
                if (ImGui::MenuItem(ICON_FA_BUILDING_COLUMNS " Банк")) {
                    uiManager.CreateView<PaymentsView>();
                }
                if (ImGui::MenuItem(ICON_FA_ADDRESS_BOOK " Контрагенты")) {
                    uiManager.CreateView<CounterpartiesView>();
                }
                if (ImGui::MenuItem(ICON_FA_FILE_CONTRACT " Договоры")) {
                    uiManager.CreateView<ContractsView>();
                }
                if (ImGui::MenuItem(ICON_FA_FILE_LINES
                                    " Документы Основания")) {
                    uiManager.CreateView<BasePaymentsView>();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_FILE_PDF " Отчеты")) {
                if (ImGui::MenuItem(ICON_FA_DATABASE " SQL Запрос")) {
                    uiManager.CreateView<SqlQueryView>();
                }
                if (ImGui::MenuItem(ICON_FA_SCALE_BALANCED
                                    " Сверка: Банк ↔ ДО")) {
                    uiManager.CreateView<ReconciliationView>();
                }
                if (ImGui::MenuItem(ICON_FA_FILE_EXPORT " Экспорт в PDF")) {
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "SavePdfFileDlgKey", "Сохранить отчет в PDF", ".pdf");
                }
                if (ImGui::MenuItem(ICON_FA_FILE_CONTRACT
                                    " Договоры для проверки (PDF)")) {
                    uiManager.ExportContractsForCheckingPdf();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_GEAR " Сервис")) {
                if (ImGui::MenuItem(ICON_FA_TABLE_CELLS " Импорт из TSV")) {
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "ImportTsvFileDlgKey", "Выберите TSV файл для импорта",
                        ".tsv");
                }
                if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Импорт ЖО4 из TSV")) {
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "ImportJO4FileDlgKey",
                        "Выберите TSV файл ЖО4 для импорта", ".tsv,.csv");
                }
                if (ImGui::MenuItem(ICON_FA_FILE_SIGNATURE
                                    " Экспорт Импорт ИКЗ")) {
                    uiManager.ShowServiceView();
                }
                if (ImGui::BeginMenu(ICON_FA_DOWNLOAD
                                     " Экспорт справочников")) {
                    if (ImGui::MenuItem("КОСГУ")) {
                        ImGuiFileDialog::Instance()->OpenDialog(
                            "ExportKosguDlgKey", "Экспорт КОСГУ", ".csv");
                    }
                    if (ImGui::MenuItem("Подозрительные слова")) {
                        ImGuiFileDialog::Instance()->OpenDialog(
                            "ExportSuspiciousWordsDlgKey",
                            "Экспорт подозрительных слов", ".csv");
                    }
                    if (ImGui::MenuItem("REGEX выражения")) {
                        ImGuiFileDialog::Instance()->OpenDialog(
                            "ExportRegexesDlgKey", "Экспорт REGEX выражений",
                            ".csv");
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(ICON_FA_UPLOAD " Импорт справочников")) {
                    if (ImGui::MenuItem("КОСГУ")) {
                        ImGuiFileDialog::Instance()->OpenDialog(
                            "ImportKosguDlgKey", "Импорт КОСГУ", ".csv");
                    }
                    if (ImGui::MenuItem("Подозрительные слова")) {
                        ImGuiFileDialog::Instance()->OpenDialog(
                            "ImportSuspiciousWordsDlgKey",
                            "Импорт подозрительных слов", ".csv");
                    }
                    if (ImGui::MenuItem("REGEX выражения")) {
                        ImGuiFileDialog::Instance()->OpenDialog(
                            "ImportRegexesDlgKey", "Импорт REGEX выражений",
                            ".csv");
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem(ICON_FA_SLIDERS " Настройки")) {
                    bool found = false;
                    for (const auto &view : uiManager.allViews) {
                        if (dynamic_cast<SettingsView *>(view.get())) {
                            ImGui::SetWindowFocus(view->GetTitle());
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        uiManager.CreateView<SettingsView>();
                    }
                }
                if (ImGui::MenuItem(ICON_FA_SQUARE_ROOT_VARIABLE
                                    " Регулярные выражения")) {
                    uiManager.CreateView<RegexesView>();
                }
                if (ImGui::MenuItem(ICON_FA_EYE " Подозрительные слова")) {
                    uiManager.CreateView<SuspiciousWordsView>();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_ERASER " Очистка базы")) {
                    bool found = false;
                    for (const auto &view : uiManager.allViews) {
                        if (dynamic_cast<SelectiveCleanView *>(view.get())) {
                            ImGui::SetWindowFocus(view->GetTitle());
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        uiManager.CreateView<SelectiveCleanView>();
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_QUESTION " Помощь")) {
                if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO " О программе")) {
                    showAboutDialog = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // --- Обработка диалогов выбора файлов ---
        uiManager.HandleFileDialogs();

        // --- Рендеринг окон через UIManager ---
        uiManager.Render();

        // --- Диалог "О программе" ---
        AboutDialog::Show(&showAboutDialog);

        // Рендеринг
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Обновление и отрисовка окна
        glfwSwapBuffers(window);
    }

    // --- Очистка ресурсов ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
