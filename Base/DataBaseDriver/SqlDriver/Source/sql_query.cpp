// SqlDriver/Source/sql_query.cpp
#include "sqldriver/sql_query.h"

#include <stdexcept>  // For potential std::logic_error

#include "sqldriver/i_sql_driver.h"  // For ISqlDriver interaction via SqlDatabase
#include "sqldriver/sql_database.h"  // For SqlDatabase context
#include "sqldriver/sql_result.h"    // For SqlResult interface

namespace cpporm_sqldriver {

    // --- Helper ---
    bool SqlQuery::checkResult(const char* methodName) const {
        if (!m_result) {
            // This state should ideally be caught by m_db check first or result creation failure.
            // If m_result is null here, it's an internal logic error.
            // For robustness, if SqlDatabase::driver() could return null:
            // if (m_db && m_db->driver()) {
            //    // This is tricky, as lastError() is on SqlQuery itself.
            //    // const_cast<SqlQuery*>(this)->m_result_error = SqlError(...)
            // }
            return false;
        }
        return true;
    }

    void SqlQuery::updateSelectStatus() {
        m_is_select_query = false;
        if (m_last_query_text.length() > 6) {
            std::string prefix = m_last_query_text.substr(0, 6);
            for (char& c : prefix) {
                c = static_cast<char>(std::toupper(c));
            }
            if (prefix == "SELECT") {
                m_is_select_query = true;
            }
        }
    }

    // --- Construction and Destruction ---
    SqlQuery::SqlQuery(SqlDatabase& db)
        : m_db(&db),
          m_result(nullptr),
          m_is_active(false),
          m_is_select_query(false),
          m_precision_policy(NumericalPrecisionPolicy::LowPrecision),  // Default
          m_binding_syntax(SqlResultNs::NamedBindingSyntax::Colon) {   // Default
        if (m_db && m_db->driver()) {
            m_result = m_db->driver()->createResult();
        }
        // If m_result is nullptr here, subsequent operations will fail in checkResult
    }

    SqlQuery::SqlQuery(const std::string& query, SqlDatabase* db) : m_db(db), m_result(nullptr), m_last_query_text(query), m_is_active(false), m_is_select_query(false), m_precision_policy(NumericalPrecisionPolicy::LowPrecision), m_binding_syntax(SqlResultNs::NamedBindingSyntax::Colon) {
        if (m_db && m_db->driver()) {
            m_result = m_db->driver()->createResult();
        }
        updateSelectStatus();
    }

    SqlQuery::~SqlQuery() {
        // m_result (unique_ptr) will be automatically destroyed.
        // finish() might be called explicitly or by destructor of SqlResult.
    }

    // --- Move Semantics ---
    SqlQuery::SqlQuery(SqlQuery&& other) noexcept
        : m_db(other.m_db), m_result(std::move(other.m_result)), m_last_query_text(std::move(other.m_last_query_text)), m_is_active(other.m_is_active), m_is_select_query(other.m_is_select_query), m_precision_policy(other.m_precision_policy), m_binding_syntax(other.m_binding_syntax) {
        other.m_db = nullptr;  // Null out moved-from object's db pointer
        other.m_is_active = false;
    }

    SqlQuery& SqlQuery::operator=(SqlQuery&& other) noexcept {
        if (this != &other) {
            m_db = other.m_db;
            m_result = std::move(other.m_result);
            m_last_query_text = std::move(other.m_last_query_text);
            m_is_active = other.m_is_active;
            m_is_select_query = other.m_is_select_query;
            m_precision_policy = other.m_precision_policy;
            m_binding_syntax = other.m_binding_syntax;

            other.m_db = nullptr;
            other.m_is_active = false;
        }
        return *this;
    }

    // --- Preparation and Execution ---
    bool SqlQuery::prepare(const std::string& query, SqlResultNs::ScrollMode scroll, SqlResultNs::ConcurrencyMode concur) {
        if (!checkResult("prepare")) return false;
        m_last_query_text = query;
        updateSelectStatus();
        m_result->setNamedBindingSyntax(m_binding_syntax);                 // Ensure syntax is set before prepare
        bool success = m_result->prepare(query, nullptr, scroll, concur);  // Assuming no type hints for now
        m_is_active = success;
        return success;
    }

    bool SqlQuery::exec() {
        if (!checkResult("exec")) return false;
        if (!m_is_active && m_last_query_text.empty()) {  // Cannot exec if not prepared and no query stored
            // set an error on m_result or return an error via lastError()
            return false;
        }
        if (!m_is_active && !m_last_query_text.empty()) {  // If query text exists but not prepared
            if (!prepare(m_last_query_text)) return false;
        }

        bool success = m_result->exec();
        // m_is_active remains true if exec succeeds, allowing data fetching.
        // It becomes false after finish() or if exec fails catastrophically.
        // For now, exec success implies active.
        m_is_active = success;
        return success;
    }

    bool SqlQuery::exec(const std::string& query) {
        m_last_query_text = query;
        updateSelectStatus();
        // No need to call prepare explicitly, as SqlResult::exec(query) is not standard.
        // We should prepare then exec.
        if (!prepare(query)) {  // Prepare first
            m_is_active = false;
            return false;
        }
        return exec();  // Then execute
    }

    bool SqlQuery::setQueryTimeout(int seconds) {
        if (!checkResult("setQueryTimeout")) return false;
        return m_result->setQueryTimeout(seconds);
    }

    // --- Binding Values ---
    void SqlQuery::bindValue(int pos, const SqlValue& val, ParamType type) {
        if (!checkResult("bindValue")) return;
        // SqlResult expects 0-based typically, but some APIs are 1-based. Assume 0-based for SqlResult interface.
        // SqlQuery API might be 0-based or 1-based. Let's assume 0-based for SqlQuery too for consistency.
        m_result->addPositionalBindValue(val, type);  // Assuming addPositionalBindValue appends in order
                                                      // or setPositionalBindValue(pos, val, type) if exists
    }

    void SqlQuery::bindValue(const std::string& placeholderName, const SqlValue& val, ParamType type) {
        if (!checkResult("bindValue")) return;
        m_result->setNamedBindValue(placeholderName, val, type);
    }

    void SqlQuery::addBindValue(const SqlValue& val, ParamType type) {
        if (!checkResult("addBindValue")) return;
        m_result->addPositionalBindValue(val, type);
    }

    SqlValue SqlQuery::boundValue(int /*pos*/) const {
        if (!checkResult("boundValue")) return SqlValue();
        // This requires SqlResult to have a method like `getPositionalBoundValue(pos)`
        // For now, not directly supported as SqlResult interface doesn't mandate it.
        return SqlValue();  // Placeholder
    }

    SqlValue SqlQuery::boundValue(const std::string& /*placeholderName*/) const {
        if (!checkResult("boundValue")) return SqlValue();
        // Requires SqlResult to have `getNamedBoundValue(name)`
        return SqlValue();  // Placeholder
    }

    void SqlQuery::clearBoundValues() {
        if (!checkResult("clearBoundValues")) return;
        m_result->clearBindValues();
    }

    // --- Navigation ---
    bool SqlQuery::next() {
        if (!checkResult("next") || !m_is_active) return false;
        SqlRecord temp_dummy_record;  // fetchNext in SqlResult needs a buffer
        return m_result->fetchNext(temp_dummy_record);
    }

    bool SqlQuery::previous() {
        if (!checkResult("previous") || !m_is_active) return false;
        SqlRecord temp_dummy_record;
        return m_result->fetchPrevious(temp_dummy_record);
    }

    bool SqlQuery::first() {
        if (!checkResult("first") || !m_is_active) return false;
        SqlRecord temp_dummy_record;
        return m_result->fetchFirst(temp_dummy_record);
    }

    bool SqlQuery::last() {
        if (!checkResult("last") || !m_is_active) return false;
        SqlRecord temp_dummy_record;
        return m_result->fetchLast(temp_dummy_record);
    }

    bool SqlQuery::seek(int index, CursorMovement movement) {
        if (!checkResult("seek") || !m_is_active) return false;
        SqlRecord temp_dummy_record;
        return m_result->fetch(index, temp_dummy_record, movement);
    }

    // --- Data Retrieval ---
    SqlRecord SqlQuery::recordMetadata() const {
        if (!checkResult("recordMetadata")) return SqlRecord();
        return m_result->recordMetadata();
    }

    SqlRecord SqlQuery::currentFetchedRow() const {
        if (!checkResult("currentFetchedRow") || !m_is_active) return SqlRecord();
        return m_result->currentFetchedRow();
    }

    SqlValue SqlQuery::value(int index) const {
        if (!checkResult("value") || !m_is_active) return SqlValue();
        // SqlResult::data(index) is the underlying call
        return m_result->data(index);
    }

    SqlValue SqlQuery::value(const std::string& name) const {
        if (!checkResult("value") || !m_is_active) return SqlValue();
        SqlRecord meta = m_result->recordMetadata();
        int index = meta.indexOf(name);
        if (index != -1) {
            return m_result->data(index);
        }
        return SqlValue();  // Not found
    }

    bool SqlQuery::isNull(int index) const {
        if (!checkResult("isNull") || !m_is_active) return true;  // Treat as null if not valid
        return m_result->isNull(index);
    }

    bool SqlQuery::isNull(const std::string& name) const {
        if (!checkResult("isNull") || !m_is_active) return true;
        SqlRecord meta = m_result->recordMetadata();
        int index = meta.indexOf(name);
        if (index != -1) {
            return m_result->isNull(index);
        }
        return true;  // Not found, treat as null
    }

    SqlField SqlQuery::field(int index) const {
        if (!checkResult("field") || !m_is_active) return SqlField();
        return m_result->field(index);
    }
    SqlField SqlQuery::field(const std::string& name) const {
        if (!checkResult("field") || !m_is_active) return SqlField();
        SqlRecord meta = m_result->recordMetadata();
        int index = meta.indexOf(name);
        if (index != -1) {
            return m_result->field(index);
        }
        return SqlField();
    }

    // --- Information / State ---
    int SqlQuery::at() const {
        if (!checkResult("at") || !m_is_active) return -1;
        return m_result->at();
    }

    int SqlQuery::size() const {
        if (!checkResult("size")) return -1;
        // SqlResult::size() can be mutable if it needs to query or count
        return const_cast<SqlResult*>(m_result.get())->size();
    }

    bool SqlQuery::isActive() const {
        // Active means prepared and potentially executed, but not finished.
        // And underlying result object also says it's active.
        return m_is_active && m_result && m_result->isActive();
    }

    bool SqlQuery::isValid() const {
        // Valid means the result set (if any) can be navigated.
        return m_is_active && m_result && m_result->isValid();
    }

    bool SqlQuery::isSelect() const {
        // Based on heuristic updated during prepare/exec(query)
        return m_is_select_query;
    }

    bool SqlQuery::setForwardOnly(bool forward) {
        if (!checkResult("setForwardOnly")) return false;
        return m_result->setForwardOnly(forward);
    }

    bool SqlQuery::setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy) {
        if (!checkResult("setNumericalPrecisionPolicy")) return false;
        bool success = m_result->setNumericalPrecisionPolicy(policy);
        if (success) m_precision_policy = policy;
        return success;
    }
    NumericalPrecisionPolicy SqlQuery::numericalPrecisionPolicy() const {
        return m_precision_policy;
    }

    SqlError SqlQuery::lastError() const {
        if (m_result) {
            return m_result->error();
        }
        if (m_db) {  // If result creation failed, error might be on db
            return m_db->lastError();
        }
        return SqlError(ErrorCategory::DriverInternal, "SqlQuery is not properly initialized.", "SqlQuery::lastError");
    }

    std::string SqlQuery::lastQuery() const {
        return m_last_query_text;
    }

    std::string SqlQuery::executedQuery() const {
        if (!checkResult("executedQuery")) return m_last_query_text;  // Fallback
        return m_result->preparedQueryText();                         // Or lastQuery() if preparedQueryText is only post-placeholder
    }

    // --- Post-Execution Information ---
    long long SqlQuery::numRowsAffected() const {
        if (!checkResult("numRowsAffected")) return -1;  // Or 0
        return m_result->numRowsAffected();
    }

    SqlValue SqlQuery::lastInsertId() const {
        if (!checkResult("lastInsertId")) return SqlValue();
        return m_result->lastInsertId();
    }

    // --- Control ---
    void SqlQuery::finish() {
        if (m_result) {
            m_result->finish();
        }
        m_is_active = false;
    }

    void SqlQuery::clear() {
        if (m_result) {
            m_result->clear();  // SqlResult::clear should also reset binds
        }
        m_last_query_text.clear();
        m_is_active = false;
        m_is_select_query = false;
    }

    // --- Associated objects ---
    SqlDatabase* SqlQuery::database() const {
        return m_db;
    }

    ISqlDriver* SqlQuery::driver() const {
        return m_db ? m_db->driver() : nullptr;
    }

    SqlResult* SqlQuery::result() const {
        return m_result.get();
    }

    // --- Multiple Result Sets ---
    bool SqlQuery::nextResult() {
        if (!checkResult("nextResult")) return false;
        bool success = m_result->nextResult();
        if (success) {
            m_is_active = true;    // New result set might be active
            updateSelectStatus();  // Re-check if new result is from SELECT
        } else {
            m_is_active = false;  // No more results or error
        }
        return success;
    }

    // --- Placeholder syntax ---
    bool SqlQuery::setNamedBindingSyntax(SqlResultNs::NamedBindingSyntax syntax) {
        m_binding_syntax = syntax;
        if (m_result) {  // Pass to underlying result object if it's already created
            return m_result->setNamedBindingSyntax(syntax);
        }
        return true;  // Store for when result is created
    }
    SqlResultNs::NamedBindingSyntax SqlQuery::namedBindingSyntax() const {
        return m_binding_syntax;
    }

}  // namespace cpporm_sqldriver