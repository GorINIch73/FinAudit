#include "ImportMapView.h"
#include "../IconsFontAwesome6.h"
#include "../ImportManager.h"
#include "../UIManager.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <thread>

// Helper to split a string by a delimiter
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
        {"Дата", {"Дата", "Дата документа", "дата", "Дата принятия"}},
        {"Номер док.", {"Номер док.", "Номер документа", "номер"}},
        {"Тип", {"Тип", "Тип БК и направление", "Бланк", "Состояние"}},
        {"Сумма", {"Сумма"}},
        {"Контрагент",
         {"Контрагент", "Наименование", "Получатель", "Плательщик"}},
        {"Назначение",
         {"Назначение", "Назначение платежа", "Примечание"}},
        {"Примечание", {"Примечание", "Назначение платежа"}},
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

static std::string get_regex_match(const std::string &text,
                                   const std::string &pattern) {
    // std::cout << "Testing regex:" << std::endl;
    // std::cout << "  Text:    '" << text << "'" << std::endl;
    // std::cout << "  Pattern: '" << pattern << "'" << std::endl;
    try {
        std::regex re(pattern);
        std::smatch match;
        if (std::regex_search(text, match, re) && match.size() > 1) {
            // std::cout << "  Match:   '" << match[1].str() << "'" <<
            // std::endl;
            return match[1].str();
        }
    } catch (const std::regex_error &e) {
        // std::cerr << "  Regex error: " << e.what() << std::endl;
        return "Regex error: " + std::string(e.what());
    }
    // std::cout << "  No match." << std::endl;
    return "No match";
}

ImportMapView::ImportMapView() {
    Title = "Сопоставление полей для импорта";
    targetFields = {"Дата",  "Номер док.", "Тип",
                    "Сумма", "Контрагент", "Назначение", "Примечание"};
}

void ImportMapView::Open(const std::string &filePath) {
    Reset();
    importFilePath = filePath;
    ReadPreviewData();
    RefreshRegexes();
    IsVisible = true;
}

void ImportMapView::Reset() {
    importFilePath.clear();
    fileHeaders.clear();
    sampleData.clear();
    currentMapping.clear();
    for (const auto &field : targetFields) {
        currentMapping[field] = -1; // -1 means "Not Mapped"
    }
    sample_description.clear();
    contract_pattern_buffer.clear();
    kosgu_pattern_buffer.clear();
    import_started = false;
    custom_note_buffer.clear();
}

void ImportMapView::ReadPreviewData() {
    if (importFilePath.empty())
        return;

    std::ifstream file(importFilePath);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open file for reading header: "
                  << importFilePath << std::endl;
        return;
    }

    // Read header
    std::string headerLine;
    if (std::getline(file, headerLine)) {
        fileHeaders = split(headerLine, '\t');
        apply_auto_mapping(fileHeaders, currentMapping);
    }

    // Read first N data rows based on settings
    int lines_to_read = 20; // Default value
    if (dbManager) {
        Settings settings = dbManager->getSettings();
        lines_to_read = settings.import_preview_lines;
    }

    std::string dataLine;
    int line_count = 0;
    while (std::getline(file, dataLine) && line_count < lines_to_read) {
        sampleData.push_back(split(dataLine, '\t'));
        line_count++;
    }
}

void ImportMapView::SetUIManager(UIManager *manager) {
    uiManager = manager;
    if (uiManager) {
        cancel_flag = &uiManager->cancelImport;
    }
}

void ImportMapView::RefreshRegexes() {
    if (dbManager) {
        regexes = dbManager->getRegexes();
    }
}

void ImportMapView::Render() {
    if (!IsVisible) {
        return;
    }

    // If import has finished, close the window
    if (import_started && uiManager && !uiManager->isImporting) {
        IsVisible = false;
        return;
    }

    float footer_height =
        ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    ImGui::SetNextWindowSize(ImVec2(700, 750), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Title.c_str(), &IsVisible)) {
        ImGui::BeginDisabled(import_started);
        ImGui::Text("Файл: %s", importFilePath.c_str());
        ImGui::Separator();

        // --- Mapping Controls ---
        ImGui::Text("Укажите, какой столбец в файле соответствует какому полю "
                    "в программе.");
        if (ImGui::BeginTable("mapping_table", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Поле в программе",
                                    ImGuiTableColumnFlags_WidthFixed, 150.0f);
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
                        if (ImGui::Selectable(fileHeaders[i].c_str(),
                                              is_selected)) {
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
        ImGui::Spacing();

        // --- Data Preview Table ---
        ImGui::Text("Предпросмотр данных (первые %d строк):", (int)sampleData.size());
        float bottom_part_height = ImGui::GetTextLineHeightWithSpacing() * 16;
        ImGui::BeginChild("PreviewScrollRegion",
                          ImVec2(0, -bottom_part_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        // Добавляем колонку '#' для порядкового номера
        if (ImGui::BeginTable("preview_table", fileHeaders.size() + 1,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            for (const auto &header : fileHeaders) {
                ImGui::TableSetupColumn(header.c_str());
            }
            ImGui::TableHeadersRow();

            for (int i = 0; i < sampleData.size(); ++i) {
                ImGui::TableNextRow();
                // Порядковый номер
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);
                for (int j = 0; j < sampleData[i].size(); ++j) {
                    ImGui::TableNextColumn();
                    std::string selectable_label =
                        sampleData[i][j] + "##row_" + std::to_string(i + 1) +
                        "_col_" + std::to_string(j + 1);
                    if (ImGui::Selectable(
                            selectable_label.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns)) {
                        int desc_col = currentMapping["Назначение"];
                        if (desc_col != -1 && desc_col < sampleData[i].size()) {
                            sample_description = sampleData[i][desc_col];
                        }
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild(); // End PreviewScrollRegion

        ImGui::Separator();
        ImGui::Spacing();

        // --- Regex Testing UI ---
        ImGui::Text("Тестирование регулярных выражений");
        ImGui::InputTextMultiline("Пример назначения платежа",
                                  &sample_description,
                                  ImVec2(-1, ImGui::GetTextLineHeight() * 4));

        auto render_regex_selector = [&](const char *label, int &selected_index,
                                         std::string &match_result,
                                         std::string &edit_buffer) {
            ImGui::PushID(label);
            const char *current_regex_name = "Не выбрано";

            if (selected_index >= 0 && selected_index < regexes.size()) {
                current_regex_name = regexes[selected_index].name.c_str();
            }

            if (ImGui::BeginCombo(label, current_regex_name)) {
                for (int i = 0; i < regexes.size(); ++i) {
                    bool is_selected = (selected_index == i);
                    if (ImGui::Selectable(regexes[i].name.c_str(),
                                          is_selected)) {
                        selected_index = i;
                        edit_buffer = regexes[i].pattern;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (selected_index >= 0) {
                ImGui::InputText("##pattern_edit", &edit_buffer);
                ImGui::SameLine();

                bool pattern_changed = edit_buffer != regexes[selected_index].pattern;

                if (pattern_changed) {
                    if (ImGui::Button("Сохранить")) {
                        if (dbManager) {
                            Regex updated_regex = regexes[selected_index];
                            updated_regex.pattern = edit_buffer;
                            if (dbManager->updateRegex(updated_regex)) {
                                RefreshRegexes();
                            }
                        }
                    }
                    ImGui::SameLine();
                }

                std::string popup_id = std::string("SaveNewRegex##") + label;
                if (ImGui::Button("Сохранить как новый")) {
                    ImGui::OpenPopup(popup_id.c_str());
                }

                if (ImGui::BeginPopup(popup_id.c_str())) {
                    static char newRegexName[128] = "";
                    ImGui::InputText("Название", newRegexName, sizeof(newRegexName));
                    if (ImGui::Button("OK")) {
                        if (dbManager && strlen(newRegexName) > 0) {
                            Regex new_regex;
                            new_regex.name = newRegexName;
                            new_regex.pattern = edit_buffer;
                            if (dbManager->addRegex(new_regex)) {
                                RefreshRegexes();
                                // Reselect the newly added regex
                                auto it = std::find_if(regexes.begin(), regexes.end(), [&](const Regex& r){ return r.name == newRegexName; });
                                if(it != regexes.end()){
                                    selected_index = std::distance(regexes.begin(), it);
                                }
                            }
                        }
                        strcpy(newRegexName, ""); // Clear buffer for next use
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Отмена")) {
                        strcpy(newRegexName, ""); // Clear buffer for next use
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }


                match_result = get_regex_match(sample_description, edit_buffer);
                ImGui::Text("Результат: %s", match_result.c_str());
            }

            ImGui::PopID();
        };

        render_regex_selector("Договор", contract_regex_index, contract_match,
                              contract_pattern_buffer);
        render_regex_selector("КОСГУ", kosgu_regex_index, kosgu_match,
                              kosgu_pattern_buffer);

        ImGui::EndDisabled(); // Re-enable UI for the buttons
        ImGui::Separator();

        ImGui::BeginDisabled(import_started);
        ImGui::InputText("Добавить к примечанию", &custom_note_buffer);
        ImGui::Checkbox("Принудительно установить тип 'Поступление'", &force_income_type);        ImGui::SameLine();
        ImGui::Checkbox("Возврат", &is_return_import);
        if (ImGui::Button("Импортировать")) {
            if (dbManager && uiManager && uiManager->importManager && cancel_flag) {
                import_started = true;
                uiManager->isImporting = true;
                *cancel_flag = false; // Reset cancel flag before starting new import
                std::thread([this]() {
                    uiManager->importManager->ImportPaymentsFromTsv(
                        importFilePath, dbManager, currentMapping,
                        uiManager->importProgress, uiManager->importMessage,
                        uiManager->importMutex, *(this->cancel_flag),
                        contract_pattern_buffer,
                        kosgu_pattern_buffer,
                        force_income_type, is_return_import,
                        custom_note_buffer);
                    uiManager->isImporting = false;
                }).detach();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена")) {
            IsVisible = false;
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> ImportMapView::GetDataAsStrings() {
    // This view doesn't present tabular data for export, so return empty.
    return {};
}
