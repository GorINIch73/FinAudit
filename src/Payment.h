#pragma once

#include <string>

struct Payment {
    int id;
    std::string date;
    std::string doc_number;
    bool type = false; // false for 'expense', true for 'income'
    double amount;
    std::string recipient;
    std::string description;
    int counterparty_id;
    std::string note;
};

struct ContractPaymentInfo {
    int contract_id;
    std::string date;
    std::string doc_number;
    double amount;
    std::string description;
    std::string counterparty_name;
    std::string kosgu_code;
};

struct CounterpartyPaymentInfo {
    int counterparty_id;
    std::string date;
    std::string doc_number;
    double amount;
    std::string description;
    std::string kosgu_code;
};

struct KosguPaymentDetailInfo {
    int kosgu_id;
    std::string date;
    std::string doc_number;
    double amount;
    std::string description;
    std::string counterparty_name;
};
