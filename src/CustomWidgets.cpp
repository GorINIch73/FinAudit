#include "CustomWidgets.h"
#include "imgui.h"
#include "imgui_internal.h" // For ImTextCharFromUtf8
#include <algorithm> // For std::remove_if, std::find_if, std::sort, std::clamp, std::replace
#include <cctype>     // For std::isdigit
#include <cstdio>     // For std::sprintf, std::sscanf, std::snprintf
#include <cstring>    // For std::strncpy, std::memset
#include <functional> // For std::function
#include <stdexcept>  // For std::invalid_argument, std::out_of_range
#include <string>     // For std::string, std::to_string
#include <strings.h>  // For strcasestr, strcasecmp (POSIX specific)
#include <vector>     // For std::vector

#include <iostream>
namespace CustomWidgets {
// вспомогательные функции для кастомного многострочного виджета
namespace { // Anonymous namespace for helper
struct InputTextCallback_UserData {
        std::string *Str;
        ImGuiInputTextCallback ChainCallback;
        void *ChainCallbackUserData;
};
// namespace
static int InputTextCallback(ImGuiInputTextCallbackData *data) {
    InputTextCallback_UserData *user_data =
        (InputTextCallback_UserData *)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string *str = user_data->Str;
        IM_ASSERT(data->Buf == str->c_str());
        str->resize(data->BufTextLen);
        data->Buf = (char *)str->data();
    } else if (user_data->ChainCallback) {
        data->UserData = user_data->ChainCallbackUserData;
        return user_data->ChainCallback(data);
    }
    return 0;
}
} // namespace

bool InputText(const char *label, std::string *str, ImGuiInputTextFlags flags) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallback_UserData cb_user_data;
    cb_user_data.Str = str;
    cb_user_data.ChainCallback = nullptr;
    cb_user_data.ChainCallbackUserData = nullptr;
    return ImGui::InputText(label, (char *)str->c_str(), str->capacity() + 1,
                            flags, InputTextCallback, &cb_user_data);
}

bool InputTextMultiline(const char *label, std::string *str, const ImVec2 &size,
                        ImGuiInputTextFlags flags) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallback_UserData cb_user_data;
    cb_user_data.Str = str;
    cb_user_data.ChainCallback = nullptr;
    cb_user_data.ChainCallbackUserData = nullptr;

    return ImGui::InputTextMultiline(label, (char *)str->c_str(),
                                     str->capacity() + 1, size, flags,
                                     InputTextCallback, &cb_user_data);
}

// Кастомный многострочный виджет текста с автопереносом по буквам
bool InputTextMultilineWithWrap(const char *label, std::string *str,
                                const ImVec2 &size, ImGuiInputTextFlags flags) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);

    bool changed = false;
    float width = (size.x <= 0) ? ImGui::GetContentRegionAvail().x : size.x;

    // Create a temporary string with manual character-level wrapping
    std::string wrapped_text;
    wrapped_text.reserve(str->size());
    float current_line_width = 0.0f;
    const char *p_text = str->c_str();
    const char *text_end = p_text + str->size();

    while (p_text < text_end) {
        const char *p_char_start = p_text;
        unsigned int c = 0;
        int char_len = ImTextCharFromUtf8(&c, p_text, text_end);
        p_text += char_len;
        if (c == 0)
            break;

        float char_width = ImGui::GetFont()
                               ->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX,
                                               0.0f, p_char_start, p_text)
                               .x;

        if (*p_char_start != '\n' && *p_char_start != '\r' &&
            current_line_width + char_width > width && width > 0) {
            wrapped_text.push_back('\n');
            current_line_width = 0.0f;
        }

        wrapped_text.append(p_char_start, char_len);

        if (*p_char_start == '\n' || *p_char_start == '\r') {
            current_line_width = 0.0f;
        } else {
            current_line_width += char_width;
        }
    }

    // Use the existing callback system to manage the buffer for the wrapped
    // text
    flags |= ImGuiInputTextFlags_CallbackResize |
             ImGuiInputTextFlags_NoHorizontalScroll;
    InputTextCallback_UserData cb_user_data;
    cb_user_data.Str = &wrapped_text;
    cb_user_data.ChainCallback = nullptr;
    cb_user_data.ChainCallbackUserData = nullptr;

    ImVec2 multiline_size =
        ImVec2(width, (size.y > 0) ? size.y : ImGui::GetTextLineHeight() * 4);

    changed = ImGui::InputTextMultiline(
        label, (char *)wrapped_text.data(), wrapped_text.capacity() + 1,
        multiline_size, flags, InputTextCallback, &cb_user_data);

    if (changed) {
        // Per user's example, strip all newlines from the result.
        // This means the field cannot contain intentional newlines.
        wrapped_text.erase(
            std::remove_if(wrapped_text.begin(), wrapped_text.end(),
                           [](char c) { return c == '\n' || c == '\r'; }),
            wrapped_text.end());
        *str = wrapped_text;
    }

    return changed;
}

// кастомный виджет комбобокса с фильтром
bool ComboWithFilter(const char *label, int &current_id,
                     std::vector<ComboItem> &items, char *search_buffer,
                     int search_buffer_size, ImGuiComboFlags flags) {
    bool changed = false;

    // Find the current item for preview
    std::string preview = "Не выбрано";
    if (current_id != -1) {
        auto current_it =
            std::find_if(items.begin(), items.end(), [&](const ComboItem &i) {
                return i.id == current_id;
            });
        if (current_it != items.end()) {
            preview = current_it->name;
        }
    }

    ImGui::PushID(label);

    ImGui::PushItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##combo", preview.c_str(),
                          flags | ImGuiComboFlags_PopupAlignLeft)) {
        static bool is_window_appearing = true;
        if (!is_window_appearing)
            is_window_appearing = ImGui::IsWindowAppearing();

        ImGui::PushItemWidth(-FLT_MIN);
        if (ImGui::InputText("##search", search_buffer, search_buffer_size,
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (!items.empty()) {
                if (search_buffer[0] == '\0') {
                    // Let user select explicitly, don't default to first item
                    // current_id = items[0].id;
                } else {
                    auto it = std::find_if(
                        items.begin(), items.end(), [&](const ComboItem &item) {
                            return strcasestr(item.name.c_str(),
                                              search_buffer) != nullptr;
                        });
                    if (it != items.end()) {
                        current_id = it->id;
                    } else {
                        current_id = -1; // Not found
                    }
                }
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopItemWidth();

        std::vector<ComboItem *> filtered_items;
        for (auto &item : items) {
            if (search_buffer[0] == '\0' ||
                strcasestr(item.name.c_str(), search_buffer) != nullptr) {
                filtered_items.push_back(&item);
            }
        }

        if (ImGui::BeginListBox(
                "##list",
                ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 8))) {

            if (search_buffer[0] == '\0') {
                bool is_none_selected = (current_id == -1);
                if (ImGui::Selectable("Не выбрано", is_none_selected)) {
                    current_id = -1;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (is_none_selected) {
                    ImGui::SetScrollHereY(0.0f);
                }
            }

            for (auto item : filtered_items) {
                bool is_selected = (current_id == item->id);
                if (ImGui::Selectable(
                        (!item->name.empty() ? item->name.c_str() : "-ПУСТО-"),
                        is_selected)) {
                    current_id = item->id;
                    changed = true;
                    search_buffer[0] = '\0'; // Reset search
                    ImGui::CloseCurrentPopup();
                }
                if (is_selected && is_window_appearing) {
                    // if (is_window_appearing) {
                    ImGui::SetScrollHereY(0.5f);
                    ImGui::SetItemDefaultFocus();
                    is_window_appearing = false;
                }
            }

            ImGui::EndListBox();
        }
        ImGui::EndCombo();
    }

    if (ImGui::IsItemActive() && search_buffer[0] != '\0') {
        auto exact_match =
            std::find_if(items.begin(), items.end(), [&](const ComboItem &i) {
                return strcasecmp(i.name.c_str(), search_buffer) == 0;
            });

        if (exact_match != items.end()) {
            current_id = exact_match->id;
            changed = true;
            search_buffer[0] = '\0';
        }
    }

    ImGui::PopID();

    return changed;
}

// Кастомный горизонтальный сплитер
void HorizontalSplitter(const char *str_id, float *height, float min_height,
                        float max_height) {
    ImGui::InvisibleButton(str_id, ImVec2(-1, 8.0f));

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImVec2 center = ImVec2((min.x + max.x) / 2, (min.y + max.y) / 2);

    ImU32 color = ImGui::GetColorU32(
        ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(min.x, center.y - 1), ImVec2(max.x, center.y + 1), color);

    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive()) {
        *height += ImGui::GetIO().MouseDelta.y;
        if (*height < min_height)
            *height = min_height;
        if (max_height > 0 && *height > max_height)
            *height = max_height;
    }
}
// кастомный вертикальный сплитер
void VerticalSplitter(const char *str_id, float *width, float min_width,
                      float max_width) {
    ImGui::InvisibleButton(str_id, ImVec2(8.0f, -1));

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImVec2 center = ImVec2((min.x + max.x) / 2, (min.y + max.y) / 2);

    ImU32 color = ImGui::GetColorU32(
        ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(center.x - 1, min.y), ImVec2(center.x + 1, max.y), color);

    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
        *width += ImGui::GetIO().MouseDelta.x;
        if (*width < min_width)
            *width = min_width;
        if (max_width > 0 && *width > max_width)
            *width = max_width;
    }
}

// Виджет для ввода даты с автоформатированием
bool InputDate(const char *label, std::string &date) {

    bool changed = false;
    const std::string pattern = "YYYY-MM-DD";
    constexpr std::size_t buf_size = 11; // "YYYY-MM-DD" + null terminator

    // ImGui::TextUnformatted(label);
    // ImGui::SameLine();

    // 1. Проверяем текущее значение на соответствие шаблону
    if (date.empty()) {
        date = "____-__-__";
    }

    // 2. Подготавливаем буфер для ImGui
    char buf[buf_size];
    std::strncpy(buf, date.c_str(), buf_size);
    buf[buf_size - 1] = '\0';

    // 3. Отображаем поле ввода
    ImGui::PushID(label);
    ImGui::PushItemWidth(ImGui::CalcTextSize(std::string(pattern).c_str()).x);
    if (ImGui::InputText("", buf, buf_size)) {
        std::string new_date = buf;

        // Попытка распознать формат dd.mm.yyyy
        if (std::count(new_date.begin(), new_date.end(), '.') == 2) {
            int d, m, y;
            if (std::sscanf(new_date.c_str(), "%d.%d.%d", &d, &m, &y) == 3) {
                // Простая валидация, чтобы избежать совсем неверных дат
                if (d > 0 && d <= 31 && m > 0 && m <= 12 && y >= 1900 &&
                    y < 2200) {
                    char formatted_date[11];
                    std::snprintf(formatted_date, sizeof(formatted_date),
                                  "%04d-%02d-%02d", y, m, d);
                    new_date = formatted_date;
                }
            }
        }

        // 4. Фильтруем ввод - оставляем только цифры и дефисы
        std::string filtered;
        for (char c : new_date) {
            if (std::isdigit(c) || c == '-' || c == '_') {
                filtered.push_back(c);
            }
        }

        // 5. Восстанавливаем шаблон
        std::string formatted = pattern;
        std::size_t digit_pos = 0;

        // Заполняем цифрами
        for (std::size_t i = 0;
             i < filtered.size() && digit_pos < pattern.size(); ++i) {
            if (std::isdigit(filtered[i])) {
                while (digit_pos < pattern.size() &&
                       pattern[digit_pos] != 'Y' && pattern[digit_pos] != 'M' &&
                       pattern[digit_pos] != 'D') {
                    digit_pos++;
                }
                if (digit_pos < pattern.size()) {
                    formatted[digit_pos++] = filtered[i];
                }
            }
        }

        // 6. Валидация и корректировка значений
        // Год (1900-2100):
        if (formatted[0] != '_') {
            int year = std::clamp(
                (formatted[0] - '0') * 1000 + (formatted[1] - '0') * 100 +
                    (formatted[2] - '0') * 10 + (formatted[3] - '0'),
                1900, 2100);
            formatted.replace(0, 4, std::to_string(year));
        }

        // Месяц (1-12)
        if (formatted[5] != '_') {
            int month = std::clamp(
                (formatted[5] - '0') * 10 + (formatted[6] - '0'), 1, 12);
            formatted.replace(5, 2,
                              month < 10 ? "0" + std::to_string(month)
                                         : std::to_string(month));
        }

        // День (1-31 с учетом месяца)
        if (formatted[8] != '_') {
            int day = (formatted[8] - '0') * 10 + (formatted[9] - '0');
            int max_day = 31;

            if (formatted[5] != '_') {
                int month = (formatted[5] - '0') * 10 + (formatted[6] - '0');
                if (month == 2) {
                    max_day = 28; // без учета високосных
                } else if (month == 4 || month == 6 || month == 9 ||
                           month == 11) {
                    max_day = 30;
                }
            }

            day = std::clamp(day, 1, max_day);
            formatted.replace(8, 2,
                              day < 10 ? "0" + std::to_string(day)
                                       : std::to_string(day));
        }

        // 7. Заменяем незаполненные позиции
        for (std::size_t i = 0; i < formatted.size(); ++i) {
            if (formatted[i] == 'Y' || formatted[i] == 'M' ||
                formatted[i] == 'D') {
                formatted[i] = '_';
            }
        }

        // 8. Сохраняем изменения
        if (formatted != date) {
            date = formatted;
            changed = true;
        }
    }
    ImGui::PopItemWidth();

    // 9. Подсказка
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Формат: YYYY-MM-DD");
    }
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    return changed;
}

bool AmountInput(const char *label, double &v, const char *format,
                 ImGuiInputTextFlags flags) {
    char buf[64];
    std::sprintf(buf, format, v);

    // Флаг ImGuiInputTextFlags_CharsDecimal разрешает только цифры, точку и
    // знак. Нам нужно также разрешить запятую и пробелы, поэтому мы не будем
    // его использовать, а обработаем ввод вручную.
    if (ImGui::InputText(label, buf, 64, flags)) {
        std::string s(buf);

        // Удаляем все пробелы (разделители тысяч)
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());

        // Заменяем запятую на точку
        std::replace(s.begin(), s.end(), ',', '.');

        try {
            // Пытаемся преобразовать строку в double
            double new_v = std::stod(s);
            if (v != new_v) {
                v = new_v;
                return true; // Возвращаем true, если значение изменилось
            }
        } catch (const std::invalid_argument &) {
            // Ошибка парсинга. Можно здесь что-то предпринять, например,
            // подсветить поле красным или оставить старое значение.
            // Пока просто игнорируем некорректный ввод.
        } catch (const std::out_of_range &) {
            // Введено слишком большое число. Игнорируем.
        }
    }
    return false; // Возвращаем false, если значение не изменилось
}

bool ConfirmationModal(const char *label, const char *title,
                       const char *message, const char *confirm_button_text,
                       const char *cancel_button_text, bool &show_modal,
                       std::function<void()> for_additional_content) {
    bool confirmed = false;
    // std::cout << "0";
    if (show_modal) {
        ImGui::PushID(label);
        ImGui::OpenPopup(title);
        if (ImGui::BeginPopupModal(title, &show_modal,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", message);
            ImGui::Separator();

            if (for_additional_content) {
                for_additional_content();
                ImGui::Separator();
            }

            if (ImGui::Button(confirm_button_text, ImVec2(120, 0))) {
                confirmed = true;
                show_modal = false; // Close on confirm
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button(cancel_button_text, ImVec2(120, 0))) {
                show_modal = false; // Close on cancel
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    return confirmed;
}

} // namespace CustomWidgets
