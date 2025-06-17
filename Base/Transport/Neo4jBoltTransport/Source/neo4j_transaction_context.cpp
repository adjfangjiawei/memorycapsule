#include "neo4j_bolt_transport/neo4j_transaction_context.h"

#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // 可能需要
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    TransactionContext::TransactionContext(SessionHandle& session) : owner_session_(session) {
    }

    std::pair<std::pair<boltprotocol::BoltError, std::string>, std::unique_ptr<BoltResultStream>> TransactionContext::run(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        if (!owner_session_.is_in_transaction()) {
            std::string err_msg = "TransactionContext::run called, but SessionHandle is not in an active explicit transaction.";
            boltprotocol::SuccessMessageParams dummy_run_summary_raw;
            auto dummy_field_names = std::make_shared<const std::vector<std::string>>();
            std::vector<boltprotocol::RecordMessageParams> empty_records;

            // 获取创建 BoltResultStream 所需的附加参数
            std::string server_addr = "unknown_server";
            boltprotocol::versions::Version bolt_ver(0, 0);
            bool utc_patch = false;
            if (owner_session_.connection_ && owner_session_.connection_->is_ready_for_queries()) {  // 检查连接有效性
                server_addr = owner_session_.connection_->get_config().target_host + ":" + std::to_string(owner_session_.connection_->get_config().target_port);
                bolt_ver = owner_session_.connection_->get_bolt_version();
                utc_patch = owner_session_.connection_->is_utc_patch_active();
            }

            // 使用更新后的构造函数
            auto failed_stream = std::make_unique<BoltResultStream>(&owner_session_,
                                                                    std::nullopt,
                                                                    std::move(dummy_run_summary_raw),
                                                                    dummy_field_names,
                                                                    std::move(empty_records),
                                                                    false,  // server_might_have_more
                                                                    bolt_ver,
                                                                    utc_patch,
                                                                    server_addr,
                                                                    owner_session_.session_params_.database_name,  // session_params_ 是 SessionHandle 的成员
                                                                    boltprotocol::BoltError::INVALID_ARGUMENT,     // initial_error
                                                                    err_msg,                                       // initial_error_message
                                                                    std::nullopt                                   // initial_failure_details
            );
            // failed_stream->_set_failure_state(boltprotocol::BoltError::INVALID_ARGUMENT, err_msg); // 这行现在由构造函数处理

            return {{boltprotocol::BoltError::INVALID_ARGUMENT, err_msg}, std::move(failed_stream)};
        }
        return owner_session_.run_query(cypher, parameters, std::nullopt);
    }

    // 注意：SessionHandle 中 run_consume 已改为 run_query_and_consume，并且返回 ResultSummary
    // TransactionContext::run_consume 的职责可能需要重新评估。
    // 如果它仍然需要返回原始的 SuccessMessageParams 和 FailureMessageParams，
    // 那么 SessionHandle 可能需要一个保留这种行为的底层方法，或者 TransactionContext 需要从 ResultSummary 中提取这些。
    // 为了简单修复编译错误，假设我们仍然希望 TransactionContext::run_consume 返回原始参数，
    // 这意味着 SessionHandle 需要一个类似 run_query_and_get_raw_summary 的方法，或者我们调整接口。
    //
    // 简单的修复：让 TransactionContext::run_consume 调用 run_query 然后 consume，然后从 ResultSummary 提取原始数据。
    // 但这有点低效。更好的方法是 SessionHandle 提供一个直接返回原始参数的 consume 方法。
    //
    // 假设我们修改 TransactionContext::run_consume 的目标是获取最终的 ResultSummary。
    // 如果要保持原接口，SessionHandle 需要一个不同的方法。
    //
    // 当前修复：修改 TransactionContext::run_consume 以匹配 SessionHandle::run_query_and_consume 的返回类型，
    // 或者，如果必须保持原始签名，则进行适配。
    //
    // **方案1: 修改 TransactionContext::run_consume 签名 (推荐)**
    // std::pair<std::pair<boltprotocol::BoltError, std::string>, ResultSummary>
    // TransactionContext::run_consume(const std::string& cypher,
    //                                 const std::map<std::string, boltprotocol::Value>& parameters) {
    //     if (!owner_session_.is_in_transaction()) {
    //         // ... 构造一个失败的 ResultSummary ...
    //         return {{boltprotocol::BoltError::INVALID_ARGUMENT, "Not in transaction"}, ResultSummary(...)};
    //     }
    //     return owner_session_.run_query_and_consume(cypher, parameters, std::nullopt);
    // }

    // **方案2: 尝试适配现有签名 (更复杂，可能不完全符合预期)**
    std::pair<boltprotocol::BoltError, std::string> TransactionContext::run_consume(const std::string& cypher,
                                                                                    const std::map<std::string, boltprotocol::Value>& parameters,
                                                                                    boltprotocol::SuccessMessageParams& out_summary_raw,  // 注意这是原始参数
                                                                                    boltprotocol::FailureMessageParams& out_failure_raw) {
        if (!owner_session_.is_in_transaction()) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "TransactionContext::run_consume called, but SessionHandle is not in an active explicit transaction."};
        }

        // 调用 SessionHandle 的 run_query_and_consume
        auto [err_pair, result_summary_typed] = owner_session_.run_query_and_consume(cypher, parameters, std::nullopt);

        // 从 ResultSummary 中获取原始参数
        out_summary_raw = result_summary_typed.raw_params();  // 这需要 ResultSummary 暴露 raw_params()
                                                              // 并且需要处理 ResultSummary 失败的情况

        if (err_pair.first != boltprotocol::BoltError::SUCCESS) {
            // 如果操作失败，尝试从 ResultStream 的失败详情中填充 out_failure_raw
            // BoltResultStream *stream_ptr_for_failure = nullptr; // 如何获取？
            // if (stream_ptr_for_failure && stream_ptr_for_failure->has_failed()){
            //    out_failure_raw = stream_ptr_for_failure->get_failure_details();
            // } else {
            out_failure_raw.metadata["message"] = boltprotocol::Value(err_pair.second);
            // }
        } else {
            out_failure_raw.metadata.clear();  // 成功时清除
        }
        return err_pair;
    }

}  // namespace neo4j_bolt_transport