#pragma once

#include <string>

struct Contract {
    int id;
    std::string number;
    std::string date;
    int counterparty_id = -1;
    double total_amount = 0.0; // This is a calculated field from related payments

    // New database fields
    double contract_amount = 0.0;
    std::string end_date;
    std::string procurement_code;
    std::string note;
    bool is_for_checking = false;
    bool is_for_special_control = false;
    bool is_found = false;
};
