#include "database/database.h"

namespace engine_placeholder::database {

const char* FeatureName(Feature feature) {
    switch (feature) {
    case Feature::Savepoints: return "savepoints";
    case Feature::WithoutRowId: return "without_rowid";
    case Feature::AddColumn: return "add_column";
    case Feature::NamedParameters: return "named_parameters";
    case Feature::ConcurrentReaders: return "concurrent_readers";
    case Feature::WriteAheadLog: return "write_ahead_log";
    case Feature::ForeignKeys: return "foreign_keys";
    }
    return "unknown";
}

Transaction::Transaction(Connection& connection, TransactionKind kind) : connection_(connection) {
    begin_ = connection_.Begin(kind);
    open_ = begin_.Ok();
}

Transaction::~Transaction() {
    if (open_) {
        static_cast<void>(connection_.Rollback()); // a failed rollback leaves the backend to discard the transaction when the connection closes
    }
}

Status Transaction::Commit() {
    if (!open_) {
        return Status::Make(ErrorCode::Misuse, 0, "Transaction::Commit: the transaction is not open");
    }
    const Status status = connection_.Commit();
    if (status.Ok()) {
        open_ = false;
    }
    return status;
}

} // namespace engine_placeholder::database
