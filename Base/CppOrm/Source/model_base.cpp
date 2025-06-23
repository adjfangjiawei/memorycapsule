// Base/CppOrm/Source/model_base.cpp
#include "cpporm/model_base.h"  // For declarations

namespace cpporm {
    namespace internal {

        // Definitions for the global model factory registry
        std::map<std::type_index, ModelFactory> &getGlobalModelFactoryRegistry() {
            static std::map<std::type_index, ModelFactory> registry;
            return registry;
        }

        std::mutex &getGlobalModelFactoryRegistryMutex() {
            static std::mutex registry_mutex;
            return registry_mutex;
        }

        // Definitions for Global Meta Finalization
        std::vector<VoidFunc> &getGlobalModelFinalizerFunctions() {
            static std::vector<VoidFunc> finalizers;
            return finalizers;
        }
        std::mutex &getGlobalModelFinalizersRegistryMutex() {
            static std::mutex mtx;
            return mtx;
        }

    }  // namespace internal

    // Definition for the user-callable global finalization function
    void finalize_all_model_meta() {
        // It's crucial that this function is called *after* all static initializers
        // (which call registerModelClassForFinalization) have run, and all model
        // class definitions are complete.

        // Create a copy of the finalizer functions to avoid issues if a finalizer
        // somehow tries to re-register (should not happen with current design).
        std::vector<internal::VoidFunc> finalizers_copy;
        {
            std::lock_guard<std::mutex> lock(internal::getGlobalModelFinalizersRegistryMutex());
            finalizers_copy = internal::getGlobalModelFinalizerFunctions();
        }

        // Sort finalizers? Not strictly necessary if _finalizeModelMeta is idempotent
        // and handles its dependencies gracefully (which it tries to, but typeid
        // makes it tricky). For now, call in registration order. A more robust system
        // might involve multiple passes or dependency tracking.
        for (const auto &finalizer_func : finalizers_copy) {
            if (finalizer_func) {
                finalizer_func();
            }
        }
    }

}  // namespace cpporm