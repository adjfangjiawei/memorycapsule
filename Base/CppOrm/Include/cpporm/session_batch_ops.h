// cpporm/session_batch_ops.h
#ifndef cpporm_SESSION_BATCH_OPS_H
#define cpporm_SESSION_BATCH_OPS_H

#include "cpporm/error.h"
#include "cpporm/model_base.h"   // For ModelType constraints
#include "cpporm/session_core.h" // Needs Session definition
#include <algorithm>              // For std::min
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Note: QueryBuilder is included via session_core.h -> query_builder.h

namespace cpporm {

// --- Implementations for Templated CreateBatch methods ---

// Vector of raw pointers version
template <typename ModelType>
inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error>
Session::CreateBatch(const std::vector<ModelType *> &models_raw_input,
                     size_t internal_db_batch_size_hint,
                     const OnConflictClause *conflict_options_override) {
  static_assert(std::is_base_of<ModelBase, ModelType>::value,
                "ModelType must derive from ModelBase.");
  if (models_raw_input.empty())
    return std::vector<std::shared_ptr<ModelType>>{};

  std::vector<ModelBase *> base_models_for_internal_provider;
  base_models_for_internal_provider.reserve(models_raw_input.size());
  // Store a way to map back from ModelBase* to the original ModelType* for
  // shared_ptr creation
  std::vector<ModelType *> original_input_ptrs_filtered;
  original_input_ptrs_filtered.reserve(models_raw_input.size());

  for (ModelType *typed_ptr : models_raw_input) {
    if (typed_ptr) {
      base_models_for_internal_provider.push_back(
          static_cast<ModelBase *>(typed_ptr));
      original_input_ptrs_filtered.push_back(typed_ptr);
    }
  }
  if (base_models_for_internal_provider.empty()) {
    return std::vector<std::shared_ptr<ModelType>>{};
  }

  QueryBuilder qb_proto = this->Model<ModelType>();
  std::vector<std::shared_ptr<ModelType>> final_result_sptrs;
  Error overall_error_from_all_batches = make_ok();

  size_t current_idx_provider = 0;
  auto internal_data_provider =
      [&]() -> std::optional<std::vector<ModelBase *>> {
    if (current_idx_provider >= base_models_for_internal_provider.size()) {
      return std::nullopt;
    }
    std::vector<ModelBase *> chunk_to_process;
    size_t end_idx =
        std::min(base_models_for_internal_provider.size(),
                 current_idx_provider + internal_db_batch_size_hint);
    for (size_t i = current_idx_provider; i < end_idx; ++i) {
      chunk_to_process.push_back(base_models_for_internal_provider[i]);
    }
    current_idx_provider = end_idx;
    if (chunk_to_process.empty())
      return std::nullopt;
    return chunk_to_process;
  };

  std::vector<ModelBase *>
      successfully_processed_and_backfilled_models_collector;

  auto internal_completion_callback =
      [&overall_error_from_all_batches,
       &successfully_processed_and_backfilled_models_collector](
          const std::vector<ModelBase *>
              &processed_batch_models_with_ids, // These have IDs backfilled
          Error batch_db_error) {
        if (batch_db_error && overall_error_from_all_batches.isOk()) {
          overall_error_from_all_batches = batch_db_error;
        }
        if (!batch_db_error) { // Only collect if this DB batch was successful
          for (ModelBase *bm : processed_batch_models_with_ids) {
            // We only care about those that were actually persisted by the DB
            // operation
            if (bm && bm->_is_persisted) {
              successfully_processed_and_backfilled_models_collector.push_back(
                  bm);
            }
          }
        }
      };

  Error provider_loop_error = this->CreateBatchProviderInternal(
      qb_proto, internal_data_provider, internal_completion_callback,
      conflict_options_override);

  if (provider_loop_error)
    return std::unexpected(provider_loop_error);
  if (overall_error_from_all_batches)
    return std::unexpected(overall_error_from_all_batches);

  // Construct shared_ptrs for successfully processed models.
  // For raw pointer input, this creates shared_ptrs that *alias* the original
  // raw pointers. The caller is responsible for the lifetime of the objects
  // pointed to by models_raw_input. The shared_ptr will not delete them if a
  // custom no-op deleter is used. If a standard shared_ptr is created, it
  // assumes it takes ownership. For safety and to fulfill the "return
  // shared_ptr" contract, we'll make shared_ptrs that share ownership with a
  // no-op deleter, assuming the original raw pointers are managed elsewhere or
  // have static/stack lifetime (latter being dangerous for shared_ptr). A more
  // robust solution for raw pointers might be to copy into new shared_ptrs, or
  // change the return type for the raw pointer overload. Given the constraints,
  // let's make shared_ptrs that do NOT delete the raw pointer. This requires
  // the user to be very careful.
  final_result_sptrs.reserve(
      successfully_processed_and_backfilled_models_collector.size());
  for (ModelBase *base_ptr :
       successfully_processed_and_backfilled_models_collector) {
    ModelType *typed_ptr = static_cast<ModelType *>(base_ptr);
    // Check if this typed_ptr was part of the original input (it should be)
    bool found_in_original = false;
    for (ModelType *original_typed_ptr : original_input_ptrs_filtered) {
      if (typed_ptr == original_typed_ptr) {
        found_in_original = true;
        break;
      }
    }
    if (found_in_original) {
      // Create a shared_ptr that does not delete the object.
      // The caller MUST ensure the object's lifetime.
      final_result_sptrs.emplace_back(typed_ptr,
                                      [](ModelType *) { /* no-op deleter */ });
    }
  }
  return final_result_sptrs;
}

// Vector of unique_ptr version
template <typename ModelType>
inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error>
Session::CreateBatch(
    std::vector<std::unique_ptr<ModelType>> &models_unique_input,
    size_t internal_db_batch_size_hint,
    const OnConflictClause *conflict_options_override) {
  static_assert(std::is_base_of<ModelBase, ModelType>::value,
                "ModelType must derive from ModelBase.");
  if (models_unique_input.empty())
    return std::vector<std::shared_ptr<ModelType>>{};

  std::vector<ModelBase *> base_models_for_internal_provider;
  std::vector<ModelType *>
      unique_ptr_original_raw_ptrs; // To map back after processing
  base_models_for_internal_provider.reserve(models_unique_input.size());
  unique_ptr_original_raw_ptrs.reserve(models_unique_input.size());

  for (const auto &u_ptr : models_unique_input) {
    if (u_ptr) {
      base_models_for_internal_provider.push_back(
          static_cast<ModelBase *>(u_ptr.get()));
      unique_ptr_original_raw_ptrs.push_back(u_ptr.get());
    }
  }
  if (base_models_for_internal_provider.empty()) {
    return std::vector<std::shared_ptr<ModelType>>{};
  }

  QueryBuilder qb_proto = this->Model<ModelType>();
  std::vector<std::shared_ptr<ModelType>> final_result_sptrs;
  Error overall_error_from_all_batches = make_ok();

  size_t current_idx_provider = 0;
  auto internal_data_provider =
      [&]() -> std::optional<std::vector<ModelBase *>> {
    if (current_idx_provider >= base_models_for_internal_provider.size()) {
      return std::nullopt;
    }
    std::vector<ModelBase *> chunk_to_process;
    size_t end_idx =
        std::min(base_models_for_internal_provider.size(),
                 current_idx_provider + internal_db_batch_size_hint);
    for (size_t i = current_idx_provider; i < end_idx; ++i) {
      chunk_to_process.push_back(base_models_for_internal_provider[i]);
    }
    current_idx_provider = end_idx;
    if (chunk_to_process.empty())
      return std::nullopt;
    return chunk_to_process;
  };

  std::vector<ModelBase *>
      successfully_processed_and_backfilled_models_collector;

  auto internal_completion_callback =
      [&overall_error_from_all_batches,
       &successfully_processed_and_backfilled_models_collector](
          const std::vector<ModelBase *> &processed_batch_models_with_ids,
          Error batch_db_error) {
        if (batch_db_error && overall_error_from_all_batches.isOk()) {
          overall_error_from_all_batches = batch_db_error;
        }
        if (!batch_db_error) {
          for (ModelBase *bm : processed_batch_models_with_ids) {
            if (bm && bm->_is_persisted) {
              successfully_processed_and_backfilled_models_collector.push_back(
                  bm);
            }
          }
        }
      };

  Error provider_loop_error = this->CreateBatchProviderInternal(
      qb_proto, internal_data_provider, internal_completion_callback,
      conflict_options_override);

  if (provider_loop_error)
    return std::unexpected(provider_loop_error);
  if (overall_error_from_all_batches)
    return std::unexpected(overall_error_from_all_batches);

  final_result_sptrs.reserve(
      successfully_processed_and_backfilled_models_collector.size());
  // Iterate through the *original* input unique_ptrs.
  // If the object it managed was successfully processed, transfer ownership to
  // a shared_ptr.
  for (auto &u_ptr :
       models_unique_input) { // Iterate by non-const ref to allow moving
    if (u_ptr) {
      bool found_and_persisted = false;
      for (ModelBase *processed_base_ptr :
           successfully_processed_and_backfilled_models_collector) {
        if (static_cast<ModelBase *>(u_ptr.get()) == processed_base_ptr) {
          found_and_persisted = true;
          break;
        }
      }
      if (found_and_persisted) {
        final_result_sptrs.push_back(std::move(u_ptr)); // Transfer ownership
      }
    }
  }
  // Remove nullptrs from the input vector that were moved
  models_unique_input.erase(std::remove(models_unique_input.begin(),
                                        models_unique_input.end(), nullptr),
                            models_unique_input.end());
  return final_result_sptrs;
}

// Vector of shared_ptr version
template <typename ModelType>
inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error>
Session::CreateBatch(
    const std::vector<std::shared_ptr<ModelType>> &models_shared_input,
    size_t internal_db_batch_size_hint,
    const OnConflictClause *conflict_options_override) {
  static_assert(std::is_base_of<ModelBase, ModelType>::value,
                "ModelType must derive from ModelBase.");
  if (models_shared_input.empty())
    return std::vector<std::shared_ptr<ModelType>>{};

  std::vector<ModelBase *> base_models_for_internal_provider;
  base_models_for_internal_provider.reserve(models_shared_input.size());
  for (const auto &s_ptr : models_shared_input) {
    if (s_ptr) {
      base_models_for_internal_provider.push_back(
          static_cast<ModelBase *>(s_ptr.get()));
    }
  }
  if (base_models_for_internal_provider.empty()) {
    return std::vector<std::shared_ptr<ModelType>>{};
  }

  QueryBuilder qb_proto = this->Model<ModelType>();
  std::vector<std::shared_ptr<ModelType>> final_result_sptrs;
  Error overall_error_from_all_batches = make_ok();

  size_t current_idx_provider = 0;
  auto internal_data_provider =
      [&]() -> std::optional<std::vector<ModelBase *>> {
    if (current_idx_provider >= base_models_for_internal_provider.size()) {
      return std::nullopt;
    }
    std::vector<ModelBase *> chunk_to_process;
    size_t end_idx =
        std::min(base_models_for_internal_provider.size(),
                 current_idx_provider + internal_db_batch_size_hint);
    for (size_t i = current_idx_provider; i < end_idx; ++i) {
      chunk_to_process.push_back(base_models_for_internal_provider[i]);
    }
    current_idx_provider = end_idx;
    if (chunk_to_process.empty())
      return std::nullopt;
    return chunk_to_process;
  };

  std::vector<ModelBase *>
      successfully_processed_and_backfilled_models_collector;

  auto internal_completion_callback =
      [&overall_error_from_all_batches,
       &successfully_processed_and_backfilled_models_collector](
          const std::vector<ModelBase *> &processed_batch_models_with_ids,
          Error batch_db_error) {
        if (batch_db_error && overall_error_from_all_batches.isOk()) {
          overall_error_from_all_batches = batch_db_error;
        }
        if (!batch_db_error) {
          for (ModelBase *bm : processed_batch_models_with_ids) {
            if (bm && bm->_is_persisted) {
              successfully_processed_and_backfilled_models_collector.push_back(
                  bm);
            }
          }
        }
      };

  Error provider_loop_error = this->CreateBatchProviderInternal(
      qb_proto, internal_data_provider, internal_completion_callback,
      conflict_options_override);

  if (provider_loop_error)
    return std::unexpected(provider_loop_error);
  if (overall_error_from_all_batches)
    return std::unexpected(overall_error_from_all_batches);

  final_result_sptrs.reserve(
      successfully_processed_and_backfilled_models_collector.size());
  // Iterate over the original input shared_ptrs.
  // If the object it points to was successfully processed, add this original
  // shared_ptr to the result.
  for (const auto &original_s_ptr : models_shared_input) {
    if (original_s_ptr) {
      bool found_and_persisted = false;
      for (ModelBase *processed_base_ptr :
           successfully_processed_and_backfilled_models_collector) {
        if (static_cast<ModelBase *>(original_s_ptr.get()) ==
            processed_base_ptr) {
          found_and_persisted = true;
          break;
        }
      }
      if (found_and_persisted) {
        final_result_sptrs.push_back(
            original_s_ptr); // Add the original shared_ptr
      }
    }
  }
  return final_result_sptrs;
}

// Provider-based CreateBatch implementation
template <typename ModelType>
inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error>
Session::CreateBatch(
    std::function<std::optional<std::vector<ModelType *>>()>
        data_batch_provider_typed,
    const OnConflictClause *conflict_options_override,
    size_t /* internal_db_batch_processing_size_hint is for provider chunking,
              not directly used by DB op here */
) {
  static_assert(std::is_base_of<ModelBase, ModelType>::value,
                "ModelType must derive from ModelBase.");

  if (!data_batch_provider_typed) {
    return std::unexpected(
        Error(ErrorCode::InvalidConfiguration,
              "CreateBatch (provider): data_batch_provider_typed is null."));
  }

  QueryBuilder qb_proto = this->Model<ModelType>(); // Get ModelMeta implicitly
  std::vector<std::shared_ptr<ModelType>> all_successfully_created_models_sptr;
  Error overall_error_from_all_batches = make_ok();

  // Adapter for the data provider from ModelType* to ModelBase*
  auto data_batch_provider_base_adapted =
      [provider_typed = std::move(data_batch_provider_typed)]() mutable
      -> std::optional<std::vector<ModelBase *>> {
    auto typed_batch_opt = provider_typed();
    if (!typed_batch_opt.has_value()) {
      return std::nullopt;
    }
    std::vector<ModelBase *> base_batch;
    base_batch.reserve(typed_batch_opt.value().size());
    for (ModelType *typed_ptr : typed_batch_opt.value()) {
      if (typed_ptr) {
        base_batch.push_back(static_cast<ModelBase *>(typed_ptr));
      }
    }
    return base_batch;
  };

  // This vector will collect all ModelBase* pointers (which are actually
  // ModelType*) that were successfully processed and had their IDs backfilled
  // by CreateBatchProviderInternal.
  std::vector<ModelBase *> collected_processed_base_models_with_ids;

  auto internal_completion_callback_for_provider =
      [&overall_error_from_all_batches,
       &collected_processed_base_models_with_ids](
          const std::vector<ModelBase *>
              &processed_batch_models_from_internal_call, // these have IDs
                                                          // backfilled
          Error batch_db_error) {
        if (batch_db_error &&
            overall_error_from_all_batches
                .isOk()) { // Store the first error encountered
          overall_error_from_all_batches = batch_db_error;
        }
        if (!batch_db_error) { // Only collect models from successful DB batches
          for (ModelBase *bm : processed_batch_models_from_internal_call) {
            if (bm && bm->_is_persisted) { // Ensure it was actually persisted
                                           // by the DB operation
              collected_processed_base_models_with_ids.push_back(bm);
            }
          }
        }
      };

  Error provider_loop_error = this->CreateBatchProviderInternal(
      qb_proto, data_batch_provider_base_adapted,
      internal_completion_callback_for_provider, conflict_options_override);

  if (provider_loop_error) { // Error from the loop/provider mechanism itself
    return std::unexpected(provider_loop_error);
  }
  if (overall_error_from_all_batches) { // Error from one of the DB batch
                                        // operations
    return std::unexpected(overall_error_from_all_batches);
  }

  // Convert collected ModelBase* (which are actually ModelType* with IDs) to
  // shared_ptr<ModelType> Assumption: The objects pointed to by ModelType* from
  // the provider are suitable for shared_ptr management. E.g., they were
  // dynamically allocated by the provider and their lifetime is now
  // transferred.
  all_successfully_created_models_sptr.reserve(
      collected_processed_base_models_with_ids.size());
  for (ModelBase *base_ptr : collected_processed_base_models_with_ids) {
    ModelType *typed_ptr = static_cast<ModelType *>(base_ptr);
    // If provider did `new ModelType()`, then
    // `std::shared_ptr<ModelType>(typed_ptr)` is correct. If provider gives
    // pointers from a `std::vector<std::unique_ptr<ModelType>>` and releases
    // them, then this is also correct.
    all_successfully_created_models_sptr.emplace_back(typed_ptr);
  }

  return all_successfully_created_models_sptr;
}

} // namespace cpporm

#endif // cpporm_SESSION_BATCH_OPS_H