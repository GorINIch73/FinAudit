#pragma once

#include <string>

struct Settings {
    int id;
    std::string organization_name;
    std::string period_start_date;
    std::string period_end_date;
    std::string note;
    int import_preview_lines;
    int theme = 0; // 0: Dark, 1: Light, 2: Classic
    int font_size = 24;
    std::string zakupki_url_template = "https://zakupki.gov.ru/epz/contract/contractCard/common-info.html?reestrNumber={IKZ}";
    std::string zakupki_url_search_template = "https://zakupki.gov.ru/epz/contract/search/results.html?searchString={NUMBER}";
};
