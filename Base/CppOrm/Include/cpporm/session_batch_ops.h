// cpporm/session_batch_ops.h
#ifndef cpporm_SESSION_BATCH_OPS_H
#define cpporm_SESSION_BATCH_OPS_H

#include <algorithm>  // For std::min
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "cpporm/error.h"
#include "cpporm/model_base.h"    // For ModelType constraints
#include "cpporm/session_core.h"  // Needs Session definition

// QueryBuilder is included via session_core.h -> query_builder.h

namespace cpporm {

    // --- Implementations for Templated CreateBatch methods ---
    // 这些实现看起来应该能与 CreateBatchProviderInternal 配合，
    // 因为后者处理底层的 ModelBase*。主要的关注点是返回类型和错误处理。
    // 返回类型 `std::expected<std::vector<std::shared_ptr<ModelType>>, Error>` 保持不变。
    // 错误处理依赖于 CreateBatchProviderInternal 返回的 Error 对象。

    // Vector of raw pointers version
    template <typename ModelType>
    inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error> Session::CreateBatch(const std::vector<ModelType *> &models_raw_input, size_t internal_db_batch_size_hint, const OnConflictClause *conflict_options_override) {
        static_assert(std::is_base_of<ModelBase, ModelType>::value, "ModelType must derive from ModelBase.");
        if (models_raw_input.empty()) return std::vector<std::shared_ptr<ModelType>>{};

        std::vector<ModelBase *> base_models_for_internal_provider;
        base_models_for_internal_provider.reserve(models_raw_input.size());
        std::vector<ModelType *> original_input_ptrs_filtered;
        original_input_ptrs_filtered.reserve(models_raw_input.size());

        for (ModelType *typed_ptr : models_raw_input) {
            if (typed_ptr) {
                base_models_for_internal_provider.push_back(static_cast<ModelBase *>(typed_ptr));
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
        auto internal_data_provider = [&]() -> std::optional<std::vector<ModelBase *>> {
            if (current_idx_provider >= base_models_for_internal_provider.size()) {
                return std::nullopt;
            }
            std::vector<ModelBase *> chunk_to_process;
            size_t end_idx = std::min(base_models_for_internal_provider.size(), current_idx_provider + internal_db_batch_size_hint);
            for (size_t i = current_idx_provider; i < end_idx; ++i) {
                chunk_to_process.push_back(base_models_for_internal_provider[i]);
            }
            current_idx_provider = end_idx;
            if (chunk_to_process.empty()) return std::nullopt;  // 表示没有更多数据或当前块为空
            return chunk_to_process;
        };

        std::vector<ModelBase *> successfully_processed_and_backfilled_models_collector;

        auto internal_completion_callback = [&overall_error_from_all_batches, &successfully_processed_and_backfilled_models_collector](const std::vector<ModelBase *> &processed_batch_models_with_ids, Error batch_db_error) {
            if (batch_db_error && overall_error_from_all_batches.isOk()) {
                overall_error_from_all_batches = batch_db_error;
            }
            if (!batch_db_error) {
                for (ModelBase *bm : processed_batch_models_with_ids) {
                    if (bm && bm->_is_persisted) {
                        successfully_processed_and_backfilled_models_collector.push_back(bm);
                    }
                }
            }
        };

        Error provider_loop_error = this->CreateBatchProviderInternal(qb_proto, internal_data_provider, internal_completion_callback, conflict_options_override);

        if (provider_loop_error) return std::unexpected(provider_loop_error);
        if (overall_error_from_all_batches) return std::unexpected(overall_error_from_all_batches);

        final_result_sptrs.reserve(successfully_processed_and_backfilled_models_collector.size());
        for (ModelBase *base_ptr : successfully_processed_and_backfilled_models_collector) {
            ModelType *typed_ptr = static_cast<ModelType *>(base_ptr);
            bool found_in_original = false;
            for (ModelType *original_typed_ptr : original_input_ptrs_filtered) {
                if (typed_ptr == original_typed_ptr) {
                    found_in_original = true;
                    break;
                }
            }
            if (found_in_original) {
                final_result_sptrs.emplace_back(typed_ptr, [](ModelType *) { /* no-op deleter */ });
            }
        }
        return final_result_sptrs;
    }

    // Vector of unique_ptr version
    template <typename ModelType>
    inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error> Session::CreateBatch(std::vector<std::unique_ptr<ModelType>> &models_unique_input, size_t internal_db_batch_size_hint, const OnConflictClause *conflict_options_override) {
        static_assert(std::is_base_of<ModelBase, ModelType>::value, "ModelType must derive from ModelBase.");
        if (models_unique_input.empty()) return std::vector<std::shared_ptr<ModelType>>{};

        std::vector<ModelBase *> base_models_for_internal_provider;
        std::vector<ModelType *> unique_ptr_original_raw_ptrs;
        base_models_for_internal_provider.reserve(models_unique_input.size());
        unique_ptr_original_raw_ptrs.reserve(models_unique_input.size());

        for (const auto &u_ptr : models_unique_input) {
            if (u_ptr) {
                base_models_for_internal_provider.push_back(static_cast<ModelBase *>(u_ptr.get()));
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
        auto internal_data_provider = [&]() -> std::optional<std::vector<ModelBase *>> {
            if (current_idx_provider >= base_models_for_internal_provider.size()) {
                return std::nullopt;
            }
            std::vector<ModelBase *> chunk_to_process;
            size_t end_idx = std::min(base_models_for_internal_provider.size(), current_idx_provider + internal_db_batch_size_hint);
            for (size_t i = current_idx_provider; i < end_idx; ++i) {
                chunk_to_process.push_back(base_models_for_internal_provider[i]);
            }
            current_idx_provider = end_idx;
            if (chunk_to_process.empty()) return std::nullopt;
            return chunk_to_process;
        };

        std::vector<ModelBase *> successfully_processed_and_backfilled_models_collector;

        auto internal_completion_callback = [&overall_error_from_all_batches, &successfully_processed_and_backfilled_models_collector](const std::vector<ModelBase *> &processed_batch_models_with_ids, Error batch_db_error) {
            if (batch_db_error && overall_error_from_all_batches.isOk()) {
                overall_error_from_all_batches = batch_db_error;
            }
            if (!batch_db_error) {
                for (ModelBase *bm : processed_batch_models_with_ids) {
                    if (bm && bm->_is_persisted) {
                        successfully_processed_and_backfilled_models_collector.push_back(bm);
                    }
                }
            }
        };

        Error provider_loop_error = this->CreateBatchProviderInternal(qb_proto, internal_data_provider, internal_completion_callback, conflict_options_override);

        if (provider_loop_error) return std::unexpected(provider_loop_error);
        if (overall_error_from_all_batches) return std::unexpected(overall_error_from_all_batches);

        final_result_sptrs.reserve(successfully_processed_and_backfilled_models_collector.size());
        for (auto &u_ptr : models_unique_input) {
            if (u_ptr) {
                bool found_and_persisted = false;
                for (ModelBase *processed_base_ptr : successfully_processed_and_backfilled_models_collector) {
                    if (static_cast<ModelBase *>(u_ptr.get()) == processed_base_ptr) {
                        found_and_persisted = true;
                        break;
                    }
                }
                if (found_and_persisted) {
                    final_result_sptrs.push_back(std::move(u_ptr));
                }
            }
        }
        models_unique_input.erase(std::remove(models_unique_input.begin(), models_unique_input.end(), nullptr), models_unique_input.end());
        return final_result_sptrs;
    }

    // Vector of shared_ptr version
    template <typename ModelType>
    inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error> Session::CreateBatch(const std::vector<std::shared_ptr<ModelType>> &models_shared_input, size_t internal_db_batch_size_hint, const OnConflictClause *conflict_options_override) {
        static_assert(std::is_base_of<ModelBase, ModelType>::value, "ModelType must derive from ModelBase.");
        if (models_shared_input.empty()) return std::vector<std::shared_ptr<ModelType>>{};

        std::vector<ModelBase *> base_models_for_internal_provider;
        base_models_for_internal_provider.reserve(models_shared_input.size());
        for (const auto &s_ptr : models_shared_input) {
            if (s_ptr) {
                base_models_for_internal_provider.push_back(static_cast<ModelBase *>(s_ptr.get()));
            }
        }
        if (base_models_for_internal_provider.empty()) {
            return std::vector<std::shared_ptr<ModelType>>{};
        }

        QueryBuilder qb_proto = this->Model<ModelType>();
        std::vector<std::shared_ptr<ModelType>> final_result_sptrs;
        Error overall_error_from_all_batches = make_ok();

        size_t current_idx_provider = 0;
        auto internal_data_provider = [&]() -> std::optional<std::vector<ModelBase *>> {
            if (current_idx_provider >= base_models_for_internal_provider.size()) {
                return std::nullopt;
            }
            std::vector<ModelBase *> chunk_to_process;
            size_t end_idx = std::min(base_models_for_internal_provider.size(), current_idx_provider + internal_db_batch_size_hint);
            for (size_t i = current_idx_provider; i < end_idx; ++i) {
                chunk_to_process.push_back(base_models_for_internal_provider[i]);
            }
            current_idx_provider = end_idx;
            if (chunk_to_process.empty()) return std::nullopt;
            return chunk_to_process;
        };

        std::vector<ModelBase *> successfully_processed_and_backfilled_models_collector;

        auto internal_completion_callback = [&overall_error_from_all_batches, &successfully_processed_and_backfilled_models_collector](const std::vector<ModelBase *> &processed_batch_models_with_ids, Error batch_db_error) {
            if (batch_db_error && overall_error_from_all_batches.isOk()) {
                overall_error_from_all_batches = batch_db_error;
            }
            if (!batch_db_error) {
                for (ModelBase *bm : processed_batch_models_with_ids) {
                    if (bm && bm->_is_persisted) {
                        successfully_processed_and_backfilled_models_collector.push_back(bm);
                    }
                }
            }
        };

        Error provider_loop_error = this->CreateBatchProviderInternal(qb_proto, internal_data_provider, internal_completion_callback, conflict_options_override);

        if (provider_loop_error) return std::unexpected(provider_loop_error);
        if (overall_error_from_all_batches) return std::unexpected(overall_error_from_all_batches);

        final_result_sptrs.reserve(successfully_processed_and_backfilled_models_collector.size());
        for (const auto &original_s_ptr : models_shared_input) {
            if (original_s_ptr) {
                bool found_and_persisted = false;
                for (ModelBase *processed_base_ptr : successfully_processed_and_backfilled_models_collector) {
                    if (static_cast<ModelBase *>(original_s_ptr.get()) == processed_base_ptr) {
                        found_and_persisted = true;
                        break;
                    }
                }
                if (found_and_persisted) {
                    final_result_sptrs.push_back(original_s_ptr);
                }
            }
        }
        return final_result_sptrs;
    }

    // Provider-based CreateBatch implementation
    template <typename ModelType>
    inline std::expected<std::vector<std::shared_ptr<ModelType>>, Error> Session::CreateBatch(std::function<std::optional<std::vector<ModelType *>>()> data_batch_provider_typed, const OnConflictClause *conflict_options_override, size_t) {
        static_assert(std::is_base_of<ModelBase, ModelType>::value, "ModelType must derive from ModelBase.");

        if (!data_batch_provider_typed) {
            return std::unexpected(Error(ErrorCode::InvalidConfiguration, "CreateBatch (provider): data_batch_provider_typed is null."));
        }

        QueryBuilder qb_proto = this->Model<ModelType>();
        std::vector<std::shared_ptr<ModelType>> all_successfully_created_models_sptr;
        Error overall_error_from_all_batches = make_ok();

        auto data_batch_provider_base_adapted = [provider_typed = std::move(data_batch_provider_typed)]() mutable -> std::optional<std::vector<ModelBase *>> {
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

        std::vector<ModelBase *> collected_processed_base_models_with_ids;

        auto internal_completion_callback_for_provider = [&overall_error_from_all_batches, &collected_processed_base_models_with_ids](const std::vector<ModelBase *> &processed_batch_models_from_internal_call, Error batch_db_error) {
            if (batch_db_error && overall_error_from_all_batches.isOk()) {
                overall_error_from_all_batches = batch_db_error;
            }
            if (!batch_db_error) {
                for (ModelBase *bm : processed_batch_models_from_internal_call) {
                    if (bm && bm->_is_persisted) {
                        collected_processed_base_models_with_ids.push_back(bm);
                    }
                }
            }
        };

        Error provider_loop_error = this->CreateBatchProviderInternal(qb_proto, data_batch_provider_base_adapted, internal_completion_callback_for_provider, conflict_options_override);

        if (provider_loop_error) {
            return std::unexpected(provider_loop_error);
        }
        if (overall_error_from_all_batches) {
            return std::unexpected(overall_error_from_all_batches);
        }

        all_successfully_created_models_sptr.reserve(collected_processed_base_models_with_ids.size());
        for (ModelBase *base_ptr : collected_processed_base_models_with_ids) {
            ModelType *typed_ptr = static_cast<ModelType *>(base_ptr);
            all_successfully_created_models_sptr.emplace_back(typed_ptr);
        }

        return all_successfully_created_models_sptr;
    }

}  // namespace cpporm

#endif  // cpporm_SESSION_BATCH_OPS_H