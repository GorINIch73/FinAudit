#include "DatabaseManager.h"
#include "ExportManager.h"
#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

DatabaseManager::DatabaseManager()
    : db(nullptr) {}

DatabaseManager::~DatabaseManager() { close(); }

static void bind_optional_id(sqlite3_stmt *stmt, int index, int id) {
    if (id > 0) {
        sqlite3_bind_int(stmt, index, id);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

static bool sqlite_scalar_int(sqlite3 *db, const std::string &sql,
                              int &value) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int(stmt, 0);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

static bool sqlite_scalar_text(sqlite3 *db, const std::string &sql,
                               std::string &value) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        value = text ? reinterpret_cast<const char *>(text) : "";
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

static bool sqlite_table_exists(sqlite3 *db, const std::string &table_name) {
    std::string sql =
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

static bool sqlite_column_exists(sqlite3 *db, const std::string &table_name,
                                 const std::string &column_name) {
    std::string sql = "PRAGMA table_info(" + table_name + ");";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool exists = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (name &&
            std::string(reinterpret_cast<const char *>(name)) == column_name) {
            exists = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return exists;
}

static bool sqlite_table_has_unique_index_on_columns(
    sqlite3 *db, const std::string &table_name,
    const std::vector<std::string> &columns) {
    sqlite3_stmt *stmt = nullptr;
    std::string sql = "PRAGMA index_list(" + table_name + ");";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::vector<std::string> unique_indexes;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int is_unique = sqlite3_column_int(stmt, 2);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (is_unique && name) {
            unique_indexes.emplace_back(reinterpret_cast<const char *>(name));
        }
    }
    sqlite3_finalize(stmt);

    for (const auto &index_name : unique_indexes) {
        sqlite3_stmt *info_stmt = nullptr;
        std::string info_sql = "PRAGMA index_info(" + index_name + ");";
        if (sqlite3_prepare_v2(db, info_sql.c_str(), -1, &info_stmt, nullptr) !=
            SQLITE_OK) {
            continue;
        }

        std::vector<std::string> index_columns;
        while (sqlite3_step(info_stmt) == SQLITE_ROW) {
            const unsigned char *name = sqlite3_column_text(info_stmt, 2);
            if (name) {
                index_columns.emplace_back(reinterpret_cast<const char *>(name));
            }
        }
        sqlite3_finalize(info_stmt);

        if (index_columns == columns) {
            return true;
        }
    }

    return false;
}

static bool payment_details_references_invoices(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_list(PaymentDetails);", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool references_invoices = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *table_name = sqlite3_column_text(stmt, 2);
        if (table_name &&
            std::string(reinterpret_cast<const char *>(table_name)) ==
                "Invoices") {
            references_invoices = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return references_invoices;
}

bool DatabaseManager::open(const std::string &filepath) {
    if (db) {
        close();
    }
    int rc = sqlite3_open(filepath.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db)
                  << std::endl;
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    if (!configureConnection()) {
        close();
        return false;
    }

    checkAndUpdateDatabaseSchema();

    return true;
}

bool DatabaseManager::configureConnection() {
    if (!db)
        return false;

    sqlite3_busy_timeout(db, 5000);

    std::vector<std::string> pragmas = {
        "PRAGMA busy_timeout = 5000;",
        "PRAGMA foreign_keys = ON;",
        "PRAGMA journal_mode = WAL;",
        "PRAGMA synchronous = NORMAL;",
        "PRAGMA temp_store = MEMORY;"};

    for (const auto &pragma : pragmas) {
        if (!execute(pragma)) {
            std::cerr << "Failed to apply SQLite setting: " << pragma
                      << std::endl;
            return false;
        }
    }

    int foreign_keys = 0;
    if (!sqlite_scalar_int(db, "PRAGMA foreign_keys;", foreign_keys) ||
        foreign_keys != 1) {
        std::cerr << "SQLite foreign key enforcement is not enabled."
                  << std::endl;
        return false;
    }

    int busy_timeout = 0;
    if (!sqlite_scalar_int(db, "PRAGMA busy_timeout;", busy_timeout) ||
        busy_timeout < 5000) {
        std::cerr << "SQLite busy_timeout was not applied." << std::endl;
        return false;
    }

    int synchronous = 0;
    if (!sqlite_scalar_int(db, "PRAGMA synchronous;", synchronous) ||
        synchronous < 1) {
        std::cerr << "SQLite synchronous mode is weaker than NORMAL."
                  << std::endl;
        return false;
    }

    std::string journal_mode;
    if (sqlite_scalar_text(db, "PRAGMA journal_mode;", journal_mode) &&
        journal_mode != "wal") {
        std::cerr << "SQLite journal_mode is '" << journal_mode
                  << "', expected WAL where supported." << std::endl;
    }

    return true;
}

void DatabaseManager::checkAndUpdateDatabaseSchema() {
    if (!db)
        return;

    if (!execute("CREATE TABLE IF NOT EXISTS SchemaVersion ("
                 "id INTEGER PRIMARY KEY CHECK (id = 1),"
                 "version INTEGER NOT NULL,"
                 "updated_at TEXT DEFAULT CURRENT_TIMESTAMP);")) {
        return;
    }

    execute("INSERT OR IGNORE INTO SchemaVersion (id, version) VALUES (1, 1);");

    execute("CREATE TABLE IF NOT EXISTS KOSGU ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "kps TEXT DEFAULT '',"
            "code TEXT NOT NULL,"
            "name TEXT NOT NULL,"
            "note TEXT,"
            "UNIQUE(kps, code));");

    execute("CREATE TABLE IF NOT EXISTS Counterparties ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "inn TEXT UNIQUE,"
            "is_contract_optional INTEGER DEFAULT 0);");

    execute("CREATE TABLE IF NOT EXISTS Contracts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "number TEXT NOT NULL,"
            "date TEXT NOT NULL,"
            "counterparty_id INTEGER,"
            "contract_amount REAL DEFAULT 0.0,"
            "end_date TEXT,"
            "procurement_code TEXT,"
            "note TEXT,"
            "is_for_checking INTEGER DEFAULT 0,"
            "is_for_special_control INTEGER DEFAULT 0,"
            "is_found INTEGER DEFAULT 0,"
            "FOREIGN KEY(counterparty_id) REFERENCES Counterparties(id));");

    execute("CREATE TABLE IF NOT EXISTS Payments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "date TEXT NOT NULL,"
            "doc_number TEXT,"
            "type INTEGER NOT NULL DEFAULT 0,"
            "amount REAL NOT NULL,"
            "recipient TEXT,"
            "description TEXT,"
            "counterparty_id INTEGER,"
            "note TEXT,"
            "FOREIGN KEY(counterparty_id) REFERENCES Counterparties(id));");

    execute("CREATE TABLE IF NOT EXISTS BasePaymentDocuments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "date TEXT NOT NULL,"
            "number TEXT NOT NULL,"
            "document_name TEXT,"
            "counterparty_name TEXT,"
            "contract_id INTEGER,"
            "payment_id INTEGER,"
            "note TEXT,"
            "is_for_checking INTEGER DEFAULT 0,"
            "is_checked INTEGER DEFAULT 0,"
            "FOREIGN KEY(contract_id) REFERENCES Contracts(id),"
            "FOREIGN KEY(payment_id) REFERENCES Payments(id));");

    execute("CREATE TABLE IF NOT EXISTS PaymentDetails ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "payment_id INTEGER NOT NULL,"
            "kosgu_id INTEGER,"
            "contract_id INTEGER,"
            "base_document_id INTEGER,"
            "amount REAL NOT NULL,"
            "FOREIGN KEY(payment_id) REFERENCES Payments(id) ON DELETE CASCADE,"
            "FOREIGN KEY(kosgu_id) REFERENCES KOSGU(id),"
            "FOREIGN KEY(contract_id) REFERENCES Contracts(id),"
            "FOREIGN KEY(base_document_id) REFERENCES BasePaymentDocuments(id));");

    execute("CREATE TABLE IF NOT EXISTS Settings ("
            "id INTEGER PRIMARY KEY,"
            "organization_name TEXT,"
            "period_start_date TEXT,"
            "period_end_date TEXT,"
            "note TEXT,"
            "import_preview_lines INTEGER DEFAULT 20,"
            "theme INTEGER DEFAULT 0,"
            "font_size INTEGER DEFAULT 24,"
            "zakupki_url_template TEXT,"
            "zakupki_url_search_template TEXT"
            ");");
    execute("INSERT OR IGNORE INTO Settings (id, organization_name, "
            "period_start_date, period_end_date, note, import_preview_lines, "
            "theme, font_size, zakupki_url_template, "
            "zakupki_url_search_template) "
            "VALUES (1, '', '', '', '', 20, 0, 24, "
            "'https://zakupki.gov.ru/epz/contract/contractCard/"
            "common-info.html?reestrNumber={IKZ}', "
            "'https://zakupki.gov.ru/epz/contract/search/"
            "results.html?searchString={NUMBER}');");

    execute("CREATE TABLE IF NOT EXISTS Regexes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "pattern TEXT NOT NULL);");
    execute("INSERT OR IGNORE INTO Regexes (name, pattern) VALUES "
            "('Контракты', '(?:по контракту|по контр|Контракт|дог\\.|К-т)(?: "
            "№)?\\s*([^\\s,]+)\\s*(?:от\\s*)?(\\d{2}\\.\\d{2}\\.(?:\\d{4}|\\d{2}))');");
    execute("INSERT OR IGNORE INTO Regexes (name, pattern) VALUES "
            "('КОСГУ', 'К(\\d{3})');");

    execute("CREATE TABLE IF NOT EXISTS SuspiciousWords ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "word TEXT NOT NULL UNIQUE);");

    execute("CREATE TABLE IF NOT EXISTS BasePaymentDocumentDetails ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "document_id INTEGER NOT NULL,"
            "operation_content TEXT,"
            "debit_account TEXT,"
            "credit_account TEXT,"
            "kosgu_id INTEGER,"
            "amount REAL NOT NULL,"
            "note TEXT,"
            "FOREIGN KEY(document_id) REFERENCES BasePaymentDocuments(id) ON DELETE CASCADE,"
            "FOREIGN KEY(kosgu_id) REFERENCES KOSGU(id));");

    auto add_column_if_missing = [&](const std::string &table,
                                     const std::string &column,
                                     const std::string &definition) {
        if (sqlite_table_exists(db, table) &&
            !sqlite_column_exists(db, table, column)) {
            return execute("ALTER TABLE " + table + " ADD COLUMN " +
                           definition + ";");
        }
        return true;
    };

    add_column_if_missing("Counterparties", "is_contract_optional",
                          "is_contract_optional INTEGER DEFAULT 0");

    if (sqlite_table_exists(db, "KOSGU")) {
        add_column_if_missing("KOSGU", "kps", "kps TEXT DEFAULT ''");
    }

    if (sqlite_table_exists(db, "KOSGU") &&
        sqlite_table_has_unique_index_on_columns(db, "KOSGU", {"code"})) {
        execute("PRAGMA foreign_keys = OFF;");
        execute("PRAGMA legacy_alter_table = ON;");

        TransactionGuard transaction(*this);
        bool migrated =
            transaction.started() &&
            execute("ALTER TABLE KOSGU RENAME TO KOSGU_old_code_unique;") &&
            execute("CREATE TABLE KOSGU ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "kps TEXT DEFAULT '',"
                    "code TEXT NOT NULL,"
                    "name TEXT NOT NULL,"
                    "note TEXT,"
                    "UNIQUE(kps, code));") &&
            execute("INSERT INTO KOSGU (id, kps, code, name, note) "
                    "SELECT id, IFNULL(kps, ''), code, name, note "
                    "FROM KOSGU_old_code_unique;") &&
            execute("DROP TABLE KOSGU_old_code_unique;") &&
            transaction.commit();

        if (!migrated) {
            transaction.rollback();
        }

        execute("PRAGMA legacy_alter_table = OFF;");
        configureConnection();
    }

    add_column_if_missing("Contracts", "contract_amount",
                          "contract_amount REAL DEFAULT 0.0");
    add_column_if_missing("Contracts", "end_date", "end_date TEXT");
    add_column_if_missing("Contracts", "procurement_code",
                          "procurement_code TEXT");
    add_column_if_missing("Contracts", "note", "note TEXT");
    add_column_if_missing("Contracts", "is_for_checking",
                          "is_for_checking INTEGER DEFAULT 0");
    add_column_if_missing("Contracts", "is_for_special_control",
                          "is_for_special_control INTEGER DEFAULT 0");
    add_column_if_missing("Contracts", "is_found",
                          "is_found INTEGER DEFAULT 0");

    add_column_if_missing("Payments", "type",
                          "type INTEGER NOT NULL DEFAULT 0");
    add_column_if_missing("Payments", "recipient", "recipient TEXT");
    add_column_if_missing("Payments", "note", "note TEXT");

    add_column_if_missing("BasePaymentDocuments", "document_name",
                          "document_name TEXT");
    add_column_if_missing("BasePaymentDocuments", "counterparty_name",
                          "counterparty_name TEXT");
    add_column_if_missing("BasePaymentDocuments", "contract_id",
                          "contract_id INTEGER");
    add_column_if_missing("BasePaymentDocuments", "payment_id",
                          "payment_id INTEGER");
    add_column_if_missing("BasePaymentDocuments", "note", "note TEXT");
    add_column_if_missing("BasePaymentDocuments", "is_for_checking",
                          "is_for_checking INTEGER DEFAULT 0");
    add_column_if_missing("BasePaymentDocuments", "is_checked",
                          "is_checked INTEGER DEFAULT 0");

    add_column_if_missing("BasePaymentDocumentDetails", "note", "note TEXT");

    add_column_if_missing("Settings", "import_preview_lines",
                          "import_preview_lines INTEGER DEFAULT 20");
    add_column_if_missing("Settings", "theme", "theme INTEGER DEFAULT 0");
    add_column_if_missing("Settings", "font_size",
                          "font_size INTEGER DEFAULT 24");
    add_column_if_missing("Settings", "zakupki_url_template",
                          "zakupki_url_template TEXT");
    add_column_if_missing("Settings", "zakupki_url_search_template",
                          "zakupki_url_search_template TEXT");

    if (sqlite_table_exists(db, "PaymentDetails") &&
        payment_details_references_invoices(db)) {
        execute("PRAGMA foreign_keys = OFF;");

        TransactionGuard transaction(*this);
        bool migrated =
            transaction.started() &&
            execute("ALTER TABLE PaymentDetails RENAME TO "
                    "PaymentDetails_old_invoice;") &&
            execute("CREATE TABLE PaymentDetails ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "payment_id INTEGER NOT NULL,"
                    "kosgu_id INTEGER,"
                    "contract_id INTEGER,"
                    "base_document_id INTEGER,"
                    "amount REAL NOT NULL,"
                    "FOREIGN KEY(payment_id) REFERENCES "
                    "Payments(id) ON DELETE CASCADE,"
                    "FOREIGN KEY(kosgu_id) REFERENCES KOSGU(id),"
                    "FOREIGN KEY(contract_id) REFERENCES "
                    "Contracts(id),"
                    "FOREIGN KEY(base_document_id) REFERENCES "
                    "BasePaymentDocuments(id));") &&
            execute("INSERT INTO PaymentDetails "
                    "(id, payment_id, kosgu_id, contract_id, "
                    "base_document_id, amount) "
                    "SELECT id, payment_id, NULLIF(kosgu_id, -1), "
                    "NULLIF(contract_id, -1), "
                    "NULLIF(invoice_id, -1), amount "
                    "FROM PaymentDetails_old_invoice;") &&
            execute("DROP TABLE PaymentDetails_old_invoice;") &&
            transaction.commit();

        if (!migrated) {
            transaction.rollback();
        }

        configureConnection();
    }

    if (sqlite_table_exists(db, "PaymentDetails") &&
        sqlite_column_exists(db, "PaymentDetails", "invoice_id") &&
        !sqlite_column_exists(db, "PaymentDetails", "base_document_id")) {
        execute("PRAGMA foreign_keys = OFF;");

        TransactionGuard transaction(*this);
        bool migrated =
            transaction.started() &&
            execute("ALTER TABLE PaymentDetails RENAME TO "
                    "PaymentDetails_old_invoice_column;") &&
            execute("CREATE TABLE PaymentDetails ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "payment_id INTEGER NOT NULL,"
                    "kosgu_id INTEGER,"
                    "contract_id INTEGER,"
                    "base_document_id INTEGER,"
                    "amount REAL NOT NULL,"
                    "FOREIGN KEY(payment_id) REFERENCES "
                    "Payments(id) ON DELETE CASCADE,"
                    "FOREIGN KEY(kosgu_id) REFERENCES KOSGU(id),"
                    "FOREIGN KEY(contract_id) REFERENCES Contracts(id),"
                    "FOREIGN KEY(base_document_id) REFERENCES "
                    "BasePaymentDocuments(id));") &&
            execute("INSERT INTO PaymentDetails "
                    "(id, payment_id, kosgu_id, contract_id, "
                    "base_document_id, amount) "
                    "SELECT id, payment_id, kosgu_id, contract_id, "
                    "invoice_id, amount "
                    "FROM PaymentDetails_old_invoice_column;") &&
            execute("DROP TABLE PaymentDetails_old_invoice_column;") &&
            transaction.commit();

        if (!migrated) {
            transaction.rollback();
        }

        configureConnection();
    }

    add_column_if_missing("PaymentDetails", "base_document_id",
                          "base_document_id INTEGER");

    execute("CREATE TABLE IF NOT EXISTS PaymentBaseDocumentLinks ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "payment_id INTEGER NOT NULL,"
            "base_document_id INTEGER NOT NULL,"
            "linked_amount REAL DEFAULT 0.0,"
            "match_score INTEGER DEFAULT 0,"
            "match_status TEXT DEFAULT 'suggested',"
            "match_reason TEXT,"
            "note TEXT,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "FOREIGN KEY(payment_id) REFERENCES Payments(id) ON DELETE CASCADE,"
            "FOREIGN KEY(base_document_id) REFERENCES BasePaymentDocuments(id) ON DELETE CASCADE,"
            "UNIQUE(payment_id, base_document_id));");
    applyPerformanceIndexes();

    if (!sqlite_table_exists(db, "PaymentDetails") ||
        sqlite_column_exists(db, "PaymentDetails", "base_document_id")) {
        execute("UPDATE SchemaVersion SET version = 2, "
                "updated_at = CURRENT_TIMESTAMP WHERE id = 1;");
    }
}

void DatabaseManager::applyPerformanceIndexes() {
    if (!db)
        return;

    struct IndexSpec {
        const char *table;
        const char *sql;
    };

    const std::vector<IndexSpec> indexes = {
        {"Contracts",
         "CREATE INDEX IF NOT EXISTS idx_contracts_number_date_cp "
         "ON Contracts(number, date, counterparty_id);"},
        {"Contracts",
         "CREATE INDEX IF NOT EXISTS idx_contracts_counterparty "
         "ON Contracts(counterparty_id);"},
        {"Contracts",
         "CREATE INDEX IF NOT EXISTS idx_contracts_review_flags "
         "ON Contracts(is_for_checking, is_for_special_control, is_found);"},
        {"Payments",
         "CREATE INDEX IF NOT EXISTS idx_payments_date_doc_amount_cp "
         "ON Payments(date, doc_number, amount, counterparty_id);"},
        {"Payments",
         "CREATE INDEX IF NOT EXISTS idx_payments_counterparty "
         "ON Payments(counterparty_id);"},
        {"Payments",
         "CREATE INDEX IF NOT EXISTS idx_payments_type_date "
         "ON Payments(type, date);"},
        {"Payments",
         "CREATE INDEX IF NOT EXISTS idx_payments_doc_number "
         "ON Payments(doc_number);"},
        {"PaymentDetails",
         "CREATE INDEX IF NOT EXISTS idx_payment_details_payment "
         "ON PaymentDetails(payment_id);"},
        {"PaymentDetails",
         "CREATE INDEX IF NOT EXISTS idx_payment_details_kosgu "
         "ON PaymentDetails(kosgu_id);"},
        {"PaymentDetails",
         "CREATE INDEX IF NOT EXISTS idx_payment_details_contract "
         "ON PaymentDetails(contract_id);"},
        {"PaymentDetails",
         "CREATE INDEX IF NOT EXISTS idx_payment_details_base_doc "
         "ON PaymentDetails(base_document_id);"},
        {"BasePaymentDocuments",
         "CREATE INDEX IF NOT EXISTS idx_base_docs_number_date_cp "
         "ON BasePaymentDocuments(number, date, counterparty_name);"},
        {"BasePaymentDocuments",
         "CREATE INDEX IF NOT EXISTS idx_base_docs_contract "
         "ON BasePaymentDocuments(contract_id);"},
        {"BasePaymentDocuments",
         "CREATE INDEX IF NOT EXISTS idx_base_docs_payment "
         "ON BasePaymentDocuments(payment_id);"},
        {"BasePaymentDocuments",
         "CREATE INDEX IF NOT EXISTS idx_base_docs_review_flags "
         "ON BasePaymentDocuments(is_for_checking, is_checked);"},
        {"PaymentBaseDocumentLinks",
         "CREATE INDEX IF NOT EXISTS idx_payment_base_doc_links_payment "
         "ON PaymentBaseDocumentLinks(payment_id);"},
        {"PaymentBaseDocumentLinks",
         "CREATE INDEX IF NOT EXISTS idx_payment_base_doc_links_document "
         "ON PaymentBaseDocumentLinks(base_document_id);"},
        {"PaymentBaseDocumentLinks",
         "CREATE INDEX IF NOT EXISTS idx_payment_base_doc_links_status "
         "ON PaymentBaseDocumentLinks(match_status);"},
        {"BasePaymentDocumentDetails",
         "CREATE INDEX IF NOT EXISTS idx_base_doc_details_document "
         "ON BasePaymentDocumentDetails(document_id);"},
        {"BasePaymentDocumentDetails",
         "CREATE INDEX IF NOT EXISTS idx_base_doc_details_kosgu "
         "ON BasePaymentDocumentDetails(kosgu_id);"},
        {"BasePaymentDocumentDetails",
         "CREATE INDEX IF NOT EXISTS idx_base_doc_details_signature "
         "ON BasePaymentDocumentDetails(document_id, operation_content, "
         "debit_account, credit_account, kosgu_id, amount);"}};

    for (const auto &index : indexes) {
        if (sqlite_table_exists(db, index.table)) {
            execute(index.sql);
        }
    }
}

void DatabaseManager::close() {
    if (db) {
        int rc = sqlite3_close(db);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to close database cleanly: "
                      << sqlite3_errmsg(db) << std::endl;
            sqlite3_close_v2(db);
        }
        db = nullptr;
    }
}

bool DatabaseManager::execute(const std::string &sql) {
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errmsg << std::endl;
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

bool DatabaseManager::is_open() const { return db != nullptr; }

bool DatabaseManager::beginTransaction() {
    return execute("BEGIN IMMEDIATE TRANSACTION;");
}

bool DatabaseManager::commitTransaction() {
    return execute("COMMIT;");
}

bool DatabaseManager::rollbackTransaction() {
    return execute("ROLLBACK;");
}

DatabaseManager::TransactionGuard::TransactionGuard(DatabaseManager& manager)
    : manager(manager), active(manager.beginTransaction()) {}

DatabaseManager::TransactionGuard::~TransactionGuard() {
    if (active) {
        manager.rollbackTransaction();
    }
}

bool DatabaseManager::TransactionGuard::started() const { return active; }

bool DatabaseManager::TransactionGuard::commit() {
    if (!active) {
        return false;
    }
    if (!manager.commitTransaction()) {
        manager.rollbackTransaction();
        active = false;
        return false;
    }
    active = false;
    return true;
}

void DatabaseManager::TransactionGuard::rollback() {
    if (active) {
        manager.rollbackTransaction();
        active = false;
    }
}

bool DatabaseManager::createDatabase(const std::string &filepath) {
    if (!open(filepath)) {
        return false;
    }

    std::vector<std::string> create_tables_sql = {
        // Справочник КОСГУ
        "CREATE TABLE IF NOT EXISTS KOSGU ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "kps TEXT DEFAULT '',"
        "code TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "note TEXT,"
        "UNIQUE(kps, code));",

        // Справочник контрагентов
        "CREATE TABLE IF NOT EXISTS Counterparties ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "inn TEXT UNIQUE,"
        "is_contract_optional INTEGER DEFAULT 0);",
        // Справочник договоров
        "CREATE TABLE IF NOT EXISTS Contracts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "number TEXT NOT NULL,"
        "date TEXT NOT NULL,"
        "counterparty_id INTEGER,"
        "contract_amount REAL DEFAULT 0.0,"
        "end_date TEXT,"
        "procurement_code TEXT,"
        "note TEXT,"
        "is_for_checking INTEGER DEFAULT 0,"
        "is_for_special_control INTEGER DEFAULT 0,"
        "is_found INTEGER DEFAULT 0,"
        "FOREIGN KEY(counterparty_id) REFERENCES Counterparties(id));",

        // Справочник платежей (банк)
        "CREATE TABLE IF NOT EXISTS Payments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "date TEXT NOT NULL,"
        "doc_number TEXT,"
        "type INTEGER NOT NULL DEFAULT 0,"
        "amount REAL NOT NULL,"
        "recipient TEXT,"
        "description TEXT,"
        "counterparty_id INTEGER,"
        "note TEXT,"
        "FOREIGN KEY(counterparty_id) REFERENCES Counterparties(id));",

        // Расшифровка платежа
        "CREATE TABLE IF NOT EXISTS PaymentDetails ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "payment_id INTEGER NOT NULL,"
        "kosgu_id INTEGER,"
        "contract_id INTEGER,"
        "base_document_id INTEGER,"
        "amount REAL NOT NULL,"
        "FOREIGN KEY(payment_id) REFERENCES Payments(id) ON DELETE CASCADE,"
        "FOREIGN KEY(kosgu_id) REFERENCES KOSGU(id),"
        "FOREIGN KEY(contract_id) REFERENCES Contracts(id),"
        "FOREIGN KEY(base_document_id) REFERENCES BasePaymentDocuments(id));",

        // Документы Основания Платежа (обобщённая сущность)
        "CREATE TABLE IF NOT EXISTS BasePaymentDocuments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "date TEXT NOT NULL,"
        "number TEXT NOT NULL,"
        "document_name TEXT,"
        "counterparty_name TEXT,"
        "contract_id INTEGER,"
        "payment_id INTEGER,"
        "note TEXT,"
        "is_for_checking INTEGER DEFAULT 0,"
        "is_checked INTEGER DEFAULT 0,"
        "FOREIGN KEY(contract_id) REFERENCES Contracts(id),"
        "FOREIGN KEY(payment_id) REFERENCES Payments(id));",

        // Расшифровка документа основания
        "CREATE TABLE IF NOT EXISTS BasePaymentDocumentDetails ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "document_id INTEGER NOT NULL,"
        "operation_content TEXT,"
        "debit_account TEXT,"
        "credit_account TEXT,"
        "kosgu_id INTEGER,"
        "amount REAL NOT NULL,"
        "note TEXT,"
        "FOREIGN KEY(document_id) REFERENCES BasePaymentDocuments(id) ON DELETE CASCADE,"
        "FOREIGN KEY(kosgu_id) REFERENCES KOSGU(id));",

        // Множественные связи платежей банка с документами ЖО4
        "CREATE TABLE IF NOT EXISTS PaymentBaseDocumentLinks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "payment_id INTEGER NOT NULL,"
        "base_document_id INTEGER NOT NULL,"
        "linked_amount REAL DEFAULT 0.0,"
        "match_score INTEGER DEFAULT 0,"
        "match_status TEXT DEFAULT 'suggested',"
        "match_reason TEXT,"
        "note TEXT,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(payment_id) REFERENCES Payments(id) ON DELETE CASCADE,"
        "FOREIGN KEY(base_document_id) REFERENCES BasePaymentDocuments(id) ON DELETE CASCADE,"
        "UNIQUE(payment_id, base_document_id));",

        // Справочник REGEX строк
        "CREATE TABLE IF NOT EXISTS Regexes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE,"
        "pattern TEXT NOT NULL);",

        // Справочник подозрительных слов
        "CREATE TABLE IF NOT EXISTS SuspiciousWords ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "word TEXT NOT NULL UNIQUE);"};

    for (const auto &sql : create_tables_sql) {
        if (!execute(sql)) {
            // Если одна из таблиц не создалась, закрываем соединение и
            // возвращаем ошибку
            close();
            return false;
        }
    }

    // Create Settings table if it doesn't exist and insert default row
    std::string create_settings_table_sql =
        "CREATE TABLE IF NOT EXISTS Settings ("
        "id INTEGER PRIMARY KEY,"
        "organization_name TEXT,"
        "period_start_date TEXT,"
        "period_end_date TEXT,"
        "note TEXT,"
        "import_preview_lines INTEGER DEFAULT 20,"
        "theme INTEGER DEFAULT 0,"
        "font_size INTEGER DEFAULT 24,"
        "zakupki_url_template TEXT,"
        "zakupki_url_search_template TEXT"
        ");";
    if (!execute(create_settings_table_sql)) {
        close();
        return false;
    }

    std::string insert_default_settings =
        "INSERT OR IGNORE INTO Settings (id, organization_name, "
        "period_start_date, period_end_date, note, import_preview_lines, "
        "theme, font_size, zakupki_url_template, zakupki_url_search_template) "
        "VALUES (1, '', '', '', '', 20, 0, 24, "
        "'https://zakupki.gov.ru/epz/contract/contractCard/"
        "common-info.html?reestrNumber={IKZ}', "
        "'https://zakupki.gov.ru/epz/contract/search/"
        "results.html?searchString={NUMBER}');";
    if (!execute(insert_default_settings)) {
        close();
        return false;
    }

    std::vector<std::string> default_regexes = {
        "INSERT OR IGNORE INTO Regexes (name, pattern) VALUES ('Контракты', "
        "'(?:по контракту|по контр|Контракт|дог\\.|К-т)(?: "
        "№)?\\s*([^\\s,]+)\\s*(?:от\\s*)?(\\d{2}\\.\\d{2}\\.(?:\\d{4}|\\d{2}))'"
        ");",
        "INSERT OR IGNORE INTO Regexes (name, pattern) VALUES ('КОСГУ', "
        "'К(\\d{3})');"};

    for (const auto &sql : default_regexes) {
        if (!execute(sql)) {
            close();
            return false;
        }
    }

    checkAndUpdateDatabaseSchema();

    return true;
}

// Callback функция для getKosguEntries
static int kosgu_select_callback(void *data, int argc, char **argv,
                                 char **azColName) {
    std::vector<Kosgu> *kosgu_list = static_cast<std::vector<Kosgu> *>(data);
    Kosgu entry;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id") {
            entry.id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "kps") {
            entry.kps = argv[i] ? argv[i] : "";
        } else if (colName == "code") {
            entry.code = argv[i] ? argv[i] : "";
        } else if (colName == "name") {
            entry.name = argv[i] ? argv[i] : "";
        } else if (colName == "note") {
            entry.note = argv[i] ? argv[i] : "";
        } else if (colName == "total_amount") {
            entry.total_amount = argv[i] ? std::stod(argv[i]) : 0.0;
        }
    }
    kosgu_list->push_back(entry);
    return 0;
}

std::vector<Kosgu> DatabaseManager::getKosguEntries() {
    std::vector<Kosgu> entries;
    if (!db)
        return entries;

    std::string sql =
        "SELECT k.id, k.kps, k.code, k.name, k.note, IFNULL(SUM(pd.amount), "
        "0.0) as total_amount "
        "FROM KOSGU k "
        "LEFT JOIN PaymentDetails pd ON k.id = pd.kosgu_id "
        "GROUP BY k.id, k.kps, k.code, k.name, k.note;";
    char *errmsg = nullptr;
    int rc =
        sqlite3_exec(db, sql.c_str(), kosgu_select_callback, &entries, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select KOSGU entries: " << errmsg << std::endl;
        sqlite3_free(errmsg);
    }
    return entries;
}

bool DatabaseManager::addKosguEntry(Kosgu &entry) {
    if (!db)
        return false;
    std::string sql =
        "INSERT INTO KOSGU (kps, code, name, note) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, entry.kps.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, entry.code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, entry.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, entry.note.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to add KOSGU entry: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    entry.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return true;
}

bool DatabaseManager::updateKosguEntry(const Kosgu &entry) {
    if (!db)
        return false;
    std::string sql =
        "UPDATE KOSGU SET kps = ?, code = ?, name = ?, note = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, entry.kps.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, entry.code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, entry.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, entry.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, entry.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to update KOSGU entry: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteKosguEntry(int id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM KOSGU WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to delete KOSGU entry: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        return false;
    }
    return true;
}

int DatabaseManager::getKosguIdByCode(const std::string &code) {
    if (!db)
        return -1;
    std::string sql =
        "SELECT id FROM KOSGU WHERE code = ? "
        "ORDER BY CASE WHEN IFNULL(kps, '') = '' THEN 0 ELSE 1 END, id "
        "LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for KOSGU lookup by code: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int DatabaseManager::getKosguIdByCodeAndKps(const std::string &code,
                                            const std::string &kps) {
    if (!db)
        return -1;
    std::string sql =
        "SELECT id FROM KOSGU WHERE code = ? AND IFNULL(kps, '') = ? LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for KOSGU lookup by code/kps: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, kps.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

// Counterparty CRUD
bool DatabaseManager::addCounterparty(Counterparty &counterparty) {
    if (!db)
        return false;
    std::string sql = "INSERT INTO Counterparties (name, inn, "
                      "is_contract_optional) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, counterparty.name.c_str(), -1, SQLITE_STATIC);
    if (counterparty.inn.empty()) {
        sqlite3_bind_null(stmt, 2);
    } else {
        sqlite3_bind_text(stmt, 2, counterparty.inn.c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, 3, counterparty.is_contract_optional ? 1 : 0);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to add counterparty: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    counterparty.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return true;
}

int DatabaseManager::getCounterpartyIdByNameInn(const std::string &name,
                                                const std::string &inn) {
    if (!db)
        return -1;
    std::string sql =
        "SELECT id FROM Counterparties WHERE name = ? AND inn = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, inn.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int DatabaseManager::getCounterpartyIdByName(const std::string &name) {
    if (!db)
        return -1;
    std::string sql =
        "SELECT id FROM Counterparties WHERE name = ? AND inn IS NULL;"; // Look
                                                                         // for
                                                                         // NULL
                                                                         // INN
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr
            << "Failed to prepare statement for counterparty lookup by name: "
            << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

// Callback функция для getCounterparties
static int counterparty_select_callback(void *data, int argc, char **argv,
                                        char **azColName) {
    std::vector<Counterparty> *counterparty_list =
        static_cast<std::vector<Counterparty> *>(data);
    Counterparty entry;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id") {
            entry.id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "name") {
            entry.name = argv[i] ? argv[i] : "";
        } else if (colName == "inn") {
            entry.inn = argv[i] ? argv[i] : "";
        } else if (colName == "is_contract_optional") {
            entry.is_contract_optional =
                argv[i] ? (std::stoi(argv[i]) == 1) : false;
        } else if (colName == "total_amount") {
            entry.total_amount = argv[i] ? std::stod(argv[i]) : 0.0;
        }
    }
    counterparty_list->push_back(entry);
    return 0;
}

std::vector<Counterparty> DatabaseManager::getCounterparties() {
    std::vector<Counterparty> entries;
    if (!db)
        return entries;

    std::string sql = "SELECT c.id, c.name, c.inn, c.is_contract_optional, "
                      "IFNULL(p_sum.total, 0.0) as total_amount "
                      "FROM Counterparties c "
                      "LEFT JOIN ( "
                      "    SELECT counterparty_id, SUM(amount) as total "
                      "    FROM Payments "
                      "    WHERE counterparty_id IS NOT NULL "
                      "    GROUP BY counterparty_id "
                      ") p_sum ON c.id = p_sum.counterparty_id;";
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), counterparty_select_callback,
                          &entries, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select Counterparty entries: " << errmsg
                  << std::endl;
        sqlite3_free(errmsg);
    }
    return entries;
}

bool DatabaseManager::updateCounterparty(const Counterparty &counterparty) {
    if (!db)
        return false;
    std::string sql = "UPDATE Counterparties SET name = ?, inn = ?, "
                      "is_contract_optional = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for counterparty update: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, counterparty.name.c_str(), -1, SQLITE_STATIC);
    if (counterparty.inn.empty()) {
        sqlite3_bind_null(stmt, 2);
    } else {
        sqlite3_bind_text(stmt, 2, counterparty.inn.c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, 3, counterparty.is_contract_optional ? 1 : 0);
    sqlite3_bind_int(stmt, 4, counterparty.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to update Counterparty entry: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteCounterparty(int id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM Counterparties WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete Counterparty entry: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

std::vector<ContractPaymentInfo>
DatabaseManager::getPaymentInfoForCounterparty(int counterparty_id) {
    std::vector<ContractPaymentInfo> results;
    if (!db)
        return results;

    std::string sql = "SELECT p.date, p.doc_number, pd.amount, p.description, "
                      "k.code AS kosgu_code "
                      "FROM Payments p "
                      "LEFT JOIN PaymentDetails pd ON p.id = pd.payment_id "
                      "LEFT JOIN KOSGU k ON pd.kosgu_id = k.id "
                      "WHERE p.counterparty_id = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr
            << "Failed to prepare statement for getPaymentInfoForCounterparty: "
            << sqlite3_errmsg(db) << std::endl;
        return results;
    }

    sqlite3_bind_int(stmt, 1, counterparty_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ContractPaymentInfo info;
        const char *date_text = (const char *)sqlite3_column_text(stmt, 0);
        const char *doc_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *desc_text = (const char *)sqlite3_column_text(stmt, 3);
        const char *kosgu_text = (const char *)sqlite3_column_text(stmt, 4);

        info.date = date_text ? date_text : "";
        info.doc_number = doc_text ? doc_text : "";
        info.amount = sqlite3_column_double(stmt, 2);
        info.description = desc_text ? desc_text : "";
        info.kosgu_code = kosgu_text ? kosgu_text : "";
        results.push_back(info);
    }

    sqlite3_finalize(stmt);
    return results;
}

// Contract CRUD
int DatabaseManager::addContract(Contract &contract) {
    if (!db)
        return -1;
    std::string sql = "INSERT INTO Contracts (number, date, counterparty_id, "
                      "contract_amount, end_date, procurement_code, note, "
                      "is_for_checking, is_for_special_control, is_found) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, contract.number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, contract.date.c_str(), -1, SQLITE_STATIC);
    if (contract.counterparty_id != -1) {
        sqlite3_bind_int(stmt, 3, contract.counterparty_id);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_double(stmt, 4, contract.contract_amount);
    sqlite3_bind_text(stmt, 5, contract.end_date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, contract.procurement_code.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, contract.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, contract.is_for_checking ? 1 : 0);
    sqlite3_bind_int(stmt, 9, contract.is_for_special_control ? 1 : 0);
    sqlite3_bind_int(stmt, 10, contract.is_found ? 1 : 0);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to add contract: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        sqlite3_finalize(stmt);
        return -1;
    }
    int id = sqlite3_last_insert_rowid(db);
    contract.id = id; // Записываем ID в объект для последующего использования
    sqlite3_finalize(stmt);
    return id;
}

int DatabaseManager::getContractIdByNumberDate(const std::string &number,
                                               const std::string &date) {
    if (!db)
        return -1;
    std::string sql = "SELECT id FROM Contracts WHERE number = ? AND date = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int DatabaseManager::updateContractProcurementCode(
    const std::string &number, const std::string &date,
    const std::string &procurement_code) {
    if (!db)
        return 0;
    std::string sql =
        "UPDATE Contracts SET procurement_code = ? WHERE number = ? AND "
        "(date = ? OR date = ?) AND (procurement_code IS NULL OR "
        "procurement_code = '');";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr
            << "Failed to prepare statement for contract procurement code "
               "update: "
            << sqlite3_errmsg(db) << std::endl;
        return 0;
    }

    sqlite3_bind_text(stmt, 1, procurement_code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, date.c_str(), -1, SQLITE_STATIC);
    // Передаем дату в формате dd.mm.yyyy для альтернативного поиска
    std::string date_ddmmyyyy;
    if (date.length() == 10 && date[4] == '-' && date[7] == '-') {
        // Конвертируем yyyy-mm-dd -> dd.mm.yyyy
        date_ddmmyyyy = date.substr(8, 2) + "." + date.substr(5, 2) + "." +
                        date.substr(0, 4);
    }
    sqlite3_bind_text(stmt, 4, date_ddmmyyyy.c_str(), -1, SQLITE_STATIC);

    int rc_step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc_step != SQLITE_DONE) {
        std::cerr << "Failed to update Contract procurement code: "
                  << sqlite3_errmsg(db) << std::endl;
        return 0;
    }

    return sqlite3_changes(db);
}

bool DatabaseManager::updateContractProcurementCode(
    int contract_id, const std::string &procurement_code) {
    if (!db)
        return false;

    std::string sql = "UPDATE Contracts SET procurement_code = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for "
                     "updateContractProcurementCode by id: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, procurement_code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, contract_id);

    int rc_step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc_step != SQLITE_DONE) {
        std::cerr << "Failed to update Contract procurement code by id: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

// Callback функция для getContracts
static int contract_select_callback(void *data, int argc, char **argv,
                                    char **azColName) {
    std::vector<Contract> *contract_list =
        static_cast<std::vector<Contract> *>(data);
    Contract entry;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id") {
            entry.id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "number") {
            entry.number = argv[i] ? argv[i] : "";
        } else if (colName == "date") {
            entry.date = argv[i] ? argv[i] : "";
        } else if (colName == "counterparty_id") {
            entry.counterparty_id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "total_amount") {
            entry.total_amount = argv[i] ? std::stod(argv[i]) : 0.0;
        } else if (colName == "contract_amount") {
            entry.contract_amount = argv[i] ? std::stod(argv[i]) : 0.0;
        } else if (colName == "end_date") {
            entry.end_date = argv[i] ? argv[i] : "";
        } else if (colName == "procurement_code") {
            entry.procurement_code = argv[i] ? argv[i] : "";
        } else if (colName == "note") {
            entry.note = argv[i] ? argv[i] : "";
        } else if (colName == "is_for_checking") {
            entry.is_for_checking = argv[i] ? (std::stoi(argv[i]) == 1) : false;
        } else if (colName == "is_for_special_control") {
            entry.is_for_special_control =
                argv[i] ? (std::stoi(argv[i]) == 1) : false;
        } else if (colName == "is_found") {
            entry.is_found = argv[i] ? (std::stoi(argv[i]) == 1) : false;
        }
    }
    contract_list->push_back(entry);
    return 0;
}

std::vector<Contract> DatabaseManager::getContracts() {
    std::vector<Contract> entries;
    if (!db)
        return entries;

    std::string sql = "SELECT c.id, c.number, c.date, c.counterparty_id, "
                      "c.contract_amount, c.end_date, c.procurement_code, "
                      "c.note, c.is_for_checking, c.is_for_special_control, "
                      "c.is_found, IFNULL(SUM(pd.amount), 0.0) as total_amount "
                      "FROM Contracts c "
                      "LEFT JOIN PaymentDetails pd ON c.id = pd.contract_id "
                      "GROUP BY c.id, c.number, c.date, c.counterparty_id;";
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), contract_select_callback, &entries,
                          &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select Contract entries: " << errmsg
                  << std::endl;
        sqlite3_free(errmsg);
    }
    return entries;
}

bool DatabaseManager::updateContract(const Contract &contract) {
    if (!db)
        return false;
    std::string sql = "UPDATE Contracts SET number = ?, date = ?, "
                      "counterparty_id = ?, contract_amount = ?, end_date = ?, "
                      "procurement_code = ?, note = ?, is_for_checking = ?, "
                      "is_for_special_control = ?, is_found = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for contract update: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, contract.number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, contract.date.c_str(), -1, SQLITE_STATIC);
    if (contract.counterparty_id != -1) {
        sqlite3_bind_int(stmt, 3, contract.counterparty_id);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_double(stmt, 4, contract.contract_amount);
    sqlite3_bind_text(stmt, 5, contract.end_date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, contract.procurement_code.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, contract.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, contract.is_for_checking ? 1 : 0);
    sqlite3_bind_int(stmt, 9, contract.is_for_special_control ? 1 : 0);
    sqlite3_bind_int(stmt, 10, contract.is_found ? 1 : 0);
    sqlite3_bind_int(stmt, 11, contract.id);

    int rc_step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc_step != SQLITE_DONE) {
        std::cerr << "Failed to update Contract entry: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteContract(int id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM Contracts WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);

    int rc_step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc_step != SQLITE_DONE) {
        std::cerr << "Failed to delete Contract entry: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::updateContractFlags(int contract_id, bool is_for_checking,
                                          bool is_for_special_control) {
    if (!db)
        return false;

    std::string sql = "UPDATE Contracts SET is_for_checking = ?, "
                      "is_for_special_control = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for updateContractFlags: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, is_for_checking ? 1 : 0);
    sqlite3_bind_int(stmt, 2, is_for_special_control ? 1 : 0);
    sqlite3_bind_int(stmt, 3, contract_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to update contract flags: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    return true;
}

void DatabaseManager::transferPaymentDetails(int from_contract_id,
                                             int to_contract_id) {
    if (!db)
        return;

    std::string sql =
        "UPDATE PaymentDetails SET contract_id = ? WHERE contract_id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr
            << "Failed to prepare statement for transferring payment details: "
            << sqlite3_errmsg(db) << std::endl;
        return;
    }

    sqlite3_bind_int(stmt, 1, to_contract_id);
    sqlite3_bind_int(stmt, 2, from_contract_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to transfer payment details: "
                  << sqlite3_errmsg(db) << std::endl;
    }
}

std::vector<ContractPaymentInfo>
DatabaseManager::getPaymentInfoForContract(int contract_id) {
    std::vector<ContractPaymentInfo> results;
    if (!db)
        return results;

    std::string sql =
        "SELECT p.date, p.doc_number, pd.amount, p.description, k.code "
        "FROM Payments p "
        "JOIN PaymentDetails pd ON p.id = pd.payment_id "
        "LEFT JOIN KOSGU k ON pd.kosgu_id = k.id "
        "WHERE pd.contract_id = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr
            << "Failed to prepare statement for getPaymentInfoForContract: "
            << sqlite3_errmsg(db) << std::endl;
        return results;
    }

    sqlite3_bind_int(stmt, 1, contract_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ContractPaymentInfo info;
        const char *date_text = (const char *)sqlite3_column_text(stmt, 0);
        const char *doc_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *desc_text = (const char *)sqlite3_column_text(stmt, 3);
        const char *kosgu_text = (const char *)sqlite3_column_text(stmt, 4);

        info.date = date_text ? date_text : "";
        info.doc_number = doc_text ? doc_text : "";
        info.amount = sqlite3_column_double(stmt, 2);
        info.description = desc_text ? desc_text : "";
        info.kosgu_code = kosgu_text ? kosgu_text : "";
        results.push_back(info);
    }

    sqlite3_finalize(stmt);
    return results;
}

// Payment CRUD
bool DatabaseManager::addPayment(Payment &payment) {
    if (!db)
        return false;

    // std::cout << "DB: ADDING PAYMENT ->"
    //           << " Date: " << payment.date << ", Type: " << payment.type
    //           << ", Amount: " << payment.amount
    //           << ", Desc: " << payment.description << std::endl;

    std::string sql = "INSERT INTO Payments (date, doc_number, type, amount, "
                      "recipient, description, counterparty_id, note) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, payment.date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, payment.doc_number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, payment.type ? 1 : 0);
    sqlite3_bind_double(stmt, 4, payment.amount);
    sqlite3_bind_text(stmt, 5, payment.recipient.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, payment.description.c_str(), -1, SQLITE_STATIC);
    if (payment.counterparty_id != -1) {
        sqlite3_bind_int(stmt, 7, payment.counterparty_id);
    } else {
        sqlite3_bind_null(stmt, 7);
    }
    sqlite3_bind_text(stmt, 8, payment.note.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to add payment: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    payment.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return true;
}

// Callback for getPayments
static int payment_select_callback(void *data, int argc, char **argv,
                                   char **azColName) {
    auto *payments = static_cast<std::vector<Payment> *>(data);
    Payment p;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id")
            p.id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "date")
            p.date = argv[i] ? argv[i] : "";
        else if (colName == "doc_number")
            p.doc_number = argv[i] ? argv[i] : "";
        else if (colName == "type")
            p.type = (argv[i] && std::stoi(argv[i]) == 1);
        else if (colName == "amount")
            p.amount = argv[i] ? std::stod(argv[i]) : 0.0;
        else if (colName == "recipient")
            p.recipient = argv[i] ? argv[i] : "";
        else if (colName == "description")
            p.description = argv[i] ? argv[i] : "";
        else if (colName == "counterparty_id")
            p.counterparty_id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "note")
            p.note = argv[i] ? argv[i] : "";
    }
    payments->push_back(p);
    return 0;
}

std::vector<Payment> DatabaseManager::getPayments() {
    std::vector<Payment> payments;
    if (!db)
        return payments;

    std::string sql = "SELECT id, date, doc_number, type, amount, recipient, "
                      "description, counterparty_id, note FROM Payments;";
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), payment_select_callback, &payments,
                          &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select Payments: " << errmsg << std::endl;
        sqlite3_free(errmsg);
    }
    return payments;
}

bool DatabaseManager::updatePayment(const Payment &payment) {
    if (!db)
        return false;
    std::string sql =
        "UPDATE Payments SET date = ?, doc_number = ?, type = ?, amount = ?, "
        "recipient = ?, description = ?, counterparty_id = ?, note = ? "
        "WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for payment update: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, payment.date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, payment.doc_number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, payment.type ? 1 : 0);
    sqlite3_bind_double(stmt, 4, payment.amount);
    sqlite3_bind_text(stmt, 5, payment.recipient.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, payment.description.c_str(), -1, SQLITE_STATIC);
    if (payment.counterparty_id != -1) {
        sqlite3_bind_int(stmt, 7, payment.counterparty_id);
    } else {
        sqlite3_bind_null(stmt, 7);
    }
    sqlite3_bind_text(stmt, 8, payment.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, payment.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to update payment: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deletePayment(int id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM Payments WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for payment delete: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        int extended_code = sqlite3_extended_errcode(db);
        std::cerr << "Failed to delete payment: " << sqlite3_errmsg(db)
                  << " (code: " << rc << ", extended code: " << extended_code
                  << ")" << std::endl;
        return false;
    }
    return true;
}

std::vector<ContractPaymentInfo>
DatabaseManager::getPaymentInfoForKosgu(int kosgu_id) {
    std::vector<ContractPaymentInfo> results;
    if (!db)
        return results;

    std::string sql =
        "SELECT p.date, p.doc_number, pd.amount, p.description, c.name "
        "FROM Payments p "
        "JOIN PaymentDetails pd ON p.id = pd.payment_id "
        "LEFT JOIN Counterparties c ON p.counterparty_id = c.id "
        "WHERE pd.kosgu_id = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getPaymentInfoForKosgu: "
                  << sqlite3_errmsg(db) << std::endl;
        return results;
    }

    sqlite3_bind_int(stmt, 1, kosgu_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ContractPaymentInfo info;
        const char *date_text = (const char *)sqlite3_column_text(stmt, 0);
        const char *doc_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *desc_text = (const char *)sqlite3_column_text(stmt, 3);
        const unsigned char *counterparty_name_text =
            sqlite3_column_text(stmt, 4);

        info.date = date_text ? date_text : "";
        info.doc_number = doc_text ? doc_text : "";
        info.amount = sqlite3_column_double(stmt, 2);
        info.description = desc_text ? desc_text : "";
        info.counterparty_name =
            counterparty_name_text ? (const char *)counterparty_name_text : "";
        results.push_back(info);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<ContractPaymentInfo>
DatabaseManager::getDecodingForKosgu(int kosgu_id,
                                     const std::string &filterText) {
    std::vector<ContractPaymentInfo> results;
    if (!db)
        return results;

    std::string sql =
        "SELECT k.code AS kosgu_code, p.date, p.doc_number, pd.amount, "
        "p.description, c.name AS counterparty_name "
        "FROM PaymentDetails pd "
        "JOIN Payments p ON pd.payment_id = p.id "
        "JOIN KOSGU k ON pd.kosgu_id = k.id "
        "LEFT JOIN Counterparties c ON p.counterparty_id = c.id "
        "WHERE pd.kosgu_id = ?";

    if (!filterText.empty()) {
        sql += " AND (LOWER(p.date) LIKE LOWER(?) OR LOWER(p.doc_number) LIKE "
               "LOWER(?) "
               "OR LOWER(pd.amount) LIKE LOWER(?) OR LOWER(p.description) LIKE "
               "LOWER(?) "
               "OR LOWER(c.name) LIKE LOWER(?) OR LOWER(k.code) LIKE LOWER(?))";
    }

    sql += ";";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getDecodingForKosgu: "
                  << sqlite3_errmsg(db) << std::endl;
        return results;
    }

    sqlite3_bind_int(stmt, 1, kosgu_id);

    if (!filterText.empty()) {
        std::string filterParam = "%" + filterText + "%";
        for (int i = 0; i < 6; i++) {
            sqlite3_bind_text(stmt, 2 + i, filterParam.c_str(), -1,
                              SQLITE_TRANSIENT);
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ContractPaymentInfo info;
        const char *kosgu_text = (const char *)sqlite3_column_text(stmt, 0);
        const char *date_text = (const char *)sqlite3_column_text(stmt, 1);
        const char *doc_text = (const char *)sqlite3_column_text(stmt, 2);
        const char *desc_text = (const char *)sqlite3_column_text(stmt, 4);
        const unsigned char *counterparty_name_text =
            sqlite3_column_text(stmt, 5);

        info.kosgu_code = kosgu_text ? kosgu_text : "";
        info.date = date_text ? date_text : "";
        info.doc_number = doc_text ? doc_text : "";
        info.amount = sqlite3_column_double(stmt, 3);
        info.description = desc_text ? desc_text : "";
        info.counterparty_name =
            counterparty_name_text ? (const char *)counterparty_name_text : "";
        results.push_back(info);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<KosguPaymentDetailInfo> DatabaseManager::getAllKosguPaymentInfo() {
    std::vector<KosguPaymentDetailInfo> results;
    if (!db)
        return results;

    std::string sql = "SELECT pd.kosgu_id, p.date, p.doc_number, pd.amount, "
                      "p.description, c.name "
                      "FROM PaymentDetails pd "
                      "JOIN Payments p ON pd.payment_id = p.id "
                      "LEFT JOIN Counterparties c ON p.counterparty_id = c.id "
                      "WHERE pd.kosgu_id IS NOT NULL;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getAllKosguPaymentInfo: "
                  << sqlite3_errmsg(db) << std::endl;
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        KosguPaymentDetailInfo info;
        info.kosgu_id = sqlite3_column_int(stmt, 0);
        const unsigned char *date_text = sqlite3_column_text(stmt, 1);
        info.date = date_text ? (const char *)date_text : "";
        const unsigned char *doc_num_text = sqlite3_column_text(stmt, 2);
        info.doc_number = doc_num_text ? (const char *)doc_num_text : "";
        info.amount = sqlite3_column_double(stmt, 3);
        const unsigned char *desc_text = sqlite3_column_text(stmt, 4);
        info.description = desc_text ? (const char *)desc_text : "";
        const unsigned char *counterparty_name_text =
            sqlite3_column_text(stmt, 5);
        info.counterparty_name =
            counterparty_name_text ? (const char *)counterparty_name_text : "";
        results.push_back(info);
    }

    sqlite3_finalize(stmt);
    return results;
}

static int contract_payment_detail_info_select_callback(void *data, int argc,
                                                        char **argv,
                                                        char **azColName) {
    auto *results = static_cast<std::vector<ContractPaymentInfo> *>(data);
    ContractPaymentInfo info;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "contract_id") {
            info.contract_id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "date") {
            info.date = argv[i] ? argv[i] : "";
        } else if (colName == "doc_number") {
            info.doc_number = argv[i] ? argv[i] : "";
        } else if (colName == "amount") {
            info.amount = argv[i] ? std::stod(argv[i]) : 0.0;
        } else if (colName == "description") {
            info.description = argv[i] ? argv[i] : "";
        } else if (colName == "kosgu_code") {
            info.kosgu_code = argv[i] ? argv[i] : "";
        }
    }
    results->push_back(info);
    return 0;
}

std::vector<ContractPaymentInfo> DatabaseManager::getAllContractPaymentInfo() {
    std::vector<ContractPaymentInfo> results;
    if (!db)
        return results;

    std::string sql = "SELECT pd.contract_id, p.date, p.doc_number, pd.amount, "
                      "p.description, k.code AS kosgu_code "
                      "FROM PaymentDetails pd "
                      "JOIN Payments p ON pd.payment_id = p.id "
                      "LEFT JOIN KOSGU k ON pd.kosgu_id = k.id "
                      "WHERE pd.contract_id IS NOT NULL;";

    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(),
                          contract_payment_detail_info_select_callback,
                          &results, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select getAllContractPaymentInfo: " << errmsg
                  << std::endl;
        sqlite3_free(errmsg);
    }
    return results;
}

static int counterparty_payment_detail_info_select_callback(void *data,
                                                            int argc,
                                                            char **argv,
                                                            char **azColName) {
    auto *results = static_cast<std::vector<CounterpartyPaymentInfo> *>(data);
    CounterpartyPaymentInfo info;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "counterparty_id") {
            info.counterparty_id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "date") {
            info.date = argv[i] ? argv[i] : "";
        } else if (colName == "doc_number") {
            info.doc_number = argv[i] ? argv[i] : "";
        } else if (colName == "amount") {
            info.amount = argv[i] ? std::stod(argv[i]) : 0.0;
        } else if (colName == "description") {
            info.description = argv[i] ? argv[i] : "";
        } else if (colName == "kosgu_code") {
            info.kosgu_code = argv[i] ? argv[i] : "";
        }
    }
    results->push_back(info);
    return 0;
}

std::vector<CounterpartyPaymentInfo>
DatabaseManager::getAllCounterpartyPaymentInfo() {
    std::vector<CounterpartyPaymentInfo> results;
    if (!db)
        return results;

    std::string sql = "SELECT p.counterparty_id, p.date, p.doc_number, "
                      "pd.amount, p.description, k.code AS kosgu_code "
                      "FROM Payments p "
                      "LEFT JOIN PaymentDetails pd ON p.id = pd.payment_id "
                      "LEFT JOIN KOSGU k ON pd.kosgu_id = k.id "
                      "WHERE p.counterparty_id IS NOT NULL;";

    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(),
                          counterparty_payment_detail_info_select_callback,
                          &results, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select getAllCounterpartyPaymentInfo: "
                  << errmsg << std::endl;
        sqlite3_free(errmsg);
    }
    return results;
}

std::vector<ContractExportData> DatabaseManager::getContractsForExport() {
    std::vector<ContractExportData> results;
    if (!db)
        return results;

    // 1. Get all contracts marked for checking
    std::string sql_contracts =
        "SELECT id, number, date, counterparty_id, is_for_special_control, "
        "note, procurement_code FROM Contracts WHERE is_for_checking = 1;";
    sqlite3_stmt *stmt_contracts = nullptr;
    if (sqlite3_prepare_v2(db, sql_contracts.c_str(), -1, &stmt_contracts,
                           nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getContractsForExport: "
                  << sqlite3_errmsg(db) << std::endl;
        return results;
    }

    while (sqlite3_step(stmt_contracts) == SQLITE_ROW) {
        ContractExportData data;
        int contract_id = sqlite3_column_int(stmt_contracts, 0);
        data.contract_number =
            (const char *)sqlite3_column_text(stmt_contracts, 1);
        data.contract_date =
            (const char *)sqlite3_column_text(stmt_contracts, 2);
        int counterparty_id = sqlite3_column_int(stmt_contracts, 3);
        data.is_for_special_control =
            sqlite3_column_int(stmt_contracts, 4) == 1;
        const unsigned char *note_text = sqlite3_column_text(stmt_contracts, 5);
        data.note = note_text ? (const char *)note_text : "";
        const unsigned char *pk_text = sqlite3_column_text(stmt_contracts, 6);
        data.procurement_code = pk_text ? (const char *)pk_text : "";

        // 2. Get counterparty name
        if (counterparty_id != 0) {
            std::string sql_cp =
                "SELECT name FROM Counterparties WHERE id = ?;";
            sqlite3_stmt *stmt_cp = nullptr;
            if (sqlite3_prepare_v2(db, sql_cp.c_str(), -1, &stmt_cp, nullptr) ==
                SQLITE_OK) {
                sqlite3_bind_int(stmt_cp, 1, counterparty_id);
                if (sqlite3_step(stmt_cp) == SQLITE_ROW) {
                    const unsigned char *cp_name =
                        sqlite3_column_text(stmt_cp, 0);
                    data.counterparty_name =
                        cp_name ? (const char *)cp_name : "";
                }
                sqlite3_finalize(stmt_cp);
            }
        }

        // 3. Get KOSGU codes
        std::string sql_kosgu =
            "SELECT DISTINCT k.code FROM KOSGU k JOIN PaymentDetails pd ON "
            "k.id = pd.kosgu_id WHERE pd.contract_id = ?;";
        sqlite3_stmt *stmt_kosgu = nullptr;
        if (sqlite3_prepare_v2(db, sql_kosgu.c_str(), -1, &stmt_kosgu,
                               nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt_kosgu, 1, contract_id);
            std::string kosgu_list;
            while (sqlite3_step(stmt_kosgu) == SQLITE_ROW) {
                const unsigned char *code = sqlite3_column_text(stmt_kosgu, 0);
                if (code) {
                    if (!kosgu_list.empty()) {
                        kosgu_list += ", ";
                    }
                    kosgu_list += (const char *)code;
                }
            }
            data.kosgu_codes = kosgu_list;
            sqlite3_finalize(stmt_kosgu);
        }

        results.push_back(data);
    }

    sqlite3_finalize(stmt_contracts);
    return results;
}

// PaymentDetail CRUD
bool DatabaseManager::addPaymentDetail(PaymentDetail &detail) {
    if (!db)
        return false;
    std::string sql =
        "INSERT INTO PaymentDetails (payment_id, kosgu_id, contract_id, "
        "base_document_id, amount) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for payment detail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, detail.payment_id);
    bind_optional_id(stmt, 2, detail.kosgu_id);
    bind_optional_id(stmt, 3, detail.contract_id);
    bind_optional_id(stmt, 4, detail.base_document_id);
    sqlite3_bind_double(stmt, 5, detail.amount);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to add payment detail: " << sqlite3_errmsg(db)
                  << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    detail.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return true;
}

static int payment_detail_select_callback(void *data, int argc, char **argv,
                                          char **azColName) {
    auto *details = static_cast<std::vector<PaymentDetail> *>(data);
    PaymentDetail pd;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id")
            pd.id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "payment_id")
            pd.payment_id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "kosgu_id")
            pd.kosgu_id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "contract_id")
            pd.contract_id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "base_document_id" || colName == "invoice_id")
            pd.base_document_id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "amount")
            pd.amount = argv[i] ? std::stod(argv[i]) : 0.0;
    }
    details->push_back(pd);
    return 0;
}

std::vector<PaymentDetail> DatabaseManager::getPaymentDetails(int payment_id) {
    std::vector<PaymentDetail> details;
    if (!db)
        return details;

    std::string sql = "SELECT * FROM PaymentDetails WHERE payment_id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getting payment details: "
                  << sqlite3_errmsg(db) << std::endl;
        return details;
    }
    sqlite3_bind_int(stmt, 1, payment_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PaymentDetail pd;
        pd.id = sqlite3_column_int(stmt, 0);
        pd.payment_id = sqlite3_column_int(stmt, 1);
        pd.kosgu_id = sqlite3_column_type(stmt, 2) == SQLITE_NULL
                          ? -1
                          : sqlite3_column_int(stmt, 2);
        pd.contract_id = sqlite3_column_type(stmt, 3) == SQLITE_NULL
                             ? -1
                             : sqlite3_column_int(stmt, 3);
        pd.base_document_id = sqlite3_column_type(stmt, 4) == SQLITE_NULL
                                  ? -1
                                  : sqlite3_column_int(stmt, 4);
        pd.amount = sqlite3_column_double(stmt, 5);
        details.push_back(pd);
    }

    sqlite3_finalize(stmt);
    return details;
}

std::vector<PaymentDetail> DatabaseManager::getAllPaymentDetails() {
    std::vector<PaymentDetail> details;
    if (!db)
        return details;

    std::string sql = "SELECT * FROM PaymentDetails;";
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), payment_detail_select_callback,
                          &details, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select all PaymentDetails: " << errmsg
                  << std::endl;
        sqlite3_free(errmsg);
    }
    return details;
}

bool DatabaseManager::updatePaymentDetail(const PaymentDetail &detail) {
    if (!db)
        return false;
    std::string sql = "UPDATE PaymentDetails SET kosgu_id = ?, contract_id = "
                      "?, base_document_id = ?, amount = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for updating payment detail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    bind_optional_id(stmt, 1, detail.kosgu_id);
    bind_optional_id(stmt, 2, detail.contract_id);
    bind_optional_id(stmt, 3, detail.base_document_id);
    sqlite3_bind_double(stmt, 4, detail.amount);
    sqlite3_bind_int(stmt, 5, detail.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to update payment detail: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deletePaymentDetail(int id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM PaymentDetails WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for deleting payment detail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete payment detail: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteAllPaymentDetails(int payment_id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM PaymentDetails WHERE payment_id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr
            << "Failed to prepare statement for deleting all payment details: "
            << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, payment_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete all payment details: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::bulkUpdatePaymentDetails(
    const std::vector<int> &payment_ids, const std::string &field_to_update,
    int new_id) {
    if (!db || payment_ids.empty()) {
        return false;
    }

    // Basic validation to prevent SQL injection
    if (field_to_update != "kosgu_id" && field_to_update != "contract_id" &&
        field_to_update != "base_document_id") {
        std::cerr << "Invalid field to update: " << field_to_update
                  << std::endl;
        return false;
    }

    std::string sql = "UPDATE PaymentDetails SET " + field_to_update +
                      " = ? WHERE payment_id IN (";
    for (size_t i = 0; i < payment_ids.size(); ++i) {
        sql += (i == 0 ? "?" : ",?");
    }
    sql += ");";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for bulk update: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // Bind the new_id for the SET part
    sqlite3_bind_int(stmt, 1, new_id);

    // Bind all payment_ids for the IN clause
    for (size_t i = 0; i < payment_ids.size(); ++i) {
        sqlite3_bind_int(stmt, i + 2, payment_ids[i]);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute bulk update: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }

    return true;
}

// Regex CRUD
static int regex_select_callback(void *data, int argc, char **argv,
                                 char **azColName) {
    std::vector<Regex> *regex_list = static_cast<std::vector<Regex> *>(data);
    Regex entry;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id") {
            entry.id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "name") {
            entry.name = argv[i] ? argv[i] : "";
        } else if (colName == "pattern") {
            entry.pattern = argv[i] ? argv[i] : "";
        }
    }
    regex_list->push_back(entry);
    return 0;
}

std::vector<Regex> DatabaseManager::getRegexes() {
    std::vector<Regex> entries;
    if (!db)
        return entries;

    std::string sql = "SELECT id, name, pattern FROM Regexes;";
    char *errmsg = nullptr;
    int rc =
        sqlite3_exec(db, sql.c_str(), regex_select_callback, &entries, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select Regex entries: " << errmsg << std::endl;
        sqlite3_free(errmsg);
    }
    return entries;
}

bool DatabaseManager::addRegex(Regex &regex) {
    if (!db)
        return false;

    std::cout << "regex: " << regex.name << " " << regex.pattern << std::endl;
    std::string sql = "INSERT INTO Regexes (name, pattern) VALUES (?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, regex.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, regex.pattern.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return false;
    }
    regex.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return true;
}

bool DatabaseManager::updateRegex(const Regex &regex) {
    if (!db)
        return false;
    std::string sql = "UPDATE Regexes SET name = ?, pattern = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, regex.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, regex.pattern.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, regex.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool DatabaseManager::deleteRegex(int id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM Regexes WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

int DatabaseManager::getRegexIdByName(const std::string &name) {
    if (!db)
        return -1;
    std::string sql = "SELECT id FROM Regexes WHERE name = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for regex lookup by name: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

// Suspicious Words CRUD
static int suspicious_word_select_callback(void *data, int argc, char **argv,
                                           char **azColName) {
    auto *words = static_cast<std::vector<SuspiciousWord> *>(data);
    SuspiciousWord sw;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id") {
            sw.id = argv[i] ? std::stoi(argv[i]) : -1;
        } else if (colName == "word") {
            sw.word = argv[i] ? argv[i] : "";
        }
    }
    words->push_back(sw);
    return 0;
}

std::vector<SuspiciousWord> DatabaseManager::getSuspiciousWords() {
    std::vector<SuspiciousWord> words;
    if (!db)
        return words;

    std::string sql = "SELECT id, word FROM SuspiciousWords;";
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), suspicious_word_select_callback,
                          &words, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to select SuspiciousWords: " << errmsg
                  << std::endl;
        sqlite3_free(errmsg);
    }
    return words;
}

bool DatabaseManager::addSuspiciousWord(SuspiciousWord &word) {
    if (!db)
        return false;
    std::string sql = "INSERT INTO SuspiciousWords (word) VALUES (?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, word.word.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return false;
    }
    word.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return true;
}

bool DatabaseManager::updateSuspiciousWord(const SuspiciousWord &word) {
    if (!db)
        return false;
    std::string sql = "UPDATE SuspiciousWords SET word = ? WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, word.word.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, word.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool DatabaseManager::deleteSuspiciousWord(int id) {
    if (!db)
        return false;
    std::string sql = "DELETE FROM SuspiciousWords WHERE id = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

int DatabaseManager::getSuspiciousWordIdByWord(const std::string &word) {
    if (!db)
        return -1;
    std::string sql = "SELECT id FROM SuspiciousWords WHERE word = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for suspicious word lookup "
                     "by word: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

// Maintenance methods
bool DatabaseManager::ClearPayments() {
    if (!db)
        return false;
    // Delete details first, although ON DELETE CASCADE should handle it.
    bool success = execute("DELETE FROM PaymentDetails;");
    if (success) {
        success = execute("DELETE FROM Payments;");
    }
    if (success) {
        execute("VACUUM;");
    }
    return success;
}

bool DatabaseManager::ClearCounterparties() {
    if (!db)
        return false;
    // Note: This can fail if foreign key constraints are violated.
    // The UI should warn the user about this.
    bool success = execute("DELETE FROM Counterparties;");
    if (success)
        execute("VACUUM;");
    return success;
}

bool DatabaseManager::ClearContracts() {
    if (!db)
        return false;
    bool success = execute("DELETE FROM Contracts;");
    if (success)
        execute("VACUUM;");
    return success;
}

bool DatabaseManager::ClearBasePaymentDocuments() {
    if (!db)
        return false;
    // Сначала удаляем расшифровки, затем сами документы
    bool success = execute("DELETE FROM BasePaymentDocumentDetails;");
    if (success)
        success = execute("DELETE FROM BasePaymentDocuments;");
    if (success)
        execute("VACUUM;");
    return success;
}

bool DatabaseManager::CleanOrphanPaymentDetails() {
    if (!db)
        return false;
    // Deletes details for which the corresponding payment does not exist.
    bool success = execute("DELETE FROM PaymentDetails WHERE payment_id NOT IN "
                           "(SELECT id FROM Payments);");
    if (success)
        execute("VACUUM;");
    return success;
}

std::vector<IntegrityIssue> DatabaseManager::getIntegrityReport() {
    std::vector<IntegrityIssue> issues;
    if (!db) {
        issues.push_back({"Ошибка", "База данных", "База не открыта"});
        return issues;
    }

    auto add_issue = [&](const std::string &severity,
                         const std::string &check_name,
                         const std::string &details) {
        issues.push_back({severity, check_name, details});
    };

    auto add_count_issue = [&](const std::string &severity,
                               const std::string &check_name,
                               const std::string &sql,
                               const std::string &details_prefix,
                               const std::string &examples_sql = "") {
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            add_issue("Ошибка", check_name,
                      "Не удалось выполнить проверку: " +
                          std::string(sqlite3_errmsg(db)));
            return;
        }

        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);

        if (count > 0) {
            std::string details =
                details_prefix + ": " + std::to_string(count);

            if (!examples_sql.empty()) {
                sqlite3_stmt *examples_stmt = nullptr;
                rc = sqlite3_prepare_v2(db, examples_sql.c_str(), -1,
                                        &examples_stmt, nullptr);
                if (rc == SQLITE_OK) {
                    std::vector<std::string> examples;
                    while (sqlite3_step(examples_stmt) == SQLITE_ROW) {
                        const unsigned char *value =
                            sqlite3_column_text(examples_stmt, 0);
                        examples.push_back(value ? reinterpret_cast<const char *>(value)
                                                 : "");
                    }
                    sqlite3_finalize(examples_stmt);

                    if (!examples.empty()) {
                        details += "\nПримеры:";
                        for (const auto &example : examples) {
                            details += "\n- " + example;
                        }
                    }
                } else {
                    details += "\nПримеры не получены: ";
                    details += sqlite3_errmsg(db);
                }
            }

            add_issue(severity, check_name, details);
        }
    };

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *value = sqlite3_column_text(stmt, 0);
            std::string text = value ? reinterpret_cast<const char *>(value)
                                     : "";
            if (text != "ok") {
                add_issue("Критично", "PRAGMA integrity_check", text);
            }
        }
        sqlite3_finalize(stmt);
    } else {
        add_issue("Ошибка", "PRAGMA integrity_check", sqlite3_errmsg(db));
    }

    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *table = sqlite3_column_text(stmt, 0);
            int rowid = sqlite3_column_int(stmt, 1);
            const unsigned char *parent = sqlite3_column_text(stmt, 2);
            int fkid = sqlite3_column_int(stmt, 3);
            std::ostringstream details;
            details << "table="
                    << (table ? reinterpret_cast<const char *>(table) : "")
                    << ", rowid=" << rowid << ", parent="
                    << (parent ? reinterpret_cast<const char *>(parent) : "")
                    << ", fk=" << fkid;
            add_issue("Критично", "PRAGMA foreign_key_check", details.str());
        }
        sqlite3_finalize(stmt);
    } else {
        add_issue("Ошибка", "PRAGMA foreign_key_check", sqlite3_errmsg(db));
    }

    add_count_issue(
        "Критично", "Расшифровки без платежа",
        "SELECT COUNT(*) FROM PaymentDetails pd "
        "LEFT JOIN Payments p ON p.id = pd.payment_id "
        "WHERE p.id IS NULL;",
        "Количество строк",
        "SELECT 'PaymentDetail id=' || pd.id || ', payment_id=' || pd.payment_id "
        "FROM PaymentDetails pd "
        "LEFT JOIN Payments p ON p.id = pd.payment_id "
        "WHERE p.id IS NULL "
        "LIMIT 5;");

    add_count_issue(
        "Предупреждение", "Платежи без расшифровок",
        "SELECT COUNT(*) FROM Payments p "
        "LEFT JOIN PaymentDetails pd ON pd.payment_id = p.id "
        "WHERE pd.id IS NULL;",
        "Количество платежей",
        "SELECT 'Payment id=' || p.id || ', дата=' || p.date || "
        "', номер=' || IFNULL(p.doc_number, '') || ', сумма=' || p.amount "
        "FROM Payments p "
        "LEFT JOIN PaymentDetails pd ON pd.payment_id = p.id "
        "WHERE pd.id IS NULL "
        "LIMIT 5;");

    add_count_issue(
        "Предупреждение", "Документы основания без деталей",
        "SELECT COUNT(*) FROM BasePaymentDocuments bpd "
        "LEFT JOIN BasePaymentDocumentDetails d ON d.document_id = bpd.id "
        "WHERE d.id IS NULL;",
        "Количество документов",
        "SELECT 'Документ id=' || bpd.id || ', дата=' || bpd.date || "
        "', номер=' || bpd.number || ', контрагент=' || IFNULL(bpd.counterparty_name, '') "
        "FROM BasePaymentDocuments bpd "
        "LEFT JOIN BasePaymentDocumentDetails d ON d.document_id = bpd.id "
        "WHERE d.id IS NULL "
        "LIMIT 5;");

    add_count_issue(
        "Предупреждение", "Расшифровки платежей без КОСГУ",
        "SELECT COUNT(*) FROM PaymentDetails WHERE kosgu_id IS NULL;",
        "Количество строк",
        "SELECT 'PaymentDetail id=' || id || ', payment_id=' || payment_id || "
        "', сумма=' || amount "
        "FROM PaymentDetails WHERE kosgu_id IS NULL LIMIT 5;");

    add_count_issue(
        "Предупреждение", "Расшифровки документов без КОСГУ",
        "SELECT COUNT(*) FROM BasePaymentDocumentDetails "
        "WHERE kosgu_id IS NULL;",
        "Количество строк",
        "SELECT 'Деталь ДО id=' || id || ', document_id=' || document_id || "
        "', сумма=' || amount || ', операция=' || IFNULL(operation_content, '') "
        "FROM BasePaymentDocumentDetails WHERE kosgu_id IS NULL LIMIT 5;");

    add_count_issue(
        "Предупреждение", "Платежи с расхождением суммы",
        "SELECT COUNT(*) FROM ("
        "SELECT p.id "
        "FROM Payments p JOIN PaymentDetails pd ON pd.payment_id = p.id "
        "GROUP BY p.id, p.amount "
        "HAVING ABS(ABS(p.amount) - ABS(SUM(pd.amount))) > 0.01"
        ");",
        "Количество платежей",
        "SELECT 'Payment id=' || p.id || ', дата=' || p.date || "
        "', номер=' || IFNULL(p.doc_number, '') || ', платеж=' || p.amount || "
        "', расшифровки=' || SUM(pd.amount) "
        "FROM Payments p JOIN PaymentDetails pd ON pd.payment_id = p.id "
        "GROUP BY p.id, p.date, p.doc_number, p.amount "
        "HAVING ABS(ABS(p.amount) - ABS(SUM(pd.amount))) > 0.01 "
        "LIMIT 5;");

    add_count_issue(
        "Предупреждение", "Дубли платежей",
        "SELECT COUNT(*) FROM ("
        "SELECT date, IFNULL(doc_number, ''), amount, "
        "IFNULL(counterparty_id, -1), IFNULL(description, ''), COUNT(*) c "
        "FROM Payments "
        "GROUP BY date, IFNULL(doc_number, ''), amount, "
        "IFNULL(counterparty_id, -1), IFNULL(description, '') "
        "HAVING c > 1"
        ");",
        "Количество групп дублей",
        "SELECT 'Дата=' || date || ', номер=' || IFNULL(doc_number, '') || "
        "', сумма=' || amount || ', дублей=' || COUNT(*) "
        "FROM Payments "
        "GROUP BY date, IFNULL(doc_number, ''), amount, "
        "IFNULL(counterparty_id, -1), IFNULL(description, '') "
        "HAVING COUNT(*) > 1 "
        "LIMIT 5;");

    add_count_issue(
        "Предупреждение", "Дубли документов основания",
        "SELECT COUNT(*) FROM ("
        "SELECT number, date, IFNULL(counterparty_name, ''), COUNT(*) c "
        "FROM BasePaymentDocuments "
        "GROUP BY number, date, IFNULL(counterparty_name, '') "
        "HAVING c > 1"
        ");",
        "Количество групп дублей",
        "SELECT 'Дата=' || date || ', номер=' || number || "
        "', контрагент=' || IFNULL(counterparty_name, '') || "
        "', дублей=' || COUNT(*) "
        "FROM BasePaymentDocuments "
        "GROUP BY number, date, IFNULL(counterparty_name, '') "
        "HAVING COUNT(*) > 1 "
        "LIMIT 5;");

    if (issues.empty()) {
        add_issue("ОК", "Целостность базы", "Проблем не найдено");
    }

    return issues;
}

// Callback for generic SELECT queries
static int callback_collect_data(void *data, int argc, char **argv,
                                 char **azColName) {
    auto *result_pair =
        static_cast<std::pair<std::vector<std::string> *,
                              std::vector<std::vector<std::string>> *> *>(data);
    std::vector<std::string> *columns = result_pair->first;
    std::vector<std::vector<std::string>> *rows = result_pair->second;

    // Populate columns if not already done (first row)
    if (columns->empty()) {
        for (int i = 0; i < argc; i++) {
            columns->push_back(azColName[i]);
        }
    }

    // Populate row data
    std::vector<std::string> row_data;
    for (int i = 0; i < argc; i++) {
        row_data.push_back(argv[i] ? argv[i] : "NULL");
    }
    rows->push_back(row_data);

    return 0;
}

bool DatabaseManager::executeSelect(
    const std::string &sql, std::vector<std::string> &columns,
    std::vector<std::vector<std::string>> &rows) {
    if (!db)
        return false;

    // Clear previous results
    columns.clear();
    rows.clear();

    // Use a pair to pass both vectors to the callback
    std::pair<std::vector<std::string> *,
              std::vector<std::vector<std::string>> *>
        result_pair(&columns, &rows);

    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), callback_collect_data, &result_pair,
                          &errmsg);

    if (rc != SQLITE_OK) {
        std::cerr << "SQL SELECT error: " << errmsg << std::endl;
        sqlite3_free(errmsg);
        return false;
    }

    return true;
}

// Settings
Settings DatabaseManager::getSettings() {
    Settings settings = {
        1,
        "",
        "",
        "",
        "",
        20,
        0,
        24,
        "https://zakupki.gov.ru/epz/contract/contractCard/"
        "common-info.html?reestrNumber={IKZ}",
        "https://zakupki.gov.ru/epz/contract/search/"
        "results.html?searchString={NUMBER}"}; // Default settings
    if (!db)
        return settings;

    std::string sql =
        "SELECT organization_name, period_start_date, "
        "period_end_date, note, import_preview_lines, theme, font_size, "
        "zakupki_url_template, zakupki_url_search_template FROM "
        "Settings WHERE id = 1;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getSettings: "
                  << sqlite3_errmsg(db) << std::endl;
        return settings;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *org_name = sqlite3_column_text(stmt, 0);
        const unsigned char *start_date = sqlite3_column_text(stmt, 1);
        const unsigned char *end_date = sqlite3_column_text(stmt, 2);
        const unsigned char *note = sqlite3_column_text(stmt, 3);
        settings.import_preview_lines = sqlite3_column_int(stmt, 4);

        settings.theme = sqlite3_column_int(stmt, 5);

        settings.font_size = sqlite3_column_int(stmt, 6);

        settings.organization_name = org_name ? (const char *)org_name : "";
        settings.period_start_date = start_date ? (const char *)start_date : "";
        settings.period_end_date = end_date ? (const char *)end_date : "";
        settings.note = note ? (const char *)note : "";
        const unsigned char *zakupki_url = sqlite3_column_text(stmt, 7);
        settings.zakupki_url_template = zakupki_url
                                            ? (const char *)zakupki_url
                                            : settings.zakupki_url_template;
        const unsigned char *zakupki_search_url = sqlite3_column_text(stmt, 8);
        settings.zakupki_url_search_template = zakupki_search_url
                                            ? (const char *)zakupki_search_url
                                            : settings.zakupki_url_search_template;
    }

    sqlite3_finalize(stmt);
    return settings;
}

bool DatabaseManager::updateSettings(const Settings &settings) {
    if (!db)
        return false;

    std::string sql =
        "UPDATE Settings SET organization_name = ?, period_start_date = ?, "
        "period_end_date = ?, note = ?, import_preview_lines = ?, theme = ?, "
        "font_size = ?, zakupki_url_template = ?, zakupki_url_search_template = ? "
        "WHERE id = 1;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for updateSettings: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, settings.organization_name.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, settings.period_start_date.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, settings.period_end_date.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, settings.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, settings.import_preview_lines);
    sqlite3_bind_int(stmt, 6, settings.theme);

    sqlite3_bind_int(stmt, 7, settings.font_size);
    sqlite3_bind_text(stmt, 8, settings.zakupki_url_template.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, settings.zakupki_url_search_template.c_str(), -1,
                      SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to update Settings: " << sqlite3_errmsg(db)
                  << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::backupTo(const std::string &backupFilepath) {
    if (!db) {
        return false;
    }

    sqlite3 *pBackupDb = nullptr;
    int rc = sqlite3_open(backupFilepath.c_str(), &pBackupDb);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open backup database: "
                  << sqlite3_errmsg(pBackupDb) << std::endl;
        sqlite3_close(pBackupDb);
        return false;
    }

    sqlite3_backup *pBackup =
        sqlite3_backup_init(pBackupDb, "main", db, "main");
    if (!pBackup) {
        std::cerr << "Failed to init backup: " << sqlite3_errmsg(pBackupDb)
                  << std::endl;
        sqlite3_close(pBackupDb);
        return false;
    }

    rc = sqlite3_backup_step(pBackup, -1); // -1 to copy all pages

    if (rc != SQLITE_DONE) {
        std::cerr << "Backup step failed: " << sqlite3_errmsg(pBackupDb)
                  << std::endl;
    }

    sqlite3_backup_finish(pBackup);

    // Check for errors during backup finish
    rc = sqlite3_errcode(pBackupDb);
    if (rc != SQLITE_OK) {
        std::cerr << "Backup finish failed: " << sqlite3_errmsg(pBackupDb)
                  << std::endl;
    }

    sqlite3_close(pBackupDb);

    return rc == SQLITE_OK;
}

// ==================== BasePaymentDocument Methods ====================

int DatabaseManager::addBasePaymentDocument(BasePaymentDocument& doc) {
    if (!db) return -1;
    std::string sql =
        "INSERT INTO BasePaymentDocuments (date, number, document_name, "
        "counterparty_name, contract_id, payment_id, note, is_for_checking, is_checked) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for addBasePaymentDocument: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, doc.date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, doc.number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, doc.document_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, doc.counterparty_name.c_str(), -1, SQLITE_STATIC);
    bind_optional_id(stmt, 5, doc.contract_id);
    bind_optional_id(stmt, 6, doc.payment_id);
    sqlite3_bind_text(stmt, 7, doc.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, doc.is_for_checking ? 1 : 0);
    sqlite3_bind_int(stmt, 9, doc.is_checked ? 1 : 0);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to add BasePaymentDocument: "
                  << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return -1;
    }
    doc.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return doc.id;
}

int DatabaseManager::getBasePaymentDocumentIdByNumberDate(const std::string& number, const std::string& date) {
    if (!db) return -1;
    std::string sql = "SELECT id FROM BasePaymentDocuments WHERE number = ? AND date = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getBasePaymentDocumentIdByNumberDate: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int DatabaseManager::getBasePaymentDocumentIdBySignature(
    const std::string& number,
    const std::string& date,
    const std::string& document_name,
    const std::string& counterparty_name) {
    if (!db) return -1;

    std::string sql =
        "SELECT id FROM BasePaymentDocuments "
        "WHERE number = ? AND date = ? "
        "AND IFNULL(document_name, '') = IFNULL(?, '') "
        "AND IFNULL(counterparty_name, '') = IFNULL(?, '') "
        "LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getBasePaymentDocumentIdBySignature: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, document_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, counterparty_name.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

std::vector<BasePaymentDocument> DatabaseManager::getBasePaymentDocuments() {
    std::vector<BasePaymentDocument> docs;
    if (!db) return docs;

    std::string sql =
        "SELECT bpd.id, bpd.date, bpd.number, bpd.document_name, "
        "bpd.counterparty_name, bpd.contract_id, bpd.payment_id, bpd.note, "
        "bpd.is_for_checking, bpd.is_checked, "
        "IFNULL(SUM(bpdd.amount), 0.0) as total_amount "
        "FROM BasePaymentDocuments bpd "
        "LEFT JOIN BasePaymentDocumentDetails bpdd ON bpd.id = bpdd.document_id "
        "GROUP BY bpd.id, bpd.date, bpd.number, bpd.document_name, "
        "bpd.counterparty_name, bpd.contract_id, bpd.payment_id, bpd.note, "
        "bpd.is_for_checking, bpd.is_checked;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getBasePaymentDocuments: "
                  << sqlite3_errmsg(db) << std::endl;
        return docs;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BasePaymentDocument doc;
        doc.id = sqlite3_column_int(stmt, 0);
        const unsigned char* date = sqlite3_column_text(stmt, 1);
        doc.date = date ? (const char*)date : "";
        const unsigned char* number = sqlite3_column_text(stmt, 2);
        doc.number = number ? (const char*)number : "";
        const unsigned char* doc_name = sqlite3_column_text(stmt, 3);
        doc.document_name = doc_name ? (const char*)doc_name : "";
        const unsigned char* cp_name = sqlite3_column_text(stmt, 4);
        doc.counterparty_name = cp_name ? (const char*)cp_name : "";
        doc.contract_id = sqlite3_column_type(stmt, 5) == SQLITE_NULL
                              ? -1
                              : sqlite3_column_int(stmt, 5);
        doc.payment_id = sqlite3_column_type(stmt, 6) == SQLITE_NULL
                             ? -1
                             : sqlite3_column_int(stmt, 6);
        const unsigned char* note = sqlite3_column_text(stmt, 7);
        doc.note = note ? (const char*)note : "";
        doc.is_for_checking = sqlite3_column_int(stmt, 8) != 0;
        doc.is_checked = sqlite3_column_int(stmt, 9) != 0;
        doc.total_amount = sqlite3_column_double(stmt, 10);
        docs.push_back(doc);
    }
    sqlite3_finalize(stmt);
    return docs;
}

bool DatabaseManager::updateBasePaymentDocument(const BasePaymentDocument& doc) {
    if (!db) return false;
    std::string sql =
        "UPDATE BasePaymentDocuments SET date = ?, number = ?, document_name = ?, "
        "counterparty_name = ?, contract_id = ?, payment_id = ?, note = ?, "
        "is_for_checking = ?, is_checked = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for updateBasePaymentDocument: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, doc.date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, doc.number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, doc.document_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, doc.counterparty_name.c_str(), -1, SQLITE_STATIC);
    bind_optional_id(stmt, 5, doc.contract_id);
    bind_optional_id(stmt, 6, doc.payment_id);
    sqlite3_bind_text(stmt, 7, doc.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, doc.is_for_checking ? 1 : 0);
    sqlite3_bind_int(stmt, 9, doc.is_checked ? 1 : 0);
    sqlite3_bind_int(stmt, 10, doc.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to update BasePaymentDocument: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteBasePaymentDocument(int id) {
    if (!db) return false;
    std::string sql = "DELETE FROM BasePaymentDocuments WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for deleteBasePaymentDocument: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete BasePaymentDocument: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::clearBasePaymentDocuments() {
    return execute("DELETE FROM BasePaymentDocuments;");
}

std::vector<ContractPaymentInfo> DatabaseManager::getPaymentInfoForBasePaymentDocument(int doc_id) {
    std::vector<ContractPaymentInfo> result;
    if (!db) return result;

    std::string sql =
        "SELECT p.id, p.date, p.doc_number, p.amount, p.description, "
        "c.name "
        "FROM Payments p "
        "JOIN ("
        "    SELECT payment_id FROM PaymentDetails WHERE base_document_id = ? "
        "    UNION "
        "    SELECT payment_id FROM PaymentBaseDocumentLinks "
        "    WHERE base_document_id = ? AND IFNULL(match_status, '') <> 'rejected'"
        ") linked ON linked.payment_id = p.id "
        "LEFT JOIN Counterparties c ON p.counterparty_id = c.id "
        "ORDER BY p.date DESC, p.id DESC;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getPaymentInfoForBasePaymentDocument: "
                  << sqlite3_errmsg(db) << std::endl;
        return result;
    }
    sqlite3_bind_int(stmt, 1, doc_id);
    sqlite3_bind_int(stmt, 2, doc_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ContractPaymentInfo info;
        info.contract_id = sqlite3_column_int(stmt, 0);
        const unsigned char* date = sqlite3_column_text(stmt, 1);
        info.date = date ? (const char*)date : "";
        const unsigned char* doc_num = sqlite3_column_text(stmt, 2);
        info.doc_number = doc_num ? (const char*)doc_num : "";
        info.amount = sqlite3_column_double(stmt, 3);
        const unsigned char* desc = sqlite3_column_text(stmt, 4);
        info.description = desc ? (const char*)desc : "";
        const unsigned char* name = sqlite3_column_text(stmt, 5);
        info.counterparty_name = name ? (const char*)name : "";
        result.push_back(info);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool DatabaseManager::addPaymentBaseDocumentLink(PaymentBaseDocumentLink& link) {
    if (!db || link.payment_id <= 0 || link.base_document_id <= 0) {
        return false;
    }

    if (link.match_status.empty()) {
        link.match_status = "suggested";
    }

    std::string sql =
        "INSERT INTO PaymentBaseDocumentLinks "
        "(payment_id, base_document_id, linked_amount, match_score, "
        "match_status, match_reason, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(payment_id, base_document_id) DO UPDATE SET "
        "linked_amount = excluded.linked_amount, "
        "match_score = excluded.match_score, "
        "match_status = excluded.match_status, "
        "match_reason = excluded.match_reason, "
        "note = excluded.note, "
        "updated_at = CURRENT_TIMESTAMP;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for addPaymentBaseDocumentLink: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    sqlite3_bind_int(stmt, 1, link.payment_id);
    sqlite3_bind_int(stmt, 2, link.base_document_id);
    sqlite3_bind_double(stmt, 3, link.linked_amount);
    sqlite3_bind_int(stmt, 4, link.match_score);
    sqlite3_bind_text(stmt, 5, link.match_status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, link.match_reason.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, link.note.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to add PaymentBaseDocumentLink: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    sqlite3_stmt* id_stmt = nullptr;
    std::string id_sql =
        "SELECT id FROM PaymentBaseDocumentLinks "
        "WHERE payment_id = ? AND base_document_id = ?;";
    if (sqlite3_prepare_v2(db, id_sql.c_str(), -1, &id_stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int(id_stmt, 1, link.payment_id);
        sqlite3_bind_int(id_stmt, 2, link.base_document_id);
        if (sqlite3_step(id_stmt) == SQLITE_ROW) {
            link.id = sqlite3_column_int(id_stmt, 0);
        }
    }
    sqlite3_finalize(id_stmt);
    return true;
}

static PaymentBaseDocumentLink readPaymentBaseDocumentLink(sqlite3_stmt* stmt) {
    PaymentBaseDocumentLink link;
    link.id = sqlite3_column_int(stmt, 0);
    link.payment_id = sqlite3_column_int(stmt, 1);
    link.base_document_id = sqlite3_column_int(stmt, 2);
    link.linked_amount = sqlite3_column_double(stmt, 3);
    link.match_score = sqlite3_column_int(stmt, 4);
    const unsigned char* text = sqlite3_column_text(stmt, 5);
    link.match_status = text ? reinterpret_cast<const char*>(text) : "";
    text = sqlite3_column_text(stmt, 6);
    link.match_reason = text ? reinterpret_cast<const char*>(text) : "";
    text = sqlite3_column_text(stmt, 7);
    link.note = text ? reinterpret_cast<const char*>(text) : "";
    return link;
}

static std::vector<PaymentBaseDocumentLink> getPaymentBaseDocumentLinks(
    sqlite3* db,
    const std::string& sql,
    int id) {
    std::vector<PaymentBaseDocumentLink> links;
    if (!db) {
        return links;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getPaymentBaseDocumentLinks: "
                  << sqlite3_errmsg(db) << std::endl;
        return links;
    }

    sqlite3_bind_int(stmt, 1, id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        links.push_back(readPaymentBaseDocumentLink(stmt));
    }
    sqlite3_finalize(stmt);
    return links;
}

std::vector<PaymentBaseDocumentLink>
DatabaseManager::getPaymentBaseDocumentLinksForPayment(int payment_id) {
    return getPaymentBaseDocumentLinks(
        db,
        "SELECT id, payment_id, base_document_id, linked_amount, match_score, "
        "match_status, match_reason, note "
        "FROM PaymentBaseDocumentLinks "
        "WHERE payment_id = ? "
        "ORDER BY match_status = 'confirmed' DESC, match_score DESC, id DESC;",
        payment_id);
}

std::vector<PaymentBaseDocumentLink>
DatabaseManager::getPaymentBaseDocumentLinksForDocument(int base_document_id) {
    return getPaymentBaseDocumentLinks(
        db,
        "SELECT id, payment_id, base_document_id, linked_amount, match_score, "
        "match_status, match_reason, note "
        "FROM PaymentBaseDocumentLinks "
        "WHERE base_document_id = ? "
        "ORDER BY match_status = 'confirmed' DESC, match_score DESC, id DESC;",
        base_document_id);
}

bool DatabaseManager::deletePaymentBaseDocumentLink(int link_id) {
    if (!db) return false;

    std::string sql = "DELETE FROM PaymentBaseDocumentLinks WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for deletePaymentBaseDocumentLink: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    sqlite3_bind_int(stmt, 1, link_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete PaymentBaseDocumentLink: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

// ==================== BasePaymentDocumentDetail Methods ====================

bool DatabaseManager::addBasePaymentDocumentDetail(BasePaymentDocumentDetail& detail) {
    if (!db) return false;
    std::string sql =
        "INSERT INTO BasePaymentDocumentDetails (document_id, operation_content, "
        "debit_account, credit_account, kosgu_id, amount, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for addBasePaymentDocumentDetail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, detail.document_id);
    sqlite3_bind_text(stmt, 2, detail.operation_content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, detail.debit_account.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, detail.credit_account.c_str(), -1, SQLITE_STATIC);
    bind_optional_id(stmt, 5, detail.kosgu_id);
    sqlite3_bind_double(stmt, 6, detail.amount);
    sqlite3_bind_text(stmt, 7, detail.note.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to add BasePaymentDocumentDetail: "
                  << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    detail.id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return true;
}

static int base_payment_document_detail_select_callback(void* data, int argc, char** argv, char** azColName) {
    auto* details = static_cast<std::vector<BasePaymentDocumentDetail>*>(data);
    BasePaymentDocumentDetail d;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        if (colName == "id") d.id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "document_id") d.document_id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "operation_content") d.operation_content = argv[i] ? argv[i] : "";
        else if (colName == "debit_account") d.debit_account = argv[i] ? argv[i] : "";
        else if (colName == "credit_account") d.credit_account = argv[i] ? argv[i] : "";
        else if (colName == "kosgu_id") d.kosgu_id = argv[i] ? std::stoi(argv[i]) : -1;
        else if (colName == "amount") d.amount = argv[i] ? std::stod(argv[i]) : 0.0;
        else if (colName == "note") d.note = argv[i] ? argv[i] : "";
    }
    details->push_back(d);
    return 0;
}

std::vector<BasePaymentDocumentDetail> DatabaseManager::getBasePaymentDocumentDetails(int document_id) {
    std::vector<BasePaymentDocumentDetail> details;
    if (!db) return details;
    
    std::string sql = "SELECT id, document_id, operation_content, debit_account, "
                      "credit_account, kosgu_id, amount, note "
                      "FROM BasePaymentDocumentDetails WHERE document_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getBasePaymentDocumentDetails: "
                  << sqlite3_errmsg(db) << std::endl;
        return details;
    }
    sqlite3_bind_int(stmt, 1, document_id);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BasePaymentDocumentDetail d;
        d.id = sqlite3_column_int(stmt, 0);
        d.document_id = sqlite3_column_int(stmt, 1);
        const unsigned char* v = sqlite3_column_text(stmt, 2);
        d.operation_content = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 3);
        d.debit_account = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 4);
        d.credit_account = v ? (const char*)v : "";
        d.kosgu_id = sqlite3_column_type(stmt, 5) == SQLITE_NULL
                         ? -1
                         : sqlite3_column_int(stmt, 5);
        d.amount = sqlite3_column_double(stmt, 6);
        v = sqlite3_column_text(stmt, 7);
        d.note = v ? (const char*)v : "";
        details.push_back(d);
    }
    sqlite3_finalize(stmt);
    return details;
}

std::vector<BasePaymentDocumentDetail> DatabaseManager::getAllBasePaymentDocumentDetails() {
    std::vector<BasePaymentDocumentDetail> details;
    if (!db) return details;
    std::string sql = "SELECT id, document_id, operation_content, debit_account, "
                      "credit_account, kosgu_id, amount, note "
                      "FROM BasePaymentDocumentDetails;";
    sqlite3_exec(db, sql.c_str(), base_payment_document_detail_select_callback, &details, nullptr);
    return details;
}

bool DatabaseManager::updateBasePaymentDocumentDetail(const BasePaymentDocumentDetail& detail) {
    if (!db) return false;
    std::string sql =
        "UPDATE BasePaymentDocumentDetails SET document_id = ?, operation_content = ?, "
        "debit_account = ?, credit_account = ?, kosgu_id = ?, amount = ?, note = ? "
        "WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for updateBasePaymentDocumentDetail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, detail.document_id);
    sqlite3_bind_text(stmt, 2, detail.operation_content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, detail.debit_account.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, detail.credit_account.c_str(), -1, SQLITE_STATIC);
    bind_optional_id(stmt, 5, detail.kosgu_id);
    sqlite3_bind_double(stmt, 6, detail.amount);
    sqlite3_bind_text(stmt, 7, detail.note.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, detail.id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to update BasePaymentDocumentDetail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteBasePaymentDocumentDetail(int id) {
    if (!db) return false;
    std::string sql = "DELETE FROM BasePaymentDocumentDetails WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for deleteBasePaymentDocumentDetail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete BasePaymentDocumentDetail: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteAllBasePaymentDocumentDetails(int document_id) {
    if (!db) return false;
    std::string sql = "DELETE FROM BasePaymentDocumentDetails WHERE document_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for deleteAllBasePaymentDocumentDetails: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, document_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete all BasePaymentDocumentDetails: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DatabaseManager::bulkUpdateBasePaymentDocumentDetails(const std::vector<int>& document_ids, const std::string& field_to_update, int new_id) {
    if (!db || document_ids.empty()) return false;

    std::string placeholders;
    for (size_t i = 0; i < document_ids.size(); ++i) {
        if (i > 0) placeholders += ",";
        placeholders += "?";
    }

    std::string sql = "UPDATE BasePaymentDocumentDetails SET " + field_to_update +
                      " = ? WHERE document_id IN (" + placeholders + ");";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for bulkUpdateBasePaymentDocumentDetails: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, new_id);
    for (size_t i = 0; i < document_ids.size(); ++i) {
        sqlite3_bind_int(stmt, 2 + static_cast<int>(i), document_ids[i]);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to bulk update BasePaymentDocumentDetails: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

int DatabaseManager::getBasePaymentDocumentDetailIdByContent(int document_id, const std::string& operation_content) {
    if (!db) return -1;
    std::string sql = "SELECT id FROM BasePaymentDocumentDetails WHERE document_id = ? AND operation_content = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getBasePaymentDocumentDetailIdByContent: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }
    sqlite3_bind_int(stmt, 1, document_id);
    sqlite3_bind_text(stmt, 2, operation_content.c_str(), -1, SQLITE_STATIC);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int DatabaseManager::getBasePaymentDocumentDetailIdBySignature(const BasePaymentDocumentDetail& detail) {
    if (!db) return -1;

    std::string sql =
        "SELECT id FROM BasePaymentDocumentDetails "
        "WHERE document_id = ? "
        "AND IFNULL(operation_content, '') = IFNULL(?, '') "
        "AND IFNULL(debit_account, '') = IFNULL(?, '') "
        "AND IFNULL(credit_account, '') = IFNULL(?, '') "
        "AND IFNULL(kosgu_id, -1) = ? "
        "AND ABS(amount - ?) <= 0.01 "
        "LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getBasePaymentDocumentDetailIdBySignature: "
                  << sqlite3_errmsg(db) << std::endl;
        return -1;
    }

    sqlite3_bind_int(stmt, 1, detail.document_id);
    sqlite3_bind_text(stmt, 2, detail.operation_content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, detail.debit_account.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, detail.credit_account.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, detail.kosgu_id > 0 ? detail.kosgu_id : -1);
    sqlite3_bind_double(stmt, 6, detail.amount);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

// ==================== Автоподбор платежа из банка ====================

// Вспомогательная функция для улучшенного сравнения наименований контрагентов
// Учитывает сокращения, кавычки, организационно-правовые формы
static std::string normalizeCounterpartyName(const std::string& name) {
    std::string result = name;
    
    // Приводим к нижнему регистру
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    
    // Удаляем кавычки и спецсимволы
    std::string special_chars = "\"'`«»〈〉<>";
    result.erase(std::remove_if(result.begin(), result.end(),
        [&special_chars](char c) {
            return special_chars.find(c) != std::string::npos;
        }), result.end());
    
    // Удаляем полные формы ОПФ (могут быть в начале/конце/середине строки)
    std::vector<std::string> full_legal_forms = {
        "общество с ограниченной ответственностью",
        "общество с дополнительной ответственностью",
        "акционерное общество",
        "публичное акционерное общество",
        "закрытое акционерное общество",
        "открытое акционерное общество",
        "индивидуальный предприниматель",
        "некоммерческая организация",
        "крестьянское фермерское хозяйство",
        "полное товарищество",
        "товарищество на вере",
        "производственный кооператив",
        "унитарное предприятие",
        "государственное унитарное предприятие",
        "муниципальное унитарное предприятие",
        "автономная некоммерческая организация",
        "бюджетное учреждение",
        "казённое учреждение",
        "некоммерческий фонд",
    };

    // Удаляем сокращённые ОПФ в середине строки (в окружении пробелов)
    std::vector<std::string> legal_forms = {
        " ооо ", " ао ", " зао ", " оао ", " пао ", " нко ", " фоп ",
        " ип ", " кфх ", " пбоюл ", " сп ", " филиал ",
    };
    for (const auto& form : legal_forms) {
        size_t pos;
        while ((pos = result.find(form)) != std::string::npos) {
            result.replace(pos, form.length(), " ");
        }
    }

    // Удаляем полные ОПФ в начале, середине и конце строки
    for (const auto& form : full_legal_forms) {
        std::string form_with_space = " " + form;
        // В начале строки
        if (result.find(form) == 0) {
            result.erase(0, form.length());
            if (!result.empty() && result[0] == ' ') {
                result.erase(0, 1);
            }
        }
        // В середине строки (с пробелом перед)
        size_t pos;
        while ((pos = result.find(form_with_space)) != std::string::npos) {
            result.replace(pos, form_with_space.length(), " ");
        }
        // В конце строки
        if (result.length() >= form.length() &&
            result.substr(result.length() - form.length()) == form) {
            result.erase(result.length() - form.length());
            if (!result.empty() && result.back() == ' ') {
                result.pop_back();
            }
        }
    }

    // Удаляем сокращённые ОПФ в начале, середине и конце строки
    std::vector<std::string> short_forms = {
        "ооо", "ао", "зао", "оао", "пао", "нко", "фоп",
        "ип", "кфх", "пбоюл", "сп",
    };
    for (const auto& form : short_forms) {
        std::string form_with_space = " " + form;
        // В начале строки
        if (result.find(form) == 0 && (result.length() == form.length() || result[form.length()] == ' ')) {
            result.erase(0, form.length());
            if (!result.empty() && result[0] == ' ') {
                result.erase(0, 1);
            }
        }
        // В середине строки (с пробелами)
        size_t pos;
        while ((pos = result.find(form_with_space)) != std::string::npos) {
            result.replace(pos, form_with_space.length(), " ");
        }
        // В конце строки
        if (result.length() >= form.length() &&
            result.substr(result.length() - form.length()) == form) {
            result.erase(result.length() - form.length());
            if (!result.empty() && result.back() == ' ') {
                result.pop_back();
            }
        }
    }
    
    // Удаляем точки, запятые, тире
    std::string punct = ".,;:-–—/\\|";
    result.erase(std::remove_if(result.begin(), result.end(),
        [& punct](char c) {
            return punct.find(c) != std::string::npos;
        }), result.end());
    
    // Сжимаем множественные пробелы в один
    std::string trimmed;
    bool last_was_space = false;
    for (char c : result) {
        if (c == ' ') {
            if (!last_was_space) {
                trimmed += ' ';
                last_was_space = true;
            }
        } else {
            trimmed += c;
            last_was_space = false;
        }
    }
    
    // Убираем пробелы по краям
    size_t start = trimmed.find_first_not_of(' ');
    size_t end = trimmed.find_last_not_of(' ');
    if (start == std::string::npos) return "";
    return trimmed.substr(start, end - start + 1);
}

// Улучшенная проверка частичного совпадения контрагентов
// Возвращает true, если имена совпадают после нормализации или одно содержится в другом
static bool counterpartyNamesMatch(const std::string& name1, const std::string& name2) {
    std::string n1 = normalizeCounterpartyName(name1);
    std::string n2 = normalizeCounterpartyName(name2);
    
    if (n1.empty() || n2.empty()) return false;
    
    // Полное совпадение после нормализации
    if (n1 == n2) return true;
    
    // Частичное: одно имя содержится в другом
    if (n1.find(n2) != std::string::npos || n2.find(n1) != std::string::npos) return true;
    
    // Проверяем по словам (если >= 70% слов одного имени есть в другом)
    std::vector<std::string> words1, words2;
    std::istringstream iss1(n1), iss2(n2);
    std::string word;
    while (iss1 >> word) words1.push_back(word);
    while (iss2 >> word) words2.push_back(word);
    
    if (words1.empty() || words2.empty()) return false;
    
    int matches = 0;
    for (const auto& w1 : words1) {
        for (const auto& w2 : words2) {
            if (w1.find(w2) != std::string::npos || w2.find(w1) != std::string::npos) {
                matches++;
                break;
            }
        }
    }
    
    double threshold = std::max(1, (int)(words1.size() * 0.7));
    return matches >= threshold;
}

static std::string normalizeDocumentNumber(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            result.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return result;
}

static bool paymentTextContainsDocumentNumber(const std::string& doc_number,
                                              const std::string& text) {
    std::string normalized_doc = normalizeDocumentNumber(doc_number);
    if (normalized_doc.empty()) {
        return false;
    }

    std::string normalized_text = normalizeDocumentNumber(text);
    if (normalized_text.find(normalized_doc) != std::string::npos) {
        return true;
    }

    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    try {
        std::regex doc_context(
            "(упд|акт|счет|сч|накладн|документ\\s+о\\s+приемк|приемк)"
            "[^0-9a-zа-я]{0,20}"
            "([0-9a-zа-я/_.-]{1,40})",
            std::regex_constants::icase);

        auto begin = std::sregex_iterator(lower_text.begin(), lower_text.end(),
                                          doc_context);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            if ((*it).size() < 3) {
                continue;
            }
            std::string candidate = normalizeDocumentNumber((*it)[2].str());
            if (!candidate.empty() &&
                (candidate == normalized_doc ||
                 candidate.find(normalized_doc) != std::string::npos ||
                 normalized_doc.find(candidate) != std::string::npos)) {
                return true;
            }
        }
    } catch (const std::regex_error&) {
        return false;
    }

    return false;
}

std::vector<DatabaseManager::PaymentMatch> DatabaseManager::findMatchingPayments(const BasePaymentDocument& doc, bool require_counterparty) {
    std::vector<PaymentMatch> matches;
    if (!db) return matches;

    // Получаем все платежи из банка
    std::string sql = 
        "SELECT p.id, p.date, p.doc_number, p.amount, "
        "       c.name as counterparty_name, p.description "
        "FROM Payments p "
        "LEFT JOIN Counterparties c ON p.counterparty_id = c.id "
        "WHERE p.id IS NOT NULL "
        "  AND IFNULL(c.is_contract_optional, 0) = 0 "
        "ORDER BY p.date DESC";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for findMatchingPayments: "
                  << sqlite3_errmsg(db) << std::endl;
        return matches;
    }

    // Используем имя контрагента напрямую из документа
    std::string doc_counterparty_name = doc.counterparty_name;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PaymentMatch match;
        match.payment_id = sqlite3_column_int(stmt, 0);
        match.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        match.doc_number = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        match.amount = sqlite3_column_double(stmt, 3);
        
        const char* cp_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        match.counterparty_name = cp_text ? cp_text : "";
        
        const char* desc_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        match.description = desc_text ? desc_text : "";

        match.match_score = 0;
        match.match_reasons.clear();

        // Критерии совпадения:
        
        // 1. Совпадение по сумме
        if (std::abs(match.amount - doc.total_amount) < 0.01) {
            match.match_score += 30;
            match.match_reasons += "Сумма; ";
        }

        // 2. Совпадение по дате
        if (!doc.date.empty() && match.date == doc.date) {
            match.match_score += 20;
            match.match_reasons += "Дата; ";
        }

        // 3. Номер первичного документа в назначении платежа.
        if (!doc.number.empty() && !match.description.empty()) {
            if (paymentTextContainsDocumentNumber(doc.number, match.description)) {
                match.match_score += 45;
                match.match_reasons += "Номер первичного документа в назначении; ";
            }
        }

        // 4. Совпадение по контрагенту. В ЖО4 и банке названия часто
        // отличаются, поэтому контрагент усиливает оценку, но не является
        // жестким фильтром.
        bool counterparty_match = false;
        bool has_counterparty_data =
            !doc.counterparty_name.empty() && !match.counterparty_name.empty();
        if (!doc.counterparty_name.empty() && !match.counterparty_name.empty()) {
            counterparty_match = counterpartyNamesMatch(doc.counterparty_name, match.counterparty_name);
            if (counterparty_match) {
                match.match_score += 5;
                match.match_reasons += "Контрагент; ";
            }
        }

        if (!counterparty_match) {
            if (require_counterparty && has_counterparty_data) {
                match.match_score -= 10;
                match.match_reasons += "Контрагент отличается; ";
            } else if (!has_counterparty_data) {
                match.match_reasons += "Контрагент не сравнен; ";
            }
        }

        // Добавляем только если есть хоть какое-то совпадение помимо контрагента
        if (match.match_score <= 5) {
            continue;
        }

        matches.push_back(match);
    }

    sqlite3_finalize(stmt);

    // Сортируем по score (по убыванию)
    std::sort(matches.begin(), matches.end(), 
        [](const PaymentMatch& a, const PaymentMatch& b) {
            return a.match_score > b.match_score;
        });

    return matches;
}

bool DatabaseManager::linkBasePaymentDocumentToPayment(int doc_id, int payment_id) {
    if (!db) return false;

    PaymentBaseDocumentLink link;
    link.payment_id = payment_id;
    link.base_document_id = doc_id;
    link.linked_amount = 0.0;
    link.match_score = 100;
    link.match_status = "confirmed";
    link.match_reason = "Ручная связь";

    if (!addPaymentBaseDocumentLink(link)) {
        return false;
    }
    
    std::string sql = "UPDATE BasePaymentDocuments SET payment_id = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for linkBasePaymentDocumentToPayment: "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, payment_id);
    sqlite3_bind_int(stmt, 2, doc_id);
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        std::cerr << "Failed to link BasePaymentDocument to Payment: "
                  << sqlite3_errmsg(db) << std::endl;
    }
    
    sqlite3_finalize(stmt);
    return success;
}

// ==================== Reconciliation Methods ====================

std::vector<DatabaseManager::ReconciliationRecord> DatabaseManager::getReconciliationData(const std::string& filter) {
    std::vector<ReconciliationRecord> records;
    if (!db) return records;

    std::string sql =
        "SELECT "
        "  p.id as payment_id, p.date as payment_date, p.doc_number as payment_doc_num, "
        "  p.amount as payment_amount, p.description as payment_desc, c.name as counterparty, "
        "  pd.id as pd_id, pd.amount as pd_amount, k.code as kosgu_code, "
        "  bpd.id as bpd_id, bpd.date as bpd_date, bpd.number as bpd_number, "
        "  bpd.document_name as bpd_name, "
        "  IFNULL(SUM(bpdd2.amount), 0.0) as bpd_total, "
        "  bpd.is_for_checking, bpd.is_checked, "
        "  bpd.contract_id as contract_id, "
        "  ctr.number as contract_number, ctr.date as contract_date, "
        "  bpdd.id as bpdd_id, bpdd.operation_content, bpdd.debit_account, "
        "  bpdd.credit_account, bpdd_k.code as bpdd_kosgu, bpdd.amount as bpdd_amount "
        "FROM Payments p "
        "LEFT JOIN PaymentDetails pd ON p.id = pd.payment_id "
        "LEFT JOIN Counterparties c ON p.counterparty_id = c.id "
        "LEFT JOIN KOSGU k ON pd.kosgu_id = k.id "
        "LEFT JOIN BasePaymentDocuments bpd ON pd.base_document_id = bpd.id "
        "LEFT JOIN Contracts ctr ON bpd.contract_id = ctr.id "
        "LEFT JOIN BasePaymentDocumentDetails bpdd ON bpd.id = bpdd.document_id "
        "LEFT JOIN KOSGU bpdd_k ON bpdd.kosgu_id = bpdd_k.id "
        "LEFT JOIN BasePaymentDocumentDetails bpdd2 ON bpd.id = bpdd2.document_id "
        "WHERE p.id IS NOT NULL "
        "GROUP BY p.id, pd.id, bpd.id, bpdd.id "
        "ORDER BY p.date, p.doc_number";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for getReconciliationData: "
                  << sqlite3_errmsg(db) << std::endl;
        return records;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ReconciliationRecord rec;
        rec.payment_id = sqlite3_column_int(stmt, 0);
        const unsigned char* v = sqlite3_column_text(stmt, 1);
        rec.payment_date = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 2);
        rec.payment_doc_number = v ? (const char*)v : "";
        rec.payment_amount = sqlite3_column_double(stmt, 3);
        v = sqlite3_column_text(stmt, 4);
        rec.payment_description = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 5);
        rec.counterparty_name = v ? (const char*)v : "";
        rec.payment_detail_id = sqlite3_column_int(stmt, 6);
        rec.detail_amount = sqlite3_column_double(stmt, 7);
        v = sqlite3_column_text(stmt, 8);
        rec.kosgu_code = v ? (const char*)v : "";

        rec.base_doc_id = sqlite3_column_int(stmt, 9);
        v = sqlite3_column_text(stmt, 10);
        rec.base_doc_date = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 11);
        rec.base_doc_number = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 12);
        rec.base_doc_name = v ? (const char*)v : "";
        rec.base_doc_total = sqlite3_column_double(stmt, 13);
        rec.base_doc_for_checking = sqlite3_column_int(stmt, 14) != 0;
        rec.base_doc_checked = sqlite3_column_int(stmt, 15) != 0;
        rec.contract_id = sqlite3_column_int(stmt, 16);
        v = sqlite3_column_text(stmt, 17);
        rec.contract_number = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 18);
        rec.contract_date = v ? (const char*)v : "";

        rec.base_detail_id = sqlite3_column_int(stmt, 19);
        v = sqlite3_column_text(stmt, 20);
        rec.base_detail_content = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 21);
        rec.base_detail_debit = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 22);
        rec.base_detail_credit = v ? (const char*)v : "";
        v = sqlite3_column_text(stmt, 23);
        rec.base_detail_kosgu = v ? (const char*)v : "";
        rec.base_detail_amount = sqlite3_column_double(stmt, 24);

        // Фильтрация
        if (!filter.empty()) {
            std::string f = filter;
            std::transform(f.begin(), f.end(), f.begin(), ::tolower);
            std::string combined = rec.payment_date + " " + rec.payment_doc_number + " " +
                                   rec.counterparty_name + " " + rec.base_doc_number + " " +
                                   rec.base_detail_content;
            std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
            if (combined.find(f) == std::string::npos) continue;
        }

        records.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return records;
}
