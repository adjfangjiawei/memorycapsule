#include "cpporm/query_builder_core.h" // For QueryBuilder definition
#include <QDebug>
#include <QMetaType> // For QMetaType in AsSubquery and toQVariant
#include <QVariant>  // For QVariant
#include <sstream>   // For std::ostringstream in buildConditionClauseGroup
#include <variant>   // For std::visit on QueryValue

namespace cpporm {

QString QueryBuilder::getFromSourceName() const {
  if (std::holds_alternative<std::string>(state_.from_clause_source_)) {
    const std::string &table_name_str =
        std::get<std::string>(state_.from_clause_source_);
    if (!table_name_str.empty()) {
      return QString::fromStdString(table_name_str);
    }
    // If from_clause_source_ is an empty string, it means use model's table
    // name
    if (state_.model_meta_ && !state_.model_meta_->table_name.empty()) {
      return QString::fromStdString(state_.model_meta_->table_name);
    }
  } else if (std::holds_alternative<SubquerySource>(
                 state_.from_clause_source_)) {
    // If it's a subquery, the "name" is its alias
    return QString::fromStdString(
        std::get<SubquerySource>(state_.from_clause_source_).alias);
  }
  return QString(); // Default empty if no source determined
}

std::expected<SubqueryExpression, Error> QueryBuilder::AsSubquery() const {
  auto [qsql_string, qvariant_bindings] =
      buildSelectSQL(true); // true for subquery context
  if (qsql_string.isEmpty()) {
    return std::unexpected(Error(ErrorCode::StatementPreparationError,
                                 "Failed to build SQL for subquery."));
  }
  std::vector<QueryValueVariantForSubquery> subquery_native_bindings;
  subquery_native_bindings.reserve(qvariant_bindings.size());

  for (const QVariant &qv : qvariant_bindings) {
    if (qv.isNull() || !qv.isValid()) {
      subquery_native_bindings.push_back(nullptr);
    } else {
      QMetaType::Type type_id_val;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      type_id_val = static_cast<QMetaType::Type>(qv.typeId());
#else
      type_id_val = static_cast<QMetaType::Type>(qv.type());
#endif

      if (type_id_val == QMetaType::UnknownType &&
          qv.userType() != QMetaType::UnknownType) {
        type_id_val = static_cast<QMetaType::Type>(qv.userType());
      }

      if (type_id_val == QMetaType::Int) {
        subquery_native_bindings.push_back(qv.toInt());
      } else if (type_id_val == QMetaType::LongLong ||
                 type_id_val == QMetaType::ULongLong) {
        subquery_native_bindings.push_back(qv.toLongLong());
      } else if (type_id_val == QMetaType::Double) {
        subquery_native_bindings.push_back(qv.toDouble());
      } else if (type_id_val == QMetaType::QString) {
        subquery_native_bindings.push_back(qv.toString().toStdString());
      } else if (type_id_val == QMetaType::Bool) {
        subquery_native_bindings.push_back(qv.toBool());
      } else if (type_id_val == QMetaType::QDateTime) {
        subquery_native_bindings.push_back(qv.toDateTime());
      } else if (type_id_val == QMetaType::QDate) {
        subquery_native_bindings.push_back(qv.toDate());
      } else if (type_id_val == QMetaType::QTime) {
        subquery_native_bindings.push_back(qv.toTime());
      } else if (type_id_val == QMetaType::QByteArray) {
        subquery_native_bindings.push_back(qv.toByteArray());
      } else {
        qWarning()
            << "cpporm QueryBuilder::AsSubquery: Unhandled QVariant typeId "
            << static_cast<int>(type_id_val) << " (" << qv.typeName()
            << ") for native conversion into SubqueryExpression bindings.";
        return std::unexpected(Error(
            ErrorCode::MappingError,
            "Unhandled QVariant type in AsSubquery bindings conversion: " +
                std::string(qv.typeName())));
      }
    }
  }
  return SubqueryExpression(qsql_string.toStdString(),
                            subquery_native_bindings);
}

std::string QueryBuilder::quoteSqlIdentifier(const std::string &identifier) {
  if (identifier.empty())
    return "";
  if (identifier == "*" || identifier.find('(') != std::string::npos ||
      identifier.find(')') != std::string::npos ||
      (identifier.front() == '`' && identifier.back() == '`') ||
      (identifier.front() == '"' && identifier.back() == '"')) {
    return identifier;
  }

  char quote_char = '`';
  std::string result;
  size_t start_pos = 0;
  size_t dot_pos;
  std::string temp_identifier = identifier;

  while ((dot_pos = temp_identifier.find('.', start_pos)) !=
         std::string::npos) {
    result += quote_char +
              temp_identifier.substr(start_pos, dot_pos - start_pos) +
              quote_char + ".";
    start_pos = dot_pos + 1;
  }
  result += quote_char + temp_identifier.substr(start_pos) + quote_char;
  return result;
}

QVariant QueryBuilder::toQVariant(const QueryValue &qv,
                                  QVariantList &subquery_bindings_accumulator) {
  return std::visit(
      [&subquery_bindings_accumulator](auto &&arg) -> QVariant {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          return QVariant(QMetaType(QMetaType::UnknownType));
        } else if constexpr (std::is_same_v<T, int>) {
          return QVariant(arg);
        } else if constexpr (std::is_same_v<T, long long>) {
          return QVariant(static_cast<qlonglong>(arg));
        } else if constexpr (std::is_same_v<T, double>) {
          return QVariant(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return QVariant(QString::fromStdString(arg));
        } else if constexpr (std::is_same_v<T, bool>) {
          return QVariant(arg);
        } else if constexpr (std::is_same_v<T, QDateTime>) {
          return QVariant(arg);
        } else if constexpr (std::is_same_v<T, QDate>) {
          return QVariant(arg);
        } else if constexpr (std::is_same_v<T, QTime>) {
          return QVariant(arg);
        } else if constexpr (std::is_same_v<T, QByteArray>) {
          return QVariant(arg);
        } else if constexpr (std::is_same_v<T, SubqueryExpression>) {
          for (const auto &sub_binding_variant : arg.bindings) {
            std::visit(
                [&subquery_bindings_accumulator](auto &&sub_val) {
                  using SubVT = std::decay_t<decltype(sub_val)>;
                  if constexpr (std::is_same_v<SubVT, std::nullptr_t>) {
                    subquery_bindings_accumulator.append(
                        QVariant(QMetaType(QMetaType::UnknownType)));
                  } else if constexpr (std::is_same_v<SubVT, int> ||
                                       std::is_same_v<SubVT, long long> ||
                                       std::is_same_v<SubVT, double> ||
                                       std::is_same_v<SubVT, std::string> ||
                                       std::is_same_v<SubVT, bool> ||
                                       std::is_same_v<SubVT, QDateTime> ||
                                       std::is_same_v<SubVT, QDate> ||
                                       std::is_same_v<SubVT, QTime> ||
                                       std::is_same_v<SubVT, QByteArray>) {
                    if constexpr (std::is_same_v<SubVT, std::string>) {
                      subquery_bindings_accumulator.append(
                          QString::fromStdString(sub_val));
                    } else {
                      subquery_bindings_accumulator.append(
                          QVariant::fromValue(sub_val));
                    }
                  } else {
                    qWarning()
                        << "QueryBuilder::toQVariant (Subquery binding): "
                           "Unhandled native type in subquery binding: "
                        << typeid(SubVT).name();
                  }
                },
                sub_binding_variant);
          }
          return QVariant(QString::fromStdString("(" + arg.sql_string + ")"));
        }
        qWarning()
            << "QueryBuilder::toQVariant: Unhandled QueryValue variant type: "
            << typeid(T).name();
        return QVariant();
      },
      qv);
}

QueryValue QueryBuilder::qvariantToQueryValue(const QVariant &qv) {
  if (qv.isNull() || !qv.isValid())
    return nullptr;

  QMetaType::Type type_id_val;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  type_id_val = static_cast<QMetaType::Type>(qv.typeId());
#else
  type_id_val = static_cast<QMetaType::Type>(qv.type());
#endif

  if (type_id_val == QMetaType::UnknownType &&
      qv.userType() != QMetaType::UnknownType) {
    type_id_val = static_cast<QMetaType::Type>(qv.userType());
  }

  if (type_id_val == QMetaType::Int)
    return qv.toInt();
  if (type_id_val == QMetaType::LongLong)
    return qv.toLongLong();
  if (type_id_val == QMetaType::ULongLong)
    return qv.toLongLong(); // Note: Conversion to signed long long
  if (type_id_val == QMetaType::UInt)
    return static_cast<int>(qv.toUInt());
  if (type_id_val == QMetaType::Double)
    return qv.toDouble();
  if (type_id_val == QMetaType::Float)
    return static_cast<double>(qv.toFloat());
  if (type_id_val == QMetaType::QString)
    return qv.toString().toStdString();
  if (type_id_val == QMetaType::Bool)
    return qv.toBool();
  if (type_id_val == QMetaType::QDateTime)
    return qv.toDateTime();
  if (type_id_val == QMetaType::QDate)
    return qv.toDate();
  if (type_id_val == QMetaType::QTime)
    return qv.toTime();
  if (type_id_val == QMetaType::QByteArray)
    return qv.toByteArray();

  qWarning() << "QueryBuilder::qvariantToQueryValue: Unhandled QVariant type "
                "for QueryValue conversion: "
             << qv.typeName() << "(TypeId: " << static_cast<int>(type_id_val)
             << ")";
  return nullptr;
}

std::pair<std::string, std::vector<QueryValue>>
QueryBuilder::buildConditionClauseGroup() const {
  std::ostringstream group_sql_stream;
  QVariantList group_qbindings;

  std::ostringstream user_defined_conditions_ss;
  bool any_user_condition_written = false;

  if (!state_.where_conditions_.empty()) {
    QueryBuilder::build_one_condition_block_internal_static_helper(
        user_defined_conditions_ss, group_qbindings, state_.where_conditions_,
        "AND", false);
    any_user_condition_written = true;
  }
  if (!state_.or_conditions_.empty()) {
    if (any_user_condition_written)
      user_defined_conditions_ss << " OR ";
    QueryBuilder::build_one_condition_block_internal_static_helper(
        user_defined_conditions_ss, group_qbindings, state_.or_conditions_,
        "OR", false);
    any_user_condition_written = true;
  }
  if (!state_.not_conditions_.empty()) {
    if (any_user_condition_written)
      user_defined_conditions_ss << " AND ";
    QueryBuilder::build_one_condition_block_internal_static_helper(
        user_defined_conditions_ss, group_qbindings, state_.not_conditions_,
        "AND", true);
    any_user_condition_written = true;
  }
  std::string user_conditions_part_sql = user_defined_conditions_ss.str();

  std::string soft_delete_fragment_for_this_group;
  if (state_.model_meta_ && state_.apply_soft_delete_scope_) {
    bool apply_sd_on_this_from_source = false;
    QString current_from_qstr = this->getFromSourceName();
    if (!current_from_qstr.isEmpty() &&
        state_.model_meta_->table_name == current_from_qstr.toStdString()) {
      apply_sd_on_this_from_source = true;
    }

    if (apply_sd_on_this_from_source) {
      if (const FieldMeta *deleted_at_field =
              state_.model_meta_->findFieldWithFlag(FieldFlag::DeletedAt)) {
        soft_delete_fragment_for_this_group =
            QueryBuilder::quoteSqlIdentifier(
                this->getFromSourceName().toStdString()) +
            "." + QueryBuilder::quoteSqlIdentifier(deleted_at_field->db_name) +
            " IS NULL";
      }
    }
  }

  if (!soft_delete_fragment_for_this_group.empty()) {
    group_sql_stream << "(" << soft_delete_fragment_for_this_group << ")";
    if (!user_conditions_part_sql.empty()) {
      group_sql_stream << " AND ";
    }
  }
  if (!user_conditions_part_sql.empty()) {
    group_sql_stream << user_conditions_part_sql;
  }

  std::string final_built_sql_group = group_sql_stream.str();
  if (final_built_sql_group.empty()) {
    return {"", {}};
  }

  std::vector<QueryValue> native_args;
  native_args.reserve(group_qbindings.size());
  for (const QVariant &qv : group_qbindings) {
    native_args.push_back(QueryBuilder::qvariantToQueryValue(qv));
  }

  return {"(" + final_built_sql_group + ")", native_args};
}

QString QueryBuilder::toSqlDebug() const {
  auto [sql_qstr, params_list] = this->buildSelectSQL();
  QString debug_sql = sql_qstr;
  int current_param_idx = 0;
  int placeholder_pos = 0;

  while (current_param_idx < params_list.size()) {
    placeholder_pos = debug_sql.indexOf('?', placeholder_pos);
    if (placeholder_pos == -1)
      break;

    QVariant v = params_list.at(current_param_idx);
    QString param_str_val;

    QMetaType::Type v_type_id;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    v_type_id = static_cast<QMetaType::Type>(v.typeId());
#else
    v_type_id = static_cast<QMetaType::Type>(v.type());
#endif

    if (v.isNull() || !v.isValid() || v_type_id == QMetaType::UnknownType) {
      param_str_val = "NULL";
    } else if (v_type_id == QMetaType::QString) {
      param_str_val = "'" + v.toString().replace("'", "''") + "'";
    } else if (v_type_id == QMetaType::QByteArray) {
      param_str_val =
          "'<BinaryData:" + QString::number(v.toByteArray().size()) + "bytes>'";
    } else if (v_type_id == QMetaType::QDateTime) {
      param_str_val = "'" + v.toDateTime().toString(Qt::ISODateWithMs) + "'";
    } else if (v_type_id == QMetaType::QDate) {
      param_str_val = "'" + v.toDate().toString(Qt::ISODate) + "'";
    } else if (v_type_id == QMetaType::QTime) {
      param_str_val = "'" + v.toTime().toString(Qt::ISODateWithMs) + "'";
    } else if (v_type_id == QMetaType::Bool) {
      param_str_val = v.toBool() ? "TRUE" : "FALSE";
    } else {
      param_str_val = v.toString();
    }

    debug_sql.replace(placeholder_pos, 1, param_str_val);
    placeholder_pos += param_str_val.length();
    current_param_idx++;
  }
  return debug_sql;
}

} // namespace cpporm