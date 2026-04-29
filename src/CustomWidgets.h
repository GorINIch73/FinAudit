#pragma once
#include "imgui.h"
#include <functional> // Добавлено для std::function
#include <string>
#include <vector>

namespace CustomWidgets {

struct ComboItem {
        int id;
        std::string name;
};

bool InputText(const char *label, std::string *str,
               ImGuiInputTextFlags flags = 0);
bool InputTextMultiline(const char *label, std::string *str,
                        const ImVec2 &size = ImVec2(0, 0),
                        ImGuiInputTextFlags flags = 0);
bool InputTextMultilineWithWrap(const char *label, std::string *str,
                                const ImVec2 &size = ImVec2(0, 0),
                                ImGuiInputTextFlags flags = 0);

bool ComboWithFilter(const char *label, int &current_id,
                     std::vector<ComboItem> &items, char *search_buffer,
                     int search_buffer_size, ImGuiComboFlags flags);

void HorizontalSplitter(const char *str_id, float *height,
                        float min_height = 50.0f, float max_height = -1.0f);
void VerticalSplitter(const char *str_id, float *width,
                      float min_width = 100.0f, float max_width = -1.0f);

bool InputDate(const char *label, std::string &date);

bool AmountInput(const char *label, double &value, const char *format = "%.2f",
                 ImGuiInputTextFlags flags = 0);

// Renders a confirmation modal.
// - id: Unique ImGui ID for the popup.
// - title: Title of the popup window.
// - message: Message displayed in the popup body.
// - confirm_button_text: Text for the confirmation button (e.g., "Удалить").
// - cancel_button_text: Text for the cancel button (e.g., "Отмена").
// - show_modal: A boolean reference to control visibility. Set to true to open,
//               will be set to false when closed (confirmed or cancelled).
// - for_additional_content: An optional function to render additional ImGui
// widgets within the modal.
// Returns true if the confirm button was pressed, false otherwise.
bool ConfirmationModal(const char *label, const char *title,
                       const char *message, const char *confirm_button_text,
                       const char *cancel_button_text, bool &show_modal,
                       std::function<void()> for_additional_content = nullptr);

} // namespace CustomWidgets
