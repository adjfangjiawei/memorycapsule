#include <array>  // For server_response_bytes_read
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cstring>  // For std::memcpy
#include <variant>
#include <vector>  // For handshake_bytes

#include "boltprotocol/handshake.h"  // For build_handshake_request, parse_handshake_response etc.
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // _stage_bolt_handshake (sync) - (保持不变)
        boltprotocol::BoltError BoltPhysicalConnection::_stage_bolt_handshake() {
            InternalState expected_prev_state;
            bool is_ssl = conn_config_.encryption_enabled;

            if (is_ssl) {
                expected_prev_state = InternalState::SSL_HANDSHAKEN;
                if (!ssl_stream_sync_ || !ssl_stream_sync_->lowest_layer().is_open()) {
                    _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not ready for Bolt handshake.");
                    if (logger_) logger_->error("[ConnBoltHS {}] SSL stream not ready for Bolt handshake. State: {}", id_, _get_current_state_as_string());
                    return last_error_code_;
                }
            } else {
                expected_prev_state = InternalState::TCP_CONNECTED;
                if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                    _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, "Plain stream not ready for Bolt handshake.");
                    if (logger_) logger_->error("[ConnBoltHS {}] Plain stream not ready for Bolt handshake. State: {}", id_, _get_current_state_as_string());
                    return last_error_code_;
                }
            }

            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s != expected_prev_state) {
                std::string msg = "Bolt handshake (sync) called in unexpected state: " + _get_current_state_as_string() + ". Expected: " + (is_ssl ? "SSL_HANDSHAKEN" : "TCP_CONNECTED");
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnBoltHS {}] {}", id_, msg);
                return last_error_code_;
            }
            current_state_.store(InternalState::BOLT_HANDSHAKING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnBoltHS {}] Performing (sync) Bolt handshake.", id_);

            std::vector<boltprotocol::versions::Version> proposed_versions;
            if (conn_config_.preferred_bolt_versions.has_value() && !conn_config_.preferred_bolt_versions.value().empty()) {
                proposed_versions = conn_config_.preferred_bolt_versions.value();
            } else {
                proposed_versions = boltprotocol::versions::get_default_proposed_versions();
            }

            if (proposed_versions.empty()) {
                _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_ARGUMENT, "No Bolt versions to propose for handshake.");
                if (logger_) logger_->error("[ConnBoltHS {}] No Bolt versions to propose.", id_);
                return last_error_code_;
            }

            boltprotocol::BoltError err;
            // perform_handshake template will handle if stream is tcp::iostream or ssl::stream
            if (is_ssl) {
                err = boltprotocol::perform_handshake(*ssl_stream_sync_, proposed_versions, negotiated_bolt_version_);
            } else {
                err = boltprotocol::perform_handshake(*plain_iostream_wrapper_, proposed_versions, negotiated_bolt_version_);
            }

            if (err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "Sync Bolt handshake failed: " + error::bolt_error_to_string(err);
                _mark_as_defunct_internal(err, msg);
                if (logger_) logger_->error("[ConnBoltHS {}] {}", id_, msg);
                return last_error_code_;
            }
            if (logger_) logger_->debug("[ConnBoltHS {}] Sync Bolt handshake successful. Negotiated version: {}.{}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor);

            current_state_.store(InternalState::BOLT_HANDSHAKEN, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            return boltprotocol::BoltError::SUCCESS;
        }

        // _stage_bolt_handshake_async (async)
        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_bolt_handshake_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref,
                                                                                                            std::chrono::milliseconds timeout /* timeout not directly used here, but by underlying IO ops */) {
            // ... (state checks and proposed_versions logic remains the same as Batch 11) ...
            bool is_ssl_stream = std::holds_alternative<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>(async_stream_variant_ref);
            InternalState expected_prev_state = is_ssl_stream ? InternalState::SSL_HANDSHAKEN : InternalState::TCP_CONNECTED;
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            bool correct_prev_state = (current_s == expected_prev_state) || (is_ssl_stream && current_s == InternalState::ASYNC_SSL_HANDSHAKING) || (!is_ssl_stream && current_s == InternalState::ASYNC_TCP_CONNECTING);

            if (!correct_prev_state) {
                std::string msg = "Bolt handshake (async) called in unexpected state: " + _get_current_state_as_string() + ". Expected: " + (is_ssl_stream ? "SSL_HANDSHAKEN/ASYNC_SSL_HANDSHAKING" : "TCP_CONNECTED/ASYNC_TCP_CONNECTING");
                mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnBoltHSAsync {}] Performing (async) Bolt handshake. Configured timeout for IO: {}ms", get_id_for_logging(), conn_config_.bolt_handshake_timeout_ms);  // Use configured timeout

            std::vector<boltprotocol::versions::Version> proposed_versions_list;  // Renamed to avoid conflict
            if (conn_config_.preferred_bolt_versions.has_value() && !conn_config_.preferred_bolt_versions.value().empty()) {
                proposed_versions_list = conn_config_.preferred_bolt_versions.value();
            } else {
                proposed_versions_list = boltprotocol::versions::get_default_proposed_versions();
            }

            if (proposed_versions_list.empty()) {
                mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_ARGUMENT, "No Bolt versions to propose for async handshake.");
                if (logger_) logger_->error("[ConnBoltHSAsync {}] No Bolt versions to propose.", get_id_for_logging());
                co_return last_error_code_;
            }

            std::array<uint8_t, boltprotocol::HANDSHAKE_REQUEST_SIZE_BYTES> handshake_request_bytes_arr;  // Use std::array
            boltprotocol::BoltError build_err = boltprotocol::build_handshake_request(proposed_versions_list, handshake_request_bytes_arr);
            if (build_err != boltprotocol::BoltError::SUCCESS) {
                mark_as_defunct_from_async(build_err, "Failed to build async handshake request.");
                if (logger_) logger_->error("[ConnBoltHSAsync {}] Build handshake request failed: {}", get_id_for_logging(), static_cast<int>(build_err));
                co_return last_error_code_;
            }
            // Convert std::array to std::vector for _write_to_active_async_stream
            std::vector<uint8_t> handshake_request_vec(handshake_request_bytes_arr.begin(), handshake_request_bytes_arr.end());

            // Use 'this->' to call the instance member
            boltprotocol::BoltError err = co_await this->_write_to_active_async_stream(async_stream_variant_ref, handshake_request_vec);
            if (err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "Async Bolt handshake: failed to send proposed versions: " + error::bolt_error_to_string(err);
                // _write_to_active_async_stream calls mark_as_defunct_from_async
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            // Use 'this->' to call the instance member
            auto [read_err, negotiated_version_bytes_vec] = co_await this->_read_from_active_async_stream(async_stream_variant_ref, boltprotocol::HANDSHAKE_RESPONSE_SIZE_BYTES);
            if (read_err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "Async Bolt handshake: failed to read negotiated version: " + error::bolt_error_to_string(read_err);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            // Convert std::vector to std::array for parse_handshake_response
            if (negotiated_version_bytes_vec.size() != boltprotocol::HANDSHAKE_RESPONSE_SIZE_BYTES) {
                std::string msg = "Async Bolt handshake: received incorrect size for negotiated version bytes.";
                mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }
            std::array<uint8_t, boltprotocol::HANDSHAKE_RESPONSE_SIZE_BYTES> negotiated_version_bytes_arr;
            std::memcpy(negotiated_version_bytes_arr.data(), negotiated_version_bytes_vec.data(), boltprotocol::HANDSHAKE_RESPONSE_SIZE_BYTES);

            boltprotocol::BoltError parse_err = boltprotocol::parse_handshake_response(negotiated_version_bytes_arr, negotiated_bolt_version_);  // Store in this->negotiated_bolt_version_
            if (parse_err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "Async Bolt handshake: failed to parse server response: " + error::bolt_error_to_string(parse_err);
                mark_as_defunct_from_async(parse_err, msg);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            if (logger_) logger_->debug("[ConnBoltHSAsync {}] Async Bolt handshake successful. Negotiated version: {}.{}", get_id_for_logging(), (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor);
            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKEN);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;  // Clear previous errors from this instance
            last_error_message_.clear();
            co_return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport