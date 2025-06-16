#include <exception>  // For std::bad_alloc, std::exception
#include <map>
#include <memory>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_begin_message(const BeginMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure begin_struct_obj;  // On stack
        begin_struct_obj.tag = static_cast<uint8_t>(MessageTag::BEGIN);
        std::shared_ptr<BoltMap> extra_map_sptr;

        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            extra_map_sptr->pairs = params.extra;  // map copy
            // BEGIN message structure is: BEGIN {<extra_map>}
            begin_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_begin_message (map prep): " << e_std.what() << std::endl;
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
            // std::cerr << "Std exception (make_shared PSS) in serialize_begin_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_commit_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::COMMIT);
            // COMMIT PSS has one field, which is an empty map {}
            // So, we need to create an empty map and add it as a field.
            auto empty_map_sptr = std::make_shared<BoltMap>();
            // empty_map_sptr->pairs is already empty.
            pss_sptr->fields.emplace_back(Value(empty_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_commit_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_rollback_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::ROLLBACK);
            // ROLLBACK PSS has one field, which is an empty map {}
            auto empty_map_sptr = std::make_shared<BoltMap>();
            pss_sptr->fields.emplace_back(Value(empty_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_rollback_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

}  // namespace boltprotocol