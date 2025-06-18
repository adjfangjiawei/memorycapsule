#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/async_result_stream.h"
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/bolt_record.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // _prepare_run_message_params (保持不变)
    // ...

    // run_query_async (MODIFIED to call _update_bookmarks_from_summary)
    boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> AsyncSessionHandle::run_query_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }
        ResultSummary default_summary_on_error({},
                                               stream_context_ ? stream_context_->negotiated_bolt_version : boltprotocol::versions::Version(0, 0),
                                               stream_context_ ? stream_context_->utc_patch_active : false,
                                               stream_context_ ? (stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port)) : "unknown_async_run",
                                               session_params_.database_name);

        if (!is_valid() || !stream_context_) {
            std::string err_msg = "AsyncSessionHandle::run_query_async called on invalid or closed session.";
            if (logger) logger->warn("[AsyncSessionExec] {}", err_msg);
            co_return std::make_pair(boltprotocol::BoltError::NETWORK_ERROR, std::move(default_summary_on_error));
        }
        if (in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionExec] run_query_async (auto-commit) called while in an explicit transaction. Use run_query_in_transaction_async instead.");
            co_return std::make_pair(boltprotocol::BoltError::INVALID_ARGUMENT, std::move(default_summary_on_error));
        }

        if (logger) logger->debug("[AsyncSessionExec] run_query_async: Cypher: {:.50}...", cypher);

        boltprotocol::RunMessageParams run_params = _prepare_run_message_params(cypher, parameters, false);

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_run_message(run_params, run_writer, stream_context_->negotiated_bolt_version);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize RUN message: " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionExec] {}", last_error_message_);
            co_return std::make_pair(last_error_code_, std::move(default_summary_on_error));
        }
        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionExec:StaticOpErrHandler] Error: {} - {}", static_cast<int>(reason), message);
        };

        auto [run_summary_err, run_result_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, run_payload_bytes, stream_context_->original_config, logger, static_op_error_handler);

        if (run_summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return std::make_pair(last_error_code_, std::move(run_result_summary_obj));
        }

        boltprotocol::SuccessMessageParams final_pull_success_meta = run_result_summary_obj.raw_params();
        bool server_has_more = true;
        auto it_run_has_more = run_result_summary_obj.raw_params().metadata.find("has_more");
        if (it_run_has_more != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<bool>(it_run_has_more->second)) {
            server_has_more = std::get<bool>(it_run_has_more->second);
        } else {
            auto it_fields = run_result_summary_obj.raw_params().metadata.find("fields");
            if (it_fields == run_result_summary_obj.raw_params().metadata.end() ||
                (std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second) && std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second) && std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)->elements.empty())) {
                server_has_more = false;
            }
        }
        std::optional<int64_t> qid_for_pull;
        auto it_qid = run_result_summary_obj.raw_params().metadata.find("qid");
        if (it_qid != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
            qid_for_pull = std::get<int64_t>(it_qid->second);
        }

        if (server_has_more) {
            boltprotocol::PullMessageParams pull_params;
            pull_params.n = -1;
            if (qid_for_pull.has_value() && (stream_context_->negotiated_bolt_version.major >= 4)) {
                pull_params.qid = qid_for_pull;
            }
            std::vector<uint8_t> pull_payload_bytes;
            boltprotocol::PackStreamWriter pull_writer(pull_payload_bytes);
            serialize_err = boltprotocol::serialize_pull_message(pull_params, pull_writer);
            if (serialize_err != boltprotocol::BoltError::SUCCESS) {
                last_error_code_ = serialize_err;
                last_error_message_ = "Failed to serialize PULL message: " + error::bolt_error_to_string(serialize_err);
                if (logger) logger->error("[AsyncSessionExec] {}", last_error_message_);
                co_return std::make_pair(
                    last_error_code_,
                    ResultSummary(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
            }

            auto [pull_summary_err, pull_result_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, pull_payload_bytes, stream_context_->original_config, logger, static_op_error_handler);

            if (pull_summary_err != boltprotocol::BoltError::SUCCESS) {
                co_return std::make_pair(last_error_code_, std::move(pull_result_summary_obj));
            }
            final_pull_success_meta = pull_result_summary_obj.raw_params();
        }

        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        _update_bookmarks_from_summary(final_pull_success_meta);  // MODIFIED: Update bookmarks
        if (logger) logger->info("[AsyncSessionExec] run_query_async (auto-commit) completed. Last bookmarks: {}", current_bookmarks_.empty() ? "<none>" : (current_bookmarks_.size() == 1 ? current_bookmarks_[0] : std::to_string(current_bookmarks_.size()) + " items"));

        ResultSummary final_summary_obj(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name);
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(final_summary_obj));
    }

    // run_query_stream_async (MODIFIED to pass is_auto_commit)
    boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<AsyncResultStream>>> AsyncSessionHandle::run_query_stream_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            std::string err_msg = "AsyncSessionHandle::run_query_stream_async called on invalid or closed session.";
            if (logger) logger->warn("[AsyncSessionExecStream] {}", err_msg);
            co_return std::make_pair(boltprotocol::BoltError::NETWORK_ERROR, nullptr);
        }
        if (close_initiated_.load(std::memory_order_acquire)) {
            std::string err_msg = "AsyncSessionHandle::run_query_stream_async called after close_async initiated.";
            if (logger) logger->warn("[AsyncSessionExecStream] {}", err_msg);
            co_return std::make_pair(boltprotocol::BoltError::INVALID_ARGUMENT, nullptr);
        }
        if (in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionExecStream] run_query_stream_async (auto-commit) called while in an explicit transaction.");
            co_return std::make_pair(boltprotocol::BoltError::INVALID_ARGUMENT, nullptr);
        }

        if (logger) logger->debug("[AsyncSessionExecStream] run_query_stream_async: Cypher: {:.50}...", cypher);

        boltprotocol::RunMessageParams run_params = _prepare_run_message_params(cypher, parameters, false);
        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_run_message(run_params, run_writer, stream_context_->negotiated_bolt_version);

        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize RUN message (stream): " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionExecStream] {}", last_error_message_);
            co_return std::make_pair(last_error_code_, nullptr);
        }

        auto static_op_error_handler_for_stream = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionExecStream:StaticOpErrHandler] Error: {} - {}", static_cast<int>(reason), message);
        };

        auto [run_summary_err, run_result_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, run_payload_bytes, stream_context_->original_config, logger, static_op_error_handler_for_stream);

        if (run_summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return std::make_pair(last_error_code_, nullptr);
        }

        std::shared_ptr<std::vector<std::string>> fields_ptr = std::make_shared<std::vector<std::string>>();
        auto it_fields = run_result_summary_obj.raw_params().metadata.find("fields");
        if (it_fields != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)) {
            const auto& list_ptr = std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second);
            if (list_ptr) {
                fields_ptr->reserve(list_ptr->elements.size());
                for (const auto& field_val : list_ptr->elements) {
                    if (std::holds_alternative<std::string>(field_val)) {
                        fields_ptr->push_back(std::get<std::string>(field_val));
                    }
                }
            }
        }

        std::optional<int64_t> qid_for_stream;
        auto it_qid = run_result_summary_obj.raw_params().metadata.find("qid");
        if (it_qid != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
            qid_for_stream = std::get<int64_t>(it_qid->second);
        }

        bool server_had_more_after_run = true;
        auto it_run_has_more = run_result_summary_obj.raw_params().metadata.find("has_more");
        if (it_run_has_more != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<bool>(it_run_has_more->second)) {
            server_had_more_after_run = std::get<bool>(it_run_has_more->second);
        } else {
            if (fields_ptr->empty()) {
                server_had_more_after_run = false;
            }
        }

        boltprotocol::SuccessMessageParams run_summary_params_copy = run_result_summary_obj.raw_params();

        std::unique_ptr<internal::ActiveAsyncStreamContext> owned_stream_ctx = std::move(stream_context_);
        if (!owned_stream_ctx) {
            if (logger) logger->error("[AsyncSessionExecStream] Stream context became null before creating AsyncResultStream.");
            co_return std::make_pair(boltprotocol::BoltError::UNKNOWN_ERROR, nullptr);
        }

        std::unique_ptr<AsyncResultStream> result_stream = std::make_unique<AsyncResultStream>(this,
                                                                                               std::move(owned_stream_ctx),
                                                                                               qid_for_stream,
                                                                                               std::move(run_summary_params_copy),
                                                                                               fields_ptr,
                                                                                               std::vector<boltprotocol::RecordMessageParams>{},
                                                                                               server_had_more_after_run,
                                                                                               this->session_params_,
                                                                                               true  // MODIFIED: This is an auto-commit query, so is_auto_commit is true
        );

        if (logger) logger->info("[AsyncSessionExecStream] AsyncResultStream created. QID: {}. Fields: {}", qid_for_stream.has_value() ? std::to_string(qid_for_stream.value()) : "N/A", fields_ptr->size());
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(result_stream));
    }

}  // namespace neo4j_bolt_transport