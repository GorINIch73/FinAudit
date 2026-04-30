#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>

#include "Kosgu.h"
#include "Counterparty.h"
#include "Contract.h"
#include "Payment.h"
#include "PaymentDetail.h"
#include "Settings.h"
#include "Regex.h"
#include "SuspiciousWord.h"
#include "BasePaymentDocument.h"

struct ContractExportData; // Forward declaration

struct IntegrityIssue {
    std::string severity;
    std::string check_name;
    std::string details;
};

class DatabaseManager {
public:
    DatabaseManager();
~DatabaseManager();

    bool open(const std::string& filepath);
    void close();
    bool createDatabase(const std::string& filepath);
    bool backupTo(const std::string& backupFilepath);
    bool is_open() const;
    sqlite3* getDatabase() const { return db; }
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    class TransactionGuard {
    public:
        explicit TransactionGuard(DatabaseManager& manager);
        ~TransactionGuard();

        TransactionGuard(const TransactionGuard&) = delete;
        TransactionGuard& operator=(const TransactionGuard&) = delete;

        bool started() const;
        bool commit();
        void rollback();

    private:
        DatabaseManager& manager;
        bool active = false;
    };

    // Settings
    Settings getSettings();
    bool updateSettings(const Settings& settings);

    std::vector<Kosgu> getKosguEntries();
    bool addKosguEntry(Kosgu& entry);
    bool updateKosguEntry(const Kosgu& entry);
    bool deleteKosguEntry(int id);
    int getKosguIdByCode(const std::string& code);
    int getKosguIdByCodeAndKps(const std::string& code, const std::string& kps);

    bool addCounterparty(Counterparty& counterparty); // Pass by reference to get the id back
    int getCounterpartyIdByNameInn(const std::string& name, const std::string& inn);
    int getCounterpartyIdByName(const std::string& name);
    std::vector<Counterparty> getCounterparties();
    bool updateCounterparty(const Counterparty& counterparty);
    bool deleteCounterparty(int id);
    std::vector<ContractPaymentInfo> getPaymentInfoForCounterparty(int counterparty_id);

    int addContract(Contract& contract); // Pass by reference to get the id back
    int getContractIdByNumberDate(const std::string& number, const std::string& date);
    int updateContractProcurementCode(const std::string& number, const std::string& date, const std::string& procurement_code);
    bool updateContractProcurementCode(int contract_id, const std::string& procurement_code);
    std::vector<Contract> getContracts();
    bool updateContract(const Contract& contract);
    bool updateContractFlags(int contract_id, bool is_for_checking, bool is_for_special_control);
    bool deleteContract(int id);
    void transferPaymentDetails(int from_contract_id, int to_contract_id);
    std::vector<ContractPaymentInfo> getPaymentInfoForContract(int contract_id);

    // BasePaymentDocument methods
    int addBasePaymentDocument(BasePaymentDocument& doc);
    int getBasePaymentDocumentIdByNumberDate(const std::string& number, const std::string& date);
    int getBasePaymentDocumentIdBySignature(const std::string& number, const std::string& date, const std::string& document_name, const std::string& counterparty_name);
    std::vector<BasePaymentDocument> getBasePaymentDocuments();
    bool updateBasePaymentDocument(const BasePaymentDocument& doc);
    bool deleteBasePaymentDocument(int id);
    bool clearBasePaymentDocuments();
    std::vector<ContractPaymentInfo> getPaymentInfoForBasePaymentDocument(int doc_id);

    // Связи платежей банка с документами ЖО4. Поддерживают несколько
    // документов на один платеж и частичные оплаты одного документа.
    bool addPaymentBaseDocumentLink(PaymentBaseDocumentLink& link);
    std::vector<PaymentBaseDocumentLink> getPaymentBaseDocumentLinksForPayment(int payment_id);
    std::vector<PaymentBaseDocumentLink> getPaymentBaseDocumentLinksForDocument(int base_document_id);
    bool deletePaymentBaseDocumentLink(int link_id);

    // BasePaymentDocumentDetail methods
    bool addBasePaymentDocumentDetail(BasePaymentDocumentDetail& detail);
    std::vector<BasePaymentDocumentDetail> getBasePaymentDocumentDetails(int document_id);
    std::vector<BasePaymentDocumentDetail> getAllBasePaymentDocumentDetails();
    bool updateBasePaymentDocumentDetail(const BasePaymentDocumentDetail& detail);
    bool deleteBasePaymentDocumentDetail(int id);
    bool deleteAllBasePaymentDocumentDetails(int document_id);
    bool bulkUpdateBasePaymentDocumentDetails(const std::vector<int>& document_ids, const std::string& field_to_update, int new_id);
    int getBasePaymentDocumentDetailIdByContent(int document_id, const std::string& operation_content);
    int getBasePaymentDocumentDetailIdBySignature(const BasePaymentDocumentDetail& detail);

    // Автоподбор платежа из банка для документа основания
    struct PaymentMatch {
        int payment_id;
        std::string date;
        std::string doc_number;
        double amount;
        std::string counterparty_name;
        std::string description;
        int match_score; // 0-100, процент совпадения
        std::string match_reasons; // описание совпадений
    };
    std::vector<PaymentMatch> findMatchingPayments(const BasePaymentDocument& doc, bool require_counterparty = true);
    bool linkBasePaymentDocumentToPayment(int doc_id, int payment_id);

    // Сверка
    struct ReconciliationRecord {
        int payment_id;
        std::string payment_date;
        std::string payment_doc_number;
        double payment_amount;
        std::string payment_description;
        std::string counterparty_name;
        int payment_detail_id;
        double detail_amount;
        std::string kosgu_code;

        int base_doc_id;
        std::string base_doc_date;
        std::string base_doc_number;
        std::string base_doc_name;
        double base_doc_total;
        bool base_doc_for_checking;
        bool base_doc_checked;
        int contract_id;
        std::string contract_number;
        std::string contract_date;

        int base_detail_id;
        std::string base_detail_content;
        std::string base_detail_debit;
        std::string base_detail_credit;
        std::string base_detail_kosgu;
        double base_detail_amount;
    };

    std::vector<ReconciliationRecord> getReconciliationData(const std::string& filter = "");

    std::vector<Payment> getPayments();
    bool addPayment(Payment& payment);
    bool updatePayment(const Payment& payment);
    bool deletePayment(int id);
    std::vector<ContractPaymentInfo> getPaymentInfoForKosgu(int kosgu_id);
    std::vector<ContractPaymentInfo> getDecodingForKosgu(int kosgu_id, const std::string& filterText = "");
    std::vector<KosguPaymentDetailInfo> getAllKosguPaymentInfo();
    std::vector<ContractPaymentInfo> getAllContractPaymentInfo();
    std::vector<CounterpartyPaymentInfo> getAllCounterpartyPaymentInfo();
    std::vector<ContractExportData> getContractsForExport();

    bool addPaymentDetail(PaymentDetail& detail);
    std::vector<PaymentDetail> getPaymentDetails(int payment_id);
    std::vector<PaymentDetail> getAllPaymentDetails();
    bool updatePaymentDetail(const PaymentDetail& detail);
    bool deletePaymentDetail(int id);
    bool deleteAllPaymentDetails(int payment_id);
    bool bulkUpdatePaymentDetails(const std::vector<int>& payment_ids, const std::string& field_to_update, int new_id);

    // Regex CRUD
    std::vector<Regex> getRegexes();
    bool addRegex(Regex& regex);
    bool updateRegex(const Regex& regex);
    bool deleteRegex(int id);
    int getRegexIdByName(const std::string& name);

    // Suspicious Words CRUD
    std::vector<SuspiciousWord> getSuspiciousWords();
    bool addSuspiciousWord(SuspiciousWord& word);
    bool updateSuspiciousWord(const SuspiciousWord& word);
    bool deleteSuspiciousWord(int id);
    int getSuspiciousWordIdByWord(const std::string& word);

    // Maintenance methods
    bool ClearPayments();
    bool ClearCounterparties();
    bool ClearContracts();
    bool ClearBasePaymentDocuments();
    bool CleanOrphanPaymentDetails();
    std::vector<IntegrityIssue> getIntegrityReport();
    
    bool executeSelect(const std::string& sql, std::vector<std::string>& columns, std::vector<std::vector<std::string>>& rows);

private:
    bool execute(const std::string& sql);
    bool configureConnection();
    void applyPerformanceIndexes();
    void checkAndUpdateDatabaseSchema();
    
    sqlite3* db;
};
