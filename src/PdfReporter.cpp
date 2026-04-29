#include "PdfReporter.h"
#include "Settings.h" // Added for Settings
#include "ExportManager.h" // Added for ContractExportData
#include <iostream>
#include <iomanip> // For std::fixed, std::setprecision
#include <algorithm> // For std::max
#include <cstring>   // For strncpy
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include "pdfgen.h" // Include the pdfgen library header
#ifdef __cplusplus
}
#endif

#include <cstdint>   // For uint32_t

PdfReporter::PdfReporter() {}

// Helper to calculate text width for a given font and size
static float get_text_width(struct pdf_doc* pdf, const char* text, float font_size, const char* font_name) {
    float width = 0.0f;
    // Temporarily set the font to calculate width
    pdf_set_font(pdf, font_name);
    pdf_get_font_text_width(pdf, font_name, text, font_size, &width);
    return width;
}

bool PdfReporter::generatePdfFromTable(
    const std::string& filename,
    const std::string& title,
    const std::vector<std::string>& columns,
    const std::vector<std::vector<std::string>>& rows
) {
    // PDF metadata
    struct pdf_info info = {
        .creator = "Financial Audit Application",
        .producer = "Financial Audit Application",
        .title = "", // Set title dynamically
        .author = "System",
        .subject = "Report",
        .date = "Generated"
    };
    strncpy(info.title, title.c_str(), sizeof(info.title) - 1);

    // Get current time for timestamp
    time_t rawtime;
    struct tm *timeinfo;
    char buffer [80];
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    std::string timestamp = buffer;

    // Create a new PDF document (A4 size)
    struct pdf_doc *pdf = pdf_create(PDF_A4_WIDTH, PDF_A4_HEIGHT, &info);
    if (!pdf) {
        std::cerr << "Failed to create PDF document: " << pdf_get_err(pdf, NULL) << std::endl;
        return false;
    }

    // --- Page layout parameters ---
    float margin_x = 50.0f;
    float margin_y = 50.0f;
    float current_y = PDF_A4_HEIGHT - margin_y;
    float line_height = 12.0f;
    float font_size_title = 18.0f;
    float font_size_header = 10.0f;
    float font_size_text = 8.0f;
    float padding = 2.0f; // Padding for cells
    float table_header_height = font_size_header + padding * 2;
    float table_row_height = font_size_text + padding * 2;

    // --- Dynamic Column Width Calculation ---
    float available_width = PDF_A4_WIDTH - 2 * margin_x;
    std::vector<float> column_widths(columns.size(), 0.0f);
    const float min_col_width = 20.0f; // Minimum width for any column

    // First pass: Calculate width based on column headers
    pdf_set_font(pdf, "Helvetica-Bold");
    for (size_t i = 0; i < columns.size(); ++i) {
        column_widths[i] = get_text_width(pdf, columns[i].c_str(), font_size_header, "Helvetica-Bold");
    }

    // Second pass: Adjust width based on content of rows
    pdf_set_font(pdf, "Helvetica");
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i < column_widths.size()) {
                float content_width = get_text_width(pdf, row[i].c_str(), font_size_text, "Helvetica");
                if (content_width > column_widths[i]) {
                    column_widths[i] = content_width;
                }
            }
        }
    }

    // Ensure minimum width and calculate total width needed
    float total_content_width = 0.0f;
    for (size_t i = 0; i < column_widths.size(); ++i) {
        column_widths[i] = std::max(min_col_width, column_widths[i]) + padding * 2; // Add padding
        total_content_width += column_widths[i];
    }

    // If total content width is less than available width, distribute remaining space
    if (total_content_width < available_width) {
        float remaining_space = available_width - total_content_width;
        float space_per_column = remaining_space / column_widths.size();
        for (size_t i = 0; i < column_widths.size(); ++i) {
            column_widths[i] += space_per_column;
        }
        total_content_width = available_width; // Now it matches available width
    } else if (total_content_width > available_width) {
        // If content is too wide, scale down all columns proportionally
        float scale_factor = available_width / total_content_width;
        for (size_t i = 0; i < column_widths.size(); ++i) {
            column_widths[i] *= scale_factor;
        }
        total_content_width = available_width;
    }


    // Add first page
    struct pdf_object* page = pdf_append_page(pdf);
    if (!page) {
        std::cerr << "Failed to add page to PDF: " << pdf_get_err(pdf, NULL) << std::endl;
        pdf_destroy(pdf);
        return false;
    }
    int page_num = 1;

    // --- Function to draw header and footer ---
    auto draw_page_header_footer = [&](struct pdf_doc* doc, struct pdf_object* current_page, int p_num) {
        // Header (Title and Timestamp)
        pdf_set_font(doc, "Times-Roman");
        pdf_add_text(doc, current_page, title.c_str(), font_size_title, margin_x, PDF_A4_HEIGHT - margin_y + font_size_title, (uint32_t)0x000000);
        pdf_add_text(doc, current_page, ("Отчет сгенерирован: " + timestamp).c_str(), font_size_text, PDF_A4_WIDTH - margin_x - get_text_width(doc, ("Отчет сгенерирован: " + timestamp).c_str(), font_size_text, "Helvetica"), PDF_A4_HEIGHT - margin_y + font_size_text, (uint32_t)0x000000);

        // Footer (Page Number)
        pdf_add_text(doc, current_page, ("Страница " + std::to_string(p_num)).c_str(), font_size_text, PDF_A4_WIDTH / 2.0f - get_text_width(doc, ("Страница " + std::to_string(p_num)).c_str(), font_size_text, "Helvetica") / 2.0f, margin_y - font_size_text, (uint32_t)0x000000);
    };


    // Draw header for first page
    draw_page_header_footer(pdf, page, page_num);
    current_y = PDF_A4_HEIGHT - margin_y - font_size_title - line_height; // Adjust start Y for table

    // --- Add Table Headers ---
    auto draw_table_headers = [&](struct pdf_doc* doc, struct pdf_object* current_page) {
        pdf_set_font(doc, "Helvetica-Bold");
        float header_x = margin_x;
        for (size_t i = 0; i < columns.size(); ++i) {
            pdf_add_text_wrap(doc, current_page, columns[i].c_str(), font_size_header, header_x + padding, current_y - padding, 0, (uint32_t)0x000000, column_widths[i] - padding * 2, 0, NULL);
            header_x += column_widths[i];
        }
        pdf_add_line(doc, current_page, margin_x, current_y - table_header_height, margin_x + total_content_width, current_y - table_header_height, 0.5f, (uint32_t)0x000000);
        current_y -= table_header_height;
    };
    draw_table_headers(pdf, page);


    // --- Add Table Rows ---
    pdf_set_font(pdf, "Helvetica");
    for (const auto& row : rows) {
        // Check for page overflow
        if (current_y - table_row_height < margin_y) {
            page = pdf_append_page(pdf);
            if (!page) {
                std::cerr << "Failed to add new page to PDF: " << pdf_get_err(pdf, NULL) << std::endl;
                pdf_destroy(pdf);
                return false;
            }
            page_num++;
            draw_page_header_footer(pdf, page, page_num);
            current_y = PDF_A4_HEIGHT - margin_y - font_size_title - line_height;
            draw_table_headers(pdf, page); // Redraw headers on new page
        }

        float cell_x = margin_x;
        float max_cell_height = 0.0f; // Initialize to 0.0f
        
        // Find max height needed for this row
        for(size_t i = 0; i < row.size(); ++i) {
             if (i < column_widths.size()) {
                float text_height = 0.0f;
                // Temporarily calculate text height without writing, using current 'page'
                // This is crucial for accurate multi-line height calculation
                pdf_add_text_wrap(pdf, page, row[i].c_str(), font_size_text, 0, 0, 0, (uint32_t)0x000000, column_widths[i] - padding * 2, PDF_ALIGN_NO_WRITE, &text_height);
                max_cell_height = std::max(max_cell_height, text_height);
             }
        }
        max_cell_height += padding * 2; // Add padding after finding max text height
        
        // Draw row content
        for (size_t i = 0; i < row.size(); ++i) {
            if (i < column_widths.size()) {
                pdf_add_text_wrap(pdf, page, row[i].c_str(), font_size_text, cell_x + padding, current_y - padding, 0, (uint32_t)0x000000, column_widths[i] - padding * 2, 0, NULL);
                cell_x += column_widths[i];
            }
        }
        pdf_add_line(pdf, page, margin_x, current_y - max_cell_height, margin_x + total_content_width, current_y - max_cell_height, 0.2f, (uint32_t)0x000000); // Row separator
        current_y -= max_cell_height; // Move to next line
    }
    
    // Save the PDF
    int ret = pdf_save(pdf, filename.c_str());
    if (ret < 0) {
        std::cerr << "Failed to save PDF: " << pdf_get_err(pdf, NULL) << std::endl;
        pdf_destroy(pdf);
        return false;
    }

    // Clean up
    pdf_destroy(pdf);
    std::cout << "PDF report '" << filename << "' generated successfully." << std::endl;
    return true;
}

bool PdfReporter::generateContractsReport(const std::string& filename, const Settings& settings, const std::vector<ContractExportData>& contracts) {
    std::string report_title = settings.organization_name;
    if (!settings.period_start_date.empty() || !settings.period_end_date.empty()) {
        report_title += "\nПериод проверки: ";
        if (!settings.period_start_date.empty()) {
            report_title += settings.period_start_date;
        }
        if (!settings.period_start_date.empty() && !settings.period_end_date.empty()) {
            report_title += " - ";
        }
        if (!settings.period_end_date.empty()) {
            report_title += settings.period_end_date;
        }
    }
    report_title += "\nОтчет по договорам для проверки";

    std::vector<std::string> columns = {
        "№ Договора", "Дата Договора", "Контрагент", "КОСГУ", "Примечание", "ИКЗ"
    };

    std::vector<std::vector<std::string>> rows;
    for (const auto& contract : contracts) {
        rows.push_back({
            contract.contract_number,
            contract.contract_date,
            contract.counterparty_name,
            contract.kosgu_codes,
            contract.note,
            contract.procurement_code
        });
    }

    return generatePdfFromTable(filename, report_title, columns, rows);
}

