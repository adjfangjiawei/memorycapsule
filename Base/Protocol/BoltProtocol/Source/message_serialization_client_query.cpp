#include <exception>  // For std::bad_alloc, std::exception
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_run_message(const RunMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure run_struct_obj;  // On stack
        run_struct_obj.tag = static_cast<uint8_t>(MessageTag::RUN);

        std::shared_ptr<BoltMap> parameters_map_sptr;
        std::shared_ptr<BoltMap> extra_metadata_map_sptr;

        try {
            // Field 1: Cypher query (string)
            run_struct_obj.fields.emplace_back(Value(params.cypher_query));

            // Field 2: Parameters map
            parameters_map_sptr = std::make_shared<BoltMap>();
            parameters_map_sptr->pairs = params.parameters;  // map copy
            run_struct_obj.fields.emplace_back(Value(parameters_map_sptr));

            // Field 3: Extra metadata map
            extra_metadata_map_sptr = std::make_shared<BoltMap>();
            extra_metadata_map_sptr->pairs = params.extra_metadata;  // map copy
            run_struct_obj.fields.emplace_back(Value(extra_metadata_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_run_message (fields prep): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(run_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_run_message (pss make_shared): " << e_std.what() << std::endl;
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
            // PULL message structure is: PULL {<extra_map>}
            pull_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_pull_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pull_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_pull_message (pss make_shared): " << e_std.what() << std::endl;
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
            // DISCARD message structure is: DISCARD {<extra_map>}
            discard_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_discard_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(discard_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_discard_message (pss make_shared): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

}  // namespace boltprotocol