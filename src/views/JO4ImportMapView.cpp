#include "JO4ImportMapView.h"
#include "../CustomWidgets.h"
#include "../IconsFontAwesome6.h"
#include "../UIManager.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

static std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

static std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

static std::string normalize_header(std::string value) {
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

static void apply_auto_mapping(const std::vector<std::string> &headers,
                               ColumnMapping &mapping) {
    std::map<std::string, std::vector<std::string>> aliases = {
        {"Дата документа", {"Дата документа", "дата", "Дата операции"}},
        {"Номер документа", {"Номер документа", "номер"}},
        {"Наименование документа", {"Наименование документа", "наименование"}},
        {"Наименование показателя",
         {"Наименование показателя", "Контрагент", "Наименование"}},
        {"Содержание операции", {"Содержание операции", "Назначение платежа"}},
        {"Счет дебет", {"Счет дебет", "дебет", "Дебет"}},
        {"Счет кредит", {"Счет кредит", "кредит", "Кредит"}},
        {"Сумма", {"Сумма"}},
    };

    std::vector<std::string> normalized_headers;
    normalized_headers.reserve(headers.size());
    for (const auto &header : headers) {
        normalized_headers.push_back(normalize_header(header));
    }

    for (const auto &[target, target_aliases] : aliases) {
        if (mapping[target] != -1) {
            continue;
        }
        for (const auto &alias : target_aliases) {
            std::string normalized_alias = normalize_header(alias);
            auto it = std::find(normalized_headers.begin(),
                                normalized_headers.end(), normalized_alias);
            if (it != normalized_headers.end()) {
                mapping[target] =
                    static_cast<int>(std::distance(normalized_headers.begin(), it));
                break;
            }
        }
    }
}

JO4ImportMapView::JO4ImportMapView() {
    Title = "Импорт ЖО4";
    for (const auto &field : targetFields) {
        currentMapping[field] = -1;
    }
}

void JO4ImportMapView::SetDatabaseManager(DatabaseManager* manager) {
    dbManager = manager;
}

void JO4ImportMapView::SetUIManager(UIManager* manager) {
    uiManager = manager;
    if (uiManager) {
        progressPtr = &uiManager->importProgress;
        messagePtr = &uiManager->importMessage;
        messageMutexPtr = &uiManager->importMutex;
        cancel_flag_ptr = &uiManager->cancelImport;
    }
}

void JO4ImportMapView::Open(const std::string& filePath) {
    importFilePath = filePath;
    Reset();
    IsVisible = true;
    ReadPreviewData();
}

void JO4ImportMapView::Reset() {
    fileHeaders.clear();
    sampleData.clear();
    import_started = false;
    for (const auto &field : targetFields) {
        currentMapping[field] = -1;
    }
}

void JO4ImportMapView::ReadPreviewData() {
    if (importFilePath.empty())
        return;

    std::ifstream file(importFilePath);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open file: " << importFilePath << std::endl;
        return;
    }

    std::string headerLine;
    if (std::getline(file, headerLine)) {
        fileHeaders = split(headerLine, '\t');
        apply_auto_mapping(fileHeaders, currentMapping);
    }

    int lines_to_read = 20;
    if (dbManager) {
        lines_to_read = dbManager->getSettings().import_preview_lines;
    }

    std::string dataLine;
    int line_count = 0;
    while (std::getline(file, dataLine) && line_count < lines_to_read) {
        sampleData.push_back(split(dataLine, '\t'));
        line_count++;
    }
}

void JO4ImportMapView::RefreshDropdownData() {
    // Для ЖО4 не нужны дополнительные данные
}

void JO4ImportMapView::StartImport() {
    if (!dbManager || import_started || !uiManager) return;

    // Сбрасываем флаг отмены перед запуском
    uiManager->cancelImport.store(false);

    import_started = true;
    uiManager->isImporting = true;

    // Сохраняем всё что нужно для потока заранее
    auto* db = dbManager;
    auto* prog = &uiManager->importProgress;
    auto* msg = &uiManager->importMessage;
    auto* mtx = &uiManager->importMutex;
    auto* cancel = &uiManager->cancelImport;
    auto* importing = &uiManager->isImporting;
    std::string path = importFilePath;
    ColumnMapping mapping = currentMapping;

    std::thread([db, prog, msg, mtx, cancel, importing, path, mapping]() mutable {
        // Сбрасываем флаг отмены и прогресс в самом потоке
        cancel->store(false);
        prog->store(0.0f);
        {
            std::lock_guard<std::mutex> lock(*mtx);
            *msg = "Начало импорта ЖО4...";
        }

        ImportManager importManager;
        int importedDocs = 0;
        int importedDetails = 0;
        std::vector<std::string> errors;

        importManager.ImportJournalOrder4FromTsv(
            path,
            db,
            mapping,
            *prog,
            *msg,
            *mtx,
            *cancel,
            importedDocs,
            importedDetails,
            errors
        );

        *importing = false;
    }).detach();
}

void JO4ImportMapView::Render() {
    if (!IsVisible) return;

    // Auto-close after import finished
    if (import_started && uiManager && !uiManager->isImporting) {
        import_started = false;
        IsVisible = false;
        return;
    }

    float footer_height =
        ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    ImGui::SetNextWindowSize(ImVec2(700, 750), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Title.c_str(), &IsVisible)) {
        ImGui::Text("Файл: %s", importFilePath.c_str());
        ImGui::Separator();

        // Блокируем маппинг во время импорта
        ImGui::BeginDisabled(import_started);

        ImGui::Text("Укажите, какой столбец в файле соответствует какому полю.");
        if (ImGui::BeginTable("jo4_mapping_table", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Поле", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Столбец из файла");
            ImGui::TableHeadersRow();

            for (const auto &targetField : targetFields) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", targetField.c_str());

                ImGui::TableNextColumn();
                ImGui::PushID(targetField.c_str());

                const char *current_item =
                    (currentMapping[targetField] >= 0 &&
                     currentMapping[targetField] < fileHeaders.size())
                        ? fileHeaders[currentMapping[targetField]].c_str()
                        : "Не выбрано";

                if (ImGui::BeginCombo("", current_item)) {
                    bool is_selected = (currentMapping[targetField] == -1);
                    if (ImGui::Selectable("Не выбрано", is_selected)) {
                        currentMapping[targetField] = -1;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();

                    for (int i = 0; i < fileHeaders.size(); ++i) {
                        is_selected = (currentMapping[targetField] == i);
                        if (ImGui::Selectable(fileHeaders[i].c_str(), is_selected)) {
                            currentMapping[targetField] = i;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Separator();

        // Data Preview
        ImGui::Text("Предпросмотр данных (первые %d строк):", (int)sampleData.size());
        float bottom_part_height = ImGui::GetTextLineHeightWithSpacing() * 10;
        ImGui::BeginChild("JO4PreviewScrollRegion",
                          ImVec2(0, -bottom_part_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("jo4_preview_table", fileHeaders.size() + 1,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            for (const auto &header : fileHeaders) {
                ImGui::TableSetupColumn(header.c_str());
            }
            ImGui::TableHeadersRow();

            for (int i = 0; i < sampleData.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);
                for (int j = 0; j < sampleData[i].size(); ++j) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", sampleData[i][j].c_str());
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::EndDisabled(); // import_started — конец блокировки маппинга
        ImGui::Spacing();

        // Progress bar during import
        if (import_started && uiManager) {
            ImGui::Text("Импорт...");
            ImGui::ProgressBar(uiManager->importProgress, ImVec2(-1, 0));
            if (!uiManager->importMessage.empty()) {
                ImGui::TextWrapped("%s", uiManager->importMessage.c_str());
            }
        } else {
            // Import button
            bool allRequiredMapped = true;
            if (currentMapping["Сумма"] == -1) allRequiredMapped = false;

            ImGui::BeginDisabled(!allRequiredMapped);
            if (ImGui::Button(ICON_FA_FILE_IMPORT " Начать импорт ЖО4")) {
                StartImport();
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::End();
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>>
JO4ImportMapView::GetDataAsStrings() {
    std::vector<std::string> headers = fileHeaders;
    return {headers, sampleData};
}
