#pragma once

// A connection wrapper for the SQL store tests (Phase 19 Days 262-263): it reports that it cannot add a column and refuses `ALTER`, so a migration takes its rebuild path on SQLite.

#include "database/database.h"
#include "containers/unique_ptr.h"

namespace engine_placeholder::storage::test {

class NoAlterConnection final : public database::Connection {
public:
    explicit NoAlterConnection(database::Connection& inner) : inner_(&inner) {}
    bool Supports(database::Feature f) const override { return f != database::Feature::AddColumn && inner_->Supports(f); }
    database::Status Prepare(database::SqlText sql, UniquePtr<database::Statement>* out) override {
        if (sql.View().StartsWith(StringView("ALTER"))) {
            return database::Status::Of(database::ErrorCode::Unsupported);
        }
        return inner_->Prepare(sql, out);
    }
    database::Status Execute(database::SqlText sql) override {
        if (sql.View().StartsWith(StringView("ALTER"))) {
            return database::Status::Of(database::ErrorCode::Unsupported);
        }
        return inner_->Execute(sql);
    }
    database::Status Begin(database::TransactionKind k) override { return inner_->Begin(k); }
    database::Status Commit() override { return inner_->Commit(); }
    database::Status Rollback() override { return inner_->Rollback(); }
    bool InTransaction() const override { return inner_->InTransaction(); }
    database::Status Savepoint(StringView n) override { return inner_->Savepoint(n); }
    database::Status ReleaseSavepoint(StringView n) override { return inner_->ReleaseSavepoint(n); }
    database::Status RollbackToSavepoint(StringView n) override { return inner_->RollbackToSavepoint(n); }
    i64 LastInsertRowId() const override { return inner_->LastInsertRowId(); }
    i64 ChangedRows() const override { return inner_->ChangedRows(); }
    database::Status SchemaVersion(i64* out) override { return inner_->SchemaVersion(out); }
    database::Status WriteSchemaVersion(i64 v) override { return inner_->WriteSchemaVersion(v); }

private:
    database::Connection* inner_;
};

} // namespace engine_placeholder::storage::test
