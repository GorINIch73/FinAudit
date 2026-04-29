#pragma once

#include <string>

struct Counterparty {
    int id = -1;
    std::string name;
    std::string inn; // Individual Taxpayer Identification Number
    bool is_contract_optional = false;
    double total_amount = 0.0;
};
