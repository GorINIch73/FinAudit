#include "SettingsView.h"
#include "../CustomWidgets.h"
#include "../UIManager.h" // Added UIManager include
#include "imgui.h"
#include <iostream>

SettingsView::SettingsView() { Title = "Настройки"; }

void SettingsView::SetUIManager(UIManager *manager) { uiManager = manager; }

void SettingsView::LoadSettings() {
    if (dbManager) {
        currentSettings = dbManager->getSettings();
        originalSettings = currentSettings;
        isDirty = false;
    }
}

void SettingsView::SaveChanges() {
    if (dbManager) {
        if (dbManager->updateSettings(currentSettings)) {
            std::cout << "DEBUG: Settings saved successfully." << std::endl;
            // если все ОК меняем шрифт
            if (uiManager) {
                uiManager->ApplyFont(currentSettings.font_size);
            }
        } else {
            std::cout << "ERROR: Failed to save settings." << std::endl;
        }
    }
    isDirty = false;
}

void SettingsView::OnDeactivate() {
    if (isDirty) {
        SaveChanges();
    }
}
void SettingsView::ForceSave() {
    if (isDirty) {
        SaveChanges();
    }
}

void SettingsView::Render() {
    if (!IsVisible) {
        if (isDirty) {
            SaveChanges();
        }
        return;
    }

    if (ImGui::Begin(Title.c_str(), &IsVisible)) {

        // Load settings only once when the view becomes visible
        if (ImGui::IsWindowAppearing()) {
            LoadSettings();
        }

        if (CustomWidgets::InputText("Название организации",
                                     &currentSettings.organization_name)) {
            isDirty = true;
        }
        if (CustomWidgets::InputText("Дата начала периода",
                                     &currentSettings.period_start_date)) {
            isDirty = true;
        }
        if (CustomWidgets::InputText("Дата окончания периода",
                                     &currentSettings.period_end_date)) {
            isDirty = true;
        }
        if (CustomWidgets::InputTextMultilineWithWrap(
                "Примечание", &currentSettings.note,
                ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4))) {
            isDirty = true;
        }
        if (ImGui::InputInt("Строк предпросмотра",
                            &currentSettings.import_preview_lines)) {
            isDirty = true;
        }

        // Theme selection UI
        const char *themes[] = {"Dark", "Light", "Classic"};
        if (ImGui::Combo("Тема оформления", &currentSettings.theme, themes,
                         IM_ARRAYSIZE(themes))) {
            isDirty = true;
            if (uiManager) {
                uiManager->ApplyTheme(currentSettings.theme);
            }
        }

        if (ImGui::InputInt("Размер шрифта", &currentSettings.font_size)) {
            isDirty = true;
            // if (uiManager) {
            //     uiManager->ApplyFont(currentSettings.font_size);
            // }
        }
        //  Подсказка
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Требуется перезапуск.");
        }

        ImGui::Separator();
        ImGui::Text("Шаблон ссылки для ГосЗакупок");
        if (CustomWidgets::InputText("##zakupki_url", &currentSettings.zakupki_url_template)) {
            isDirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Используйте {IKZ} как placeholder для номера закупки.\n"
                              "Пример: https://zakupki.gov.ru/epz/contract/contractCard/common-info.html?reestrNumber={IKZ}");
        }

        ImGui::Text("Шаблон поиска договора на ГосЗакупках (без ИКЗ)");
        if (CustomWidgets::InputText("##zakupki_search_url", &currentSettings.zakupki_url_search_template)) {
            isDirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Используйте {NUMBER} как placeholder для номера договора.\n"
                              "Пример: https://zakupki.gov.ru/epz/contract/search/results.html?searchString={NUMBER}");
        }
    }
    ImGui::End();
}
