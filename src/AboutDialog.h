#pragma once

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include <string>

class AboutDialog {
    public:
        static void Show(bool *p_open) {
            if (!*p_open)
                return;

            ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                    ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            // Используем Begin вместо BeginPopupModal для простоты
            if (ImGui::Begin("О программе", p_open,
                             ImGuiWindowFlags_Modal |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {

                // Заголовок с иконкой
                ImGui::PushFont(nullptr); // Использовать шрифт по умолчанию
                ImGui::SetWindowFontScale(1.5f);
                ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f),
                                   ICON_FA_CIRCLE_INFO " Financial Audit");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopFont();

                ImGui::Separator();
                ImGui::Spacing();

                // Основная информация
                ImGui::Text("Система аудита финансовых операций");
                ImGui::Spacing();

                ImGui::Text("Версия: %s", VERSION_NUMBER);
                ImGui::Text("Дата сборки: %s", BUILD_DATE);
                ImGui::Spacing();

                // Назначение программы
                ImGui::PushTextWrapPos(450.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                                   "Назначение:");
                ImGui::Text(
                    "Программа предназначена для автоматизированного аудита "
                    "финансовых операций, анализа договоров, отслеживания "
                    "платежей "
                    "и выявления подозрительных операций в финансовых "
                    "документах.");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();

                // Применяемые технологии
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                                   "Используемые технологии:");
                ImGui::BulletText("Dear ImGui - графический интерфейс");
                ImGui::BulletText("GLFW - управление окнами и вводом");
                ImGui::BulletText("OpenGL 3.3 - рендеринг");
                ImGui::BulletText("SQLite 3 - база данных");
                ImGui::BulletText("PDFGen - генерация PDF-отчётов");
                ImGui::Spacing();

                // Разработчик
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                   "Разработчик:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "GorINIch");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "E-mail:");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_ENVELOPE " gGorINIch@gmail.com")) {
                    // Попытка открыть почтовый клиент
                    const char *command = "xdg-open mailto:gGorINIch@gmail.com";
                    system(command);
                }
                ImGui::Spacing();

                ImGui::Separator();
                ImGui::Spacing();

                // Кнопка закрытия
                ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x -
                                      ImGui::CalcTextSize("Закрыть").x) *
                                     0.5f);
                if (ImGui::Button("Закрыть", ImVec2(120, 0))) {
                    *p_open = false;
                }

                ImGui::End();
            }
        }

    private:
        static constexpr const char *VERSION_NUMBER = "0.1";
        static constexpr const char *BUILD_DATE = __DATE__ " " __TIME__;
};
