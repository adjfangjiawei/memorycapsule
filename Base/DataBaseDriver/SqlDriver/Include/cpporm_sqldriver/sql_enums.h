// cpporm_sqldriver/sql_enums.h
#pragma once

namespace cpporm_sqldriver {

    // --- Enums used across driver interfaces ---

    enum class Feature {
        Transactions,
        QuerySize,
        BLOB,
        Unicode,
        PreparedQueries,
        NamedPlaceholders,
        PositionalPlaceholders,
        LastInsertId,
        BatchOperations,
        SimpleScrollOnError,
        EventNotifications,
        FinishQuery,
        MultipleResultSets,
        LowPrecisionNumbers,
        CancelQuery,
        InsertAndReturnId,
        NamedSavepoints,
        ThreadSafe,
        SchemaOperations,
        SequenceOperations,
        UpdatableCursors,
        TransactionIsolationLevel,
        GetTypeInfo,
        PingConnection,
        SetQueryTimeout,
        StreamBlob,
        CallableStatements,
        BatchWithErrorDetails
    };

    enum class IdentifierType { Table, Field, Index, Schema, Sequence, Trigger, View, Constraint, User, Role, Procedure, Function };

    enum class StatementType { Select, Insert, Update, Delete, DDL, DCL, TCL, Call, Begin, Commit, Rollback, Savepoint, Unknown };

    enum class TransactionIsolationLevel { ReadUncommitted, ReadCommitted, RepeatableRead, Serializable, Snapshot, Default };

    enum class ParamType { In, Out, InOut, Binary, ReturnValue };

    enum class CursorMovement { Absolute, RelativeFirst, RelativeNext, RelativePrevious, RelativeLast };

    namespace ISqlDriverNs {
        enum class TableType { All, Tables, Views, SystemTables, Aliases, Synonyms, TemporaryTables, GlobalTemporaryTables };
    }  // namespace ISqlDriverNs

    namespace SqlResultNs {
        enum class ScrollMode { ForwardOnly, Scrollable };
        enum class ConcurrencyMode { ReadOnly, Updatable };
        enum class NamedBindingSyntax { Colon, AtSign, QuestionMark };
    }  // namespace SqlResultNs

}  // namespace cpporm_sqldriver