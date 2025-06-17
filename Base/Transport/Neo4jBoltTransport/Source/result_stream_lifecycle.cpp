#include <iostream>
#include <utility>  // For std::move

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    BoltResultStream::BoltResultStream(SessionHandle* session,
                                       std::optional<int64_t> query_id_for_streaming,
                                       boltprotocol::SuccessMessageParams run_summary_params_raw,
                                       std::shared_ptr<const std::vector<std::string>> field_names_ptr,
                                       std::vector<boltprotocol::RecordMessageParams> initial_records,
                                       bool server_might_have_more,
                                       const boltprotocol::versions::Version& bolt_version,
                                       bool utc_patch_active,
                                       const std::string& server_address_for_summary,
                                       const std::optional<std::string>& database_name_for_summary,
                                       boltprotocol::BoltError initial_error,
                                       const std::string& initial_error_message,
                                       const std::optional<boltprotocol::FailureMessageParams>& initial_failure_details)
        : owner_session_(session),
          query_id_(query_id_for_streaming),
          field_names_ptr_cache_(std::move(field_names_ptr)),
          // Initialize run_summary_typed_ with a copy of raw params for now
          run_summary_typed_(boltprotocol::SuccessMessageParams(run_summary_params_raw), bolt_version, utc_patch_active, server_address_for_summary, database_name_for_summary),
          // final_summary_typed_ initialized similarly, will be updated by _update_final_summary
          final_summary_typed_(boltprotocol::SuccessMessageParams(run_summary_params_raw), bolt_version, utc_patch_active, server_address_for_summary, database_name_for_summary),
          initial_server_has_more_records_(server_might_have_more),
          server_has_more_records_(server_might_have_more),
          bolt_version_cache_(bolt_version),
          utc_patch_active_cache_(utc_patch_active),
          server_address_cache_(server_address_for_summary),
          database_name_cache_(database_name_for_summary) {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;

        for (auto&& rec_param : initial_records) {
            raw_record_buffer_.push_back(std::move(rec_param));
        }

        if (initial_error != boltprotocol::BoltError::SUCCESS) {
            _set_failure_state(initial_error, initial_error_message, initial_failure_details);
        } else if (!owner_session_ || !owner_session_->is_connection_valid()) {
            _set_failure_state(boltprotocol::BoltError::NETWORK_ERROR, "Session or connection invalid at ResultStream creation.");
        }

        // Ensure field_names_ptr_cache_ is populated from run_summary_typed_ if it was null
        if (!field_names_ptr_cache_ || field_names_ptr_cache_->empty()) {
            auto it_fields = run_summary_typed_.raw_params().metadata.find("fields");
            if (it_fields != run_summary_typed_.raw_params().metadata.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)) {
                auto temp_field_names = std::make_shared<std::vector<std::string>>();
                const auto& list_ptr = std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second);
                if (list_ptr) {
                    temp_field_names->reserve(list_ptr->elements.size());
                    for (const auto& field_val : list_ptr->elements) {
                        if (std::holds_alternative<std::string>(field_val)) {
                            temp_field_names->push_back(std::get<std::string>(field_val));
                        }
                    }
                }
                field_names_ptr_cache_ = std::const_pointer_cast<const std::vector<std::string>>(temp_field_names);
            } else {
                field_names_ptr_cache_ = std::make_shared<const std::vector<std::string>>();  // Empty
            }
        }

        if (!stream_failed_) {
            // If no records were pipelined and server RUN summary says no more, then stream is done.
            if (raw_record_buffer_.empty() && !initial_server_has_more_records_) {
                stream_fully_consumed_or_discarded_ = true;
                // final_summary_typed_ is already a copy of run_summary_typed_ here
            }
        }

        // A PULL/DISCARD is needed if: buffer is empty AND server might have more records (initial_server_has_more_records_) AND not failed
        is_first_pull_attempt_ = raw_record_buffer_.empty() && initial_server_has_more_records_ && !stream_failed_;

        if (logger) {
            logger->debug("[ResultStreamLC {}] Created. QID: {}. InitRecs: {}. InitialSrvMore: {}. Failed: {}. FirstPullAttempt: {}", (void*)this, query_id_ ? std::to_string(*query_id_) : "N/A", raw_record_buffer_.size(), initial_server_has_more_records_, stream_failed_, is_first_pull_attempt_);
        }
    }

    BoltResultStream::~BoltResultStream() {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) {
            logger->debug("[ResultStreamLC {}] Destructing. Consumed: {}, Failed: {}", (void*)this, stream_fully_consumed_or_discarded_, stream_failed_);
        }

        if (!stream_fully_consumed_or_discarded_ && !stream_failed_ && owner_session_ && owner_session_->is_connection_valid()) {
            if (logger) logger->trace("[ResultStreamLC {}] Auto-discarding in dtor.", (void*)this);
            // This will update final_summary_typed_
            auto discard_res = _discard_all_remaining_records();
            if (discard_res.first != boltprotocol::BoltError::SUCCESS && logger) {
                logger->warn("[ResultStreamLC {}] Auto-discard in dtor failed: {}", (void*)this, discard_res.second);
            }
        }
    }

    BoltResultStream::BoltResultStream(BoltResultStream&& other) noexcept
        : owner_session_(other.owner_session_),
          query_id_(other.query_id_),
          raw_record_buffer_(std::move(other.raw_record_buffer_)),
          field_names_ptr_cache_(std::move(other.field_names_ptr_cache_)),
          run_summary_typed_(std::move(other.run_summary_typed_)),
          final_summary_typed_(std::move(other.final_summary_typed_)),
          failure_details_raw_(std::move(other.failure_details_raw_)),
          server_has_more_records_(other.server_has_more_records_),
          initial_server_has_more_records_(other.initial_server_has_more_records_),
          stream_fully_consumed_or_discarded_(other.stream_fully_consumed_or_discarded_),
          stream_failed_(other.stream_failed_),
          failure_reason_(other.failure_reason_),
          failure_message_(std::move(other.failure_message_)),
          is_first_pull_attempt_(other.is_first_pull_attempt_),
          bolt_version_cache_(other.bolt_version_cache_),
          utc_patch_active_cache_(other.utc_patch_active_cache_),
          server_address_cache_(std::move(other.server_address_cache_)),
          database_name_cache_(std::move(other.database_name_cache_)) {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) logger->trace("[ResultStreamLC {}] Move constructed from ResultStream {}.", (void*)this, (void*)&other);

        other.owner_session_ = nullptr;  // Invalidate other
        other.stream_fully_consumed_or_discarded_ = true;
        other.stream_failed_ = true;  // Mark other as unusable
    }

    BoltResultStream& BoltResultStream::operator=(BoltResultStream&& other) noexcept {
        if (this != &other) {
            std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
            if (logger) logger->trace("[ResultStreamLC {}] Move assigning from ResultStream {}.", (void*)this, (void*)&other);

            // Discard current stream's resources if it's still active
            if (!stream_fully_consumed_or_discarded_ && !stream_failed_ && owner_session_ && owner_session_->is_connection_valid()) {
                _discard_all_remaining_records();
            }

            owner_session_ = other.owner_session_;
            query_id_ = other.query_id_;
            raw_record_buffer_ = std::move(other.raw_record_buffer_);
            field_names_ptr_cache_ = std::move(other.field_names_ptr_cache_);
            run_summary_typed_ = std::move(other.run_summary_typed_);
            final_summary_typed_ = std::move(other.final_summary_typed_);
            failure_details_raw_ = std::move(other.failure_details_raw_);
            server_has_more_records_ = other.server_has_more_records_;
            initial_server_has_more_records_ = other.initial_server_has_more_records_;
            stream_fully_consumed_or_discarded_ = other.stream_fully_consumed_or_discarded_;
            stream_failed_ = other.stream_failed_;
            failure_reason_ = other.failure_reason_;
            failure_message_ = std::move(other.failure_message_);
            is_first_pull_attempt_ = other.is_first_pull_attempt_;
            bolt_version_cache_ = other.bolt_version_cache_;
            utc_patch_active_cache_ = other.utc_patch_active_cache_;
            server_address_cache_ = std::move(other.server_address_cache_);
            database_name_cache_ = std::move(other.database_name_cache_);

            other.owner_session_ = nullptr;  // Invalidate other
            other.stream_fully_consumed_or_discarded_ = true;
            other.stream_failed_ = true;
        }
        return *this;
    }

    const std::vector<std::string>& BoltResultStream::field_names() const {
        static const std::vector<std::string> empty_names_singleton;
        return field_names_ptr_cache_ ? *field_names_ptr_cache_ : empty_names_singleton;
    }

    void BoltResultStream::_set_failure_state(boltprotocol::BoltError reason, std::string detailed_message, const std::optional<boltprotocol::FailureMessageParams>& details) {
        if (stream_failed_ && failure_reason_ != boltprotocol::BoltError::SUCCESS) {
            // Already in a more specific failure state, don't override with a potentially less specific one unless reason is new.
            // However, allow updating message if new details are provided.
            if (!detailed_message.empty() && failure_message_.find(detailed_message) == std::string::npos) {
                failure_message_ += "; Additional detail: " + detailed_message;
            }
            if (details.has_value() && failure_details_raw_.metadata.empty()) {  // Only update raw details if not already set
                failure_details_raw_ = *details;
            }
            return;
        }
        stream_failed_ = true;
        failure_reason_ = reason;
        failure_message_ = std::move(detailed_message);

        if (details.has_value()) {
            failure_details_raw_ = *details;
        } else {
            failure_details_raw_.metadata.clear();  // Ensure it's clear
            if (!failure_message_.empty() && reason != boltprotocol::BoltError::SUCCESS) {
                // Create a minimal failure detail from the message
                failure_details_raw_.metadata["message"] = boltprotocol::Value(failure_message_);
            }
        }
        stream_fully_consumed_or_discarded_ = true;  // A failed stream is considered consumed

        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) {
            logger->warn("[ResultStreamLC {}] Failure state set. Reason: {} ({}), Msg: {}", (void*)this, static_cast<int>(reason), error::bolt_error_to_string(reason), failure_message_);
        }
    }

    // New private helper to update final_summary_typed_
    void BoltResultStream::_update_final_summary(boltprotocol::SuccessMessageParams&& pull_or_discard_raw_summary) {
        final_summary_typed_ = ResultSummary(std::move(pull_or_discard_raw_summary), bolt_version_cache_, utc_patch_active_cache_, server_address_cache_, database_name_cache_);
    }

}  // namespace neo4j_bolt_transport