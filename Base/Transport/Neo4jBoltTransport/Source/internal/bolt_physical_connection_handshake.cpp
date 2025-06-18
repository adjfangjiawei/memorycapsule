#include <variant>

#include "boltprotocol/handshake.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

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

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_bolt_handshake_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref, std::chrono::milliseconds timeout) {
            bool is_ssl_stream = std::holds_alternative<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>(async_stream_variant_ref);
            InternalState expected_prev_state = is_ssl_stream ? InternalState::SSL_HANDSHAKEN : InternalState::TCP_CONNECTED;

            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            // Allow if just finished async SSL HS or async TCP connect, or if already in correct pre-state
            bool correct_prev_state = (current_s == expected_prev_state) || (is_ssl_stream && current_s == InternalState::ASYNC_SSL_HANDSHAKING) || (!is_ssl_stream && current_s == InternalState::ASYNC_TCP_CONNECTING);

            if (!correct_prev_state) {
                std::string msg = "Bolt handshake (async) called in unexpected state: " + _get_current_state_as_string() + ". Expected: " + (is_ssl_stream ? "SSL_HANDSHAKEN/ASYNC_SSL_HANDSHAKING" : "TCP_CONNECTED/ASYNC_TCP_CONNECTING");
                mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnBoltHSAsync {}] Performing (async) Bolt handshake. Timeout: {}ms", get_id_for_logging(), timeout.count());

            std::vector<boltprotocol::versions::Version> proposed_versions;
            if (conn_config_.preferred_bolt_versions.has_value() && !conn_config_.preferred_bolt_versions.value().empty()) {
                proposed_versions = conn_config_.preferred_bolt_versions.value();
            } else {
                proposed_versions = boltprotocol::versions::get_default_proposed_versions();
            }

            if (proposed_versions.empty()) {
                mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_ARGUMENT, "No Bolt versions to propose for async handshake.");
                if (logger_) logger_->error("[ConnBoltHSAsync {}] No Bolt versions to propose.", get_id_for_logging());
                co_return last_error_code_;
            }

            std::vector<uint8_t> handshake_bytes;
            handshake_bytes.push_back(0x60);
            handshake_bytes.push_back(0x60);
            handshake_bytes.push_back(0xB0);
            handshake_bytes.push_back(0x17);
            for (const auto& version : proposed_versions) {
                handshake_bytes.push_back(version.minor);
                handshake_bytes.push_back(version.major);
                handshake_bytes.push_back(0);
                handshake_bytes.push_back(0);
            }
            while (handshake_bytes.size() < 20) {
                handshake_bytes.push_back(0);
            }

            boltprotocol::BoltError err = co_await _write_to_active_async_stream(async_stream_variant_ref, handshake_bytes);
            if (err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "Async Bolt handshake: failed to send proposed versions: " + error::bolt_error_to_string(err);
                // _write_to_active_async_stream calls mark_as_defunct_from_async
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            auto [read_err, negotiated_version_bytes] = co_await _read_from_active_async_stream(async_stream_variant_ref, 4);
            if (read_err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "Async Bolt handshake: failed to read negotiated version: " + error::bolt_error_to_string(read_err);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            if (negotiated_version_bytes.size() != 4) {
                std::string msg = "Async Bolt handshake: received incorrect size for negotiated version.";
                mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            // Assuming Version struct does not have patch member, as per previous clarification.
            negotiated_bolt_version_.minor = negotiated_version_bytes[1];
            negotiated_bolt_version_.major = negotiated_version_bytes[0];
            // negotiated_bolt_version_.patch = 0; // No patch field

            bool version_supported = false;
            for (const auto& proposed : proposed_versions) {
                if (proposed.major == negotiated_bolt_version_.major && proposed.minor == negotiated_bolt_version_.minor) {
                    version_supported = true;
                    break;
                }
            }

            if (negotiated_bolt_version_.major == 0 && negotiated_bolt_version_.minor == 0) {
                std::string msg = "Async Bolt handshake: Server rejected all proposed Bolt versions (responded with 0.0).";
                mark_as_defunct_from_async(boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION, msg);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            if (!version_supported) {
                std::string msg = "Async Bolt handshake: Server chose an unsupported Bolt version: " + negotiated_bolt_version_.to_string();
                mark_as_defunct_from_async(boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION, msg);
                if (logger_) logger_->error("[ConnBoltHSAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            if (logger_) logger_->debug("[ConnBoltHSAsync {}] Async Bolt handshake successful. Negotiated version: {}.{}", get_id_for_logging(), (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor);
            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKEN);  // Corrected enum member was added
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            co_return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport