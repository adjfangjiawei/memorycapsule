#include <exception>  // For std::bad_alloc, std::exception
#include <map>
#include <memory>  // For std::make_shared, std::shared_ptr
#include <vector>  // For PackStreamStructure::fields

#include "boltprotocol/message_defs.h"           // For message structs, Value, MessageTag, BoltError
#include "boltprotocol/message_serialization.h"  // Defines interfaces
#include "boltprotocol/packstream_writer.h"      // For PackStreamWriter

namespace boltprotocol {

    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure pss_hello_obj;
        pss_hello_obj.tag = static_cast<uint8_t>(MessageTag::HELLO);

        std::shared_ptr<BoltMap> map_for_field_sptr;
        try {
            map_for_field_sptr = std::make_shared<BoltMap>();
            for (const auto& p : params.extra_auth_tokens) {
                // Value copy/move into map can also throw if allocators are involved, though less common for simple types
                map_for_field_sptr->pairs.emplace(p.first, p.second);
            }
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);  // Inform writer about the error origin if possible
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!map_for_field_sptr) {  // Should be redundant if make_shared throws or we catch
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        try {
            Value map_value_as_field(std::move(map_for_field_sptr));
            pss_hello_obj.fields.emplace_back(std::move(map_value_as_field));  // vector::emplace_back can throw
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pss_hello_obj));
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

    BoltError serialize_run_message(const RunMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure run_struct_obj;
        run_struct_obj.tag = static_cast<uint8_t>(MessageTag::RUN);

        std::shared_ptr<BoltMap> parameters_map_sptr;
        std::shared_ptr<BoltMap> extra_metadata_map_sptr;

        try {
            run_struct_obj.fields.emplace_back(Value(params.cypher_query));  // String copy/move

            parameters_map_sptr = std::make_shared<BoltMap>();
            for (const auto& p : params.parameters) {
                parameters_map_sptr->pairs.emplace(p.first, p.second);
            }
            run_struct_obj.fields.emplace_back(Value(parameters_map_sptr));  // std::move(parameters_map_sptr) below

            extra_metadata_map_sptr = std::make_shared<BoltMap>();
            for (const auto& p : params.extra_metadata) {
                extra_metadata_map_sptr->pairs.emplace(p.first, p.second);
            }
            run_struct_obj.fields.emplace_back(Value(extra_metadata_map_sptr));  // std::move(extra_metadata_map_sptr) below

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        // No need to std::move the sptr itself when constructing Value if it's the last use,
        // Value(sptr) will copy the shared_ptr (increment ref count).
        // Value(std::move(sptr)) is fine too.

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(run_struct_obj));
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

    BoltError serialize_pull_message(const PullMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure pull_struct_obj;
        pull_struct_obj.tag = static_cast<uint8_t>(MessageTag::PULL);
        std::shared_ptr<BoltMap> extra_map_sptr;

        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            if (params.n.has_value()) {
                extra_map_sptr->pairs.emplace("n", Value(params.n.value()));
            }
            if (params.qid.has_value()) {
                extra_map_sptr->pairs.emplace("qid", Value(params.qid.value()));
            }
            pull_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pull_struct_obj));
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

    BoltError serialize_discard_message(const DiscardMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure discard_struct_obj;
        discard_struct_obj.tag = static_cast<uint8_t>(MessageTag::DISCARD);
        std::shared_ptr<BoltMap> extra_map_sptr;

        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            if (params.n.has_value()) {
                extra_map_sptr->pairs.emplace("n", Value(params.n.value()));
            }
            if (params.qid.has_value()) {
                extra_map_sptr->pairs.emplace("qid", Value(params.qid.value()));
            }
            discard_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(discard_struct_obj));
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

    BoltError serialize_goodbye_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            // PackStreamStructure goodbye_struct_obj; // Can be on stack
            // goodbye_struct_obj.tag = static_cast<uint8_t>(MessageTag::GOODBYE);
            // pss_sptr = std::make_shared<PackStreamStructure>(std::move(goodbye_struct_obj));
            // Or more directly if it has no fields and default ctor is fine for tag=0 then set:
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::GOODBYE);
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

    BoltError serialize_reset_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::RESET);
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