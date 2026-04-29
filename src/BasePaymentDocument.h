#pragma once

#include <string>

// Документ Основания Платежа (обобщённая сущность вместо Накладных)
// Может представлять: накладную, акт, счёт, УПД и т.д.
struct BasePaymentDocument {
    int id = -1;
    std::string date;                    // YYYY-MM-DD
    std::string number;                  // номер документа
    std::string document_name;           // наименование документа (тип: накладная, акт и т.д.)
    std::string counterparty_name;       // наименование контрагента (импортируется из файла)
    int contract_id = -1;                // id договора
    int payment_id = -1;                 // id платежа из банка (связь)
    std::string note;                    // примечание
    bool is_for_checking = false;        // для сверки (пометка для внимательного изучения)
    bool is_checked = false;             // сверено
    double total_amount = 0.0;           // Сумма ИТОГО по документу (вычисляемое поле)
};

// Расшифровка документа основания
struct BasePaymentDocumentDetail {
    int id = -1;
    int document_id = -1;                // id документа основания
    std::string operation_content;       // содержание операции (вид работ, номенклатура)
    std::string debit_account;           // счёт дебет
    std::string credit_account;          // счёт кредит
    int kosgu_id = -1;                   // id КОСГУ (определяется по счёту дебета)
    double amount = 0.0;                 // сумма
    std::string note;                    // примечание
};
