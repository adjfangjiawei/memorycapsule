#include <exception>  // For std::bad_alloc, std::exception
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"  // 主头文件，声明这些函数
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure pss_hello_obj;
        pss_hello_obj.tag = static_cast<uint8_t>(MessageTag::HELLO);
        std::shared_ptr<BoltMap> map_for_field_sptr;
        try {
            map_for_field_sptr = std::make_shared<BoltMap>();
            map_for_field_sptr->pairs = params.extra_auth_tokens;  // map copy
            Value map_value_as_field(map_for_field_sptr);
            pss_hello_obj.fields.emplace_back(std::move(map_value_as_field));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_hello_message (map prep): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pss_hello_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_hello_message (pss make_shared): " << e_std.what() << std::endl;
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
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::GOODBYE);
            // No fields for GOODBYE
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_goodbye_message: " << e_std.what() << std::endl;
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
            // No fields for RESET
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_reset_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

}  // namespace boltprotocol