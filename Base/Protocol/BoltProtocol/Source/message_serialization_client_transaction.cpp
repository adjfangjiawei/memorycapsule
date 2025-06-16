#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "boltprotocol/message_defs.h"  // Includes bolt_errors_versions.h for versions::Version
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_begin_message(const BeginMessageParams& params, PackStreamWriter& writer, const versions::Version& target_bolt_version) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure begin_struct_obj;
        begin_struct_obj.tag = static_cast<uint8_t>(MessageTag::BEGIN);

        std::shared_ptr<BoltMap> extra_map_sptr;
        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            auto& extra_pairs = extra_map_sptr->pairs;

            // Populate extra_pairs from params based on target_bolt_version
            // BEGIN shares many 'extra' fields with RUN's auto-commit extras
            if (target_bolt_version.major >= 3) {  // Bookmarks, tx_timeout, tx_metadata, mode introduced in Bolt 3
                if (params.bookmarks.has_value() && !params.bookmarks.value().empty()) {
                    auto bookmarks_list_sptr = std::make_shared<BoltList>();
                    for (const auto& bm : params.bookmarks.value()) {
                        bookmarks_list_sptr->elements.emplace_back(Value(bm));
                    }
                    extra_pairs.emplace("bookmarks", Value(bookmarks_list_sptr));
                }
                if (params.tx_timeout.has_value()) {
                    extra_pairs.emplace("tx_timeout", Value(params.tx_timeout.value()));
                }
                if (params.tx_metadata.has_value() && !params.tx_metadata.value().empty()) {
                    auto tx_meta_map_sptr = std::make_shared<BoltMap>();
                    tx_meta_map_sptr->pairs = params.tx_metadata.value();
                    extra_pairs.emplace("tx_metadata", Value(tx_meta_map_sptr));
                }
                if (params.mode.has_value()) {  // Defaults to "w" if not sent, but can be explicit
                    extra_pairs.emplace("mode", Value(params.mode.value()));
                }
            }

            if (target_bolt_version.major >= 4) {  // db and imp_user for BEGIN introduced in Bolt 4.0
                if (params.db.has_value()) {
                    extra_pairs.emplace("db", Value(params.db.value()));
                }
                if (params.imp_user.has_value()) {
                    extra_pairs.emplace("imp_user", Value(params.imp_user.value()));
                }
            }

            if (target_bolt_version.major > 5 || (target_bolt_version.major == 5 && target_bolt_version.minor >= 2)) {  // notifications introduced in Bolt 5.2
                if (params.notifications_min_severity.has_value()) {
                    extra_pairs.emplace("notifications_minimum_severity", Value(params.notifications_min_severity.value()));
                }
                if (params.notifications_disabled_categories.has_value() && !params.notifications_disabled_categories.value().empty()) {
                    auto disabled_cat_list_sptr = std::make_shared<BoltList>();
                    for (const auto& cat : params.notifications_disabled_categories.value()) {
                        disabled_cat_list_sptr->elements.emplace_back(Value(cat));
                    }
                    extra_pairs.emplace("notifications_disabled_categories", Value(disabled_cat_list_sptr));
                }
            }

            // Add other custom fields
            for (const auto& field_pair : params.other_extra_fields) {
                extra_pairs.emplace(field_pair.first, field_pair.second);
            }

            begin_struct_obj.fields.emplace_back(Value(extra_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(begin_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    // serialize_commit_message and serialize_rollback_message remain unchanged
    // ... (serialize_commit_message implementation) ...
    BoltError serialize_commit_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::COMMIT);
            auto empty_map_sptr = std::make_shared<BoltMap>();
            pss_sptr->fields.emplace_back(Value(empty_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    // ... (serialize_rollback_message implementation) ...
    BoltError serialize_rollback_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::ROLLBACK);
            auto empty_map_sptr = std::make_shared<BoltMap>();
            pss_sptr->fields.emplace_back(Value(empty_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

}  // namespace boltprotocol