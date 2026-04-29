#include "SuspiciousWordsView.h"
#include "imgui.h"
#include "IconsFontAwesome6.h"

SuspiciousWordsView::SuspiciousWordsView() {}

void SuspiciousWordsView::SetUIManager(UIManager* manager) {
    uiManager = manager;
}

void SuspiciousWordsView::Render() {
    if (!IsVisible) {
        return;
    }

    ImGui::Begin(GetTitle(), &IsVisible);

    // Toolbar
    if (ImGui::Button(ICON_FA_PLUS " Добавить")) {
        ImGui::OpenPopup("Добавить слово");
    }

    // Popup for adding a new word
    if (ImGui::BeginPopupModal("Добавить слово", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char newWord[256] = "";
        ImGui::InputText("Слово", newWord, sizeof(newWord));

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (strlen(newWord) > 0) {
                SuspiciousWord word;
                word.word = newWord;
                if (dbManager->addSuspiciousWord(word)) {
                    loadSuspiciousWords();
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Popup for editing a word
    if (showEditPopup) {
        ImGui::OpenPopup("Редактировать слово");
        showEditPopup = false;
    }
    if (ImGui::BeginPopupModal("Редактировать слово", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Слово", editedWordBuffer, sizeof(editedWordBuffer));

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (strlen(editedWordBuffer) > 0) {
                editedWord.word = editedWordBuffer;
                if (dbManager->updateSuspiciousWord(editedWord)) {
                    loadSuspiciousWords();
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // Table of suspicious words
    if (ImGui::BeginTable("suspiciousWordsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Слово");
        ImGui::TableSetupColumn("Действия", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();

        for (const auto& word : suspiciousWords) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", word.word.c_str());
            ImGui::TableNextColumn();
            
            std::string editButton = std::string(ICON_FA_PENCIL) + "##" + std::to_string(word.id);
            if (ImGui::Button(editButton.c_str())) {
                editedWord = word;
                strncpy(editedWordBuffer, editedWord.word.c_str(), sizeof(editedWordBuffer));
                showEditPopup = true;
            }
            ImGui::SameLine();

            std::string deleteButton = std::string(ICON_FA_TRASH_CAN) + "##" + std::to_string(word.id);
            if (ImGui::Button(deleteButton.c_str())) {
                if (dbManager->deleteSuspiciousWord(word.id)) {
                    loadSuspiciousWords();
                }
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

const char* SuspiciousWordsView::GetTitle() {
    return "Подозрительные слова";
}

void SuspiciousWordsView::loadSuspiciousWords() {
    suspiciousWords = dbManager->getSuspiciousWords();
}

void SuspiciousWordsView::SetDatabaseManager(DatabaseManager* dbManager) {
    this->dbManager = dbManager;
    loadSuspiciousWords();
}

void SuspiciousWordsView::SetPdfReporter(PdfReporter* pdfReporter) {
    this->pdfReporter = pdfReporter;
}

std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> SuspiciousWordsView::GetDataAsStrings() {
    std::vector<std::string> headers = {"Слово"};
    std::vector<std::vector<std::string>> data;
    for (const auto& word : suspiciousWords) {
        data.push_back({word.word});
    }
    return {headers, data};
}

