#include <boost/asio/detached.hpp>

#include "neo4j_bolt_transport/async_result_stream.h"
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

#ifdef __has_include
#if __has_include(<openssl/ssl.h>)
#include <openssl/ssl.h>
#define HAS_OPENSSL_SSL_H_ASYNC_RESULT_STREAM_LC
#endif
#endif

namespace neo4j_bolt_transport {

    AsyncResultStream::AsyncResultStream(AsyncSessionHandle* owner_session,
                                         std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx,
                                         std::optional<int64_t> query_id,
                                         boltprotocol::SuccessMessageParams run_summary_params_raw,
                                         std::shared_ptr<const std::vector<std::string>> field_names,
                                         std::vector<boltprotocol::RecordMessageParams> initial_records_raw,
                                         bool server_had_more_after_run,
                                         const config::SessionParameters& session_config,
                                         bool is_auto_commit  // Initialize new member
                                         )
        : owner_session_(owner_session),
          stream_context_(std::move(stream_ctx)),
          query_id_(query_id),
          session_config_cache_(session_config),
          is_auto_commit_(is_auto_commit),  // Store if it's auto-commit
          field_names_ptr_cache_(std::move(field_names)),
          run_summary_typed_(boltprotocol::SuccessMessageParams(run_summary_params_raw),
                             stream_context_ ? stream_context_->negotiated_bolt_version : boltprotocol::versions::Version(0, 0),
                             stream_context_ ? stream_context_->utc_patch_active : false,
                             stream_context_ ? stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port) : "unknown_ars_run",
                             session_config_cache_.database_name),
          final_summary_typed_(std::move(run_summary_params_raw),
                               stream_context_ ? stream_context_->negotiated_bolt_version : boltprotocol::versions::Version(0, 0),
                               stream_context_ ? stream_context_->utc_patch_active : false,
                               stream_context_ ? stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port) : "unknown_ars_final",
                               session_config_cache_.database_name),
          server_has_more_records_after_last_pull_(server_had_more_after_run),
          initial_server_has_more_after_run_(server_had_more_after_run),
          stream_fully_consumed_or_discarded_(false),
          stream_failed_(false),
          failure_reason_(boltprotocol::BoltError::SUCCESS) {
        for (auto&& rec_param : initial_records_raw) {
            raw_record_buffer_.push_back(std::move(rec_param));
        }

        if (!stream_context_ || !std::visit(
                                    [](auto& s_ref) {
                                        return s_ref.lowest_layer().is_open();
                                    },
                                    stream_context_->stream)) {
            set_failure_state(boltprotocol::BoltError::NETWORK_ERROR, "AsyncResultStream created with invalid or closed stream context.");
        } else {
            if (raw_record_buffer_.empty() && !initial_server_has_more_after_run_) {
                stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);
                try_update_session_bookmarks_on_stream_end();  // Attempt bookmark update if stream ends at creation
            }
        }
        is_first_fetch_attempt_ = raw_record_buffer_.empty() && initial_server_has_more_after_run_ && !stream_failed_.load(std::memory_order_acquire);

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }
        if (logger) {
            logger->debug("[AsyncResultStreamLC {}] Created. QID: {}. InitRecs: {}. InitialSrvMore: {}. AutoCommit: {}. Failed: {}. FirstFetch: {}",
                          (void*)this,
                          query_id_ ? std::to_string(*query_id_) : "N/A",
                          raw_record_buffer_.size(),
                          initial_server_has_more_after_run_,
                          is_auto_commit_,
                          stream_failed_.load(std::memory_order_acquire),
                          is_first_fetch_attempt_);
        }
    }

    // ... (析构函数, 移动构造/赋值, is_open, field_names, set_failure_state, update_final_summary 保持不变) ...
    AsyncResultStream::~AsyncResultStream() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }

        if (logger) {
            logger->debug("[AsyncResultStreamLC {}] Destructing. Consumed: {}, Failed: {}", (void*)this, stream_fully_consumed_or_discarded_.load(std::memory_order_acquire), stream_failed_.load(std::memory_order_acquire));
        }

        if (stream_context_ && !stream_fully_consumed_or_discarded_.load(std::memory_order_acquire) && !stream_failed_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncResultStreamLC {}] Destructed without full consumption/discard. Stream context will be closed abruptly if not already consumed by a co_await consume_async().", (void*)this);
            // To truly discard, consume_async should be co_awaited.
            // Destructor cannot co_await. Best effort is to close the socket.
        }
        if (stream_context_) {
            std::visit(
                [logger_copy = logger, this_ptr = (void*)this](auto& s_variant_ref) {
                    boost::system::error_code ec_shutdown, ec_close;
                    if (s_variant_ref.lowest_layer().is_open()) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
#ifdef HAS_OPENSSL_SSL_H_ASYNC_RESULT_STREAM_LC
                            if (!(SSL_get_shutdown(s_variant_ref.native_handle()) & (SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN))) {
                                s_variant_ref.async_shutdown(boost::asio::detached);
                                if (logger_copy) logger_copy->trace("[AsyncResultStreamLC {}] Destructor: Initiated async SSL shutdown.", this_ptr);
                            }
#else
                            if (logger_copy) logger_copy->warn("[AsyncResultStreamLC {}] Destructor: OpenSSL headers not detected for SSL_get_shutdown check. Proceeding with socket shutdown only.", this_ptr);
#endif
                        }
                        s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec_shutdown);
                        s_variant_ref.lowest_layer().close(ec_close);
                        if (logger_copy && (ec_shutdown || ec_close)) {
                            logger_copy->trace("[AsyncResultStreamLC {}] Destructor: Socket shutdown/close errors: SD={}, CL={}", this_ptr, ec_shutdown.message(), ec_close.message());
                        }
                    }
                },
                stream_context_->stream);
            stream_context_.reset();
        }
    }

    AsyncResultStream::AsyncResultStream(AsyncResultStream&& other) noexcept
        : owner_session_(other.owner_session_),
          stream_context_(std::move(other.stream_context_)),
          query_id_(other.query_id_),
          session_config_cache_(std::move(other.session_config_cache_)),
          is_auto_commit_(other.is_auto_commit_),
          raw_record_buffer_(std::move(other.raw_record_buffer_)),
          field_names_ptr_cache_(std::move(other.field_names_ptr_cache_)),
          run_summary_typed_(std::move(other.run_summary_typed_)),
          final_summary_typed_(std::move(other.final_summary_typed_)),
          server_has_more_records_after_last_pull_(other.server_has_more_records_after_last_pull_.load(std::memory_order_acquire)),
          initial_server_has_more_after_run_(other.initial_server_has_more_after_run_),
          stream_fully_consumed_or_discarded_(other.stream_fully_consumed_or_discarded_.load(std::memory_order_acquire)),
          stream_failed_(other.stream_failed_.load(std::memory_order_acquire)),
          failure_reason_(other.failure_reason_.load(std::memory_order_acquire)),
          failure_message_(std::move(other.failure_message_)),
          is_first_fetch_attempt_(other.is_first_fetch_attempt_) {
        other.owner_session_ = nullptr;
        other.stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);
        other.stream_failed_.store(true, std::memory_order_release);
    }

    AsyncResultStream& AsyncResultStream::operator=(AsyncResultStream&& other) noexcept {
        if (this != &other) {
            if (stream_context_ && !stream_fully_consumed_or_discarded_.load(std::memory_order_acquire) && !stream_failed_.load(std::memory_order_acquire)) {
                std::visit(
                    [](auto& s_variant_ref) {
                        boost::system::error_code ec;
                        if (s_variant_ref.lowest_layer().is_open()) {
                            if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
#ifdef HAS_OPENSSL_SSL_H_ASYNC_RESULT_STREAM_LC
                                if (!(SSL_get_shutdown(s_variant_ref.native_handle()) & (SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN))) {
                                    s_variant_ref.async_shutdown(boost::asio::detached);
                                }
#endif
                            }
                            s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                            s_variant_ref.lowest_layer().close(ec);
                        }
                    },
                    stream_context_->stream);
            }
            stream_context_.reset();

            owner_session_ = other.owner_session_;
            stream_context_ = std::move(other.stream_context_);
            query_id_ = other.query_id_;
            session_config_cache_ = std::move(other.session_config_cache_);
            is_auto_commit_ = other.is_auto_commit_;
            raw_record_buffer_ = std::move(other.raw_record_buffer_);
            field_names_ptr_cache_ = std::move(other.field_names_ptr_cache_);
            run_summary_typed_ = std::move(other.run_summary_typed_);
            final_summary_typed_ = std::move(other.final_summary_typed_);
            server_has_more_records_after_last_pull_.store(other.server_has_more_records_after_last_pull_.load(std::memory_order_acquire), std::memory_order_release);
            initial_server_has_more_after_run_ = other.initial_server_has_more_after_run_;
            stream_fully_consumed_or_discarded_.store(other.stream_fully_consumed_or_discarded_.load(std::memory_order_acquire), std::memory_order_release);
            stream_failed_.store(other.stream_failed_.load(std::memory_order_acquire), std::memory_order_release);
            failure_reason_.store(other.failure_reason_.load(std::memory_order_acquire), std::memory_order_release);
            failure_message_ = std::move(other.failure_message_);
            is_first_fetch_attempt_ = other.is_first_fetch_attempt_;

            other.owner_session_ = nullptr;
            other.stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);
            other.stream_failed_.store(true, std::memory_order_release);
        }
        return *this;
    }

    bool AsyncResultStream::is_open() const {
        return stream_context_ != nullptr &&
               std::visit(
                   [](const auto& s_ref) {
                       return s_ref.lowest_layer().is_open();
                   },
                   stream_context_->stream) &&
               !stream_failed_.load(std::memory_order_acquire) && !stream_fully_consumed_or_discarded_.load(std::memory_order_acquire);
    }

    const std::vector<std::string>& AsyncResultStream::field_names() const {
        static const std::vector<std::string> empty_names_singleton;
        return field_names_ptr_cache_ ? *field_names_ptr_cache_ : empty_names_singleton;
    }

    void AsyncResultStream::set_failure_state(boltprotocol::BoltError reason, std::string detailed_message, const std::optional<boltprotocol::FailureMessageParams>& details) {
        if (stream_failed_.load(std::memory_order_acquire) && failure_reason_.load(std::memory_order_acquire) != boltprotocol::BoltError::SUCCESS) {
            if (!detailed_message.empty() && failure_message_.find(detailed_message) == std::string::npos) {
                failure_message_ += "; Additional detail: " + detailed_message;
            }
            return;
        }
        stream_failed_.store(true, std::memory_order_release);
        failure_reason_.store(reason, std::memory_order_release);
        failure_message_ = std::move(detailed_message);
        stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }
        if (logger) {
            logger->warn("[AsyncResultStream {}] Failure state set. Reason: {} ({}), Msg: {}", (void*)this, static_cast<int>(reason), error::bolt_error_to_string(reason), failure_message_);
        }
    }

    void AsyncResultStream::update_final_summary(boltprotocol::SuccessMessageParams&& pull_or_discard_raw_summary) {
        if (stream_context_) {
            final_summary_typed_ =
                ResultSummary(std::move(pull_or_discard_raw_summary), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_config_cache_.database_name);
        } else {
            std::shared_ptr<spdlog::logger> logger = nullptr;
            if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
                logger = owner_session_->transport_manager_->get_config().logger;
            }
            if (logger) logger->error("[AsyncResultStream {}] Cannot update final summary: stream_context_ is null.", (void*)this);
        }
    }

    void AsyncResultStream::try_update_session_bookmarks_on_stream_end() {
        if (is_auto_commit_ && owner_session_ && !stream_failed_.load(std::memory_order_acquire)) {
            // We use final_summary_typed_ as it contains the summary from the *last*
            // successful server interaction (PULL or DISCARD, or RUN if no PULL/DISCARD was needed).
            owner_session_->_update_bookmarks_from_summary(final_summary_typed_.raw_params());
        }
    }

}  // namespace neo4j_bolt_transport