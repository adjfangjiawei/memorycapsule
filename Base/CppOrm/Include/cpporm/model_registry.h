#ifndef cpporm_MODEL_REGISTRY_H
#define cpporm_MODEL_REGISTRY_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <typeindex>
#include <vector>

#include "cpporm/model_base_class.h"  // For ModelBase

namespace cpporm {

    namespace internal {
        using ModelFactory = std::function<std::unique_ptr<ModelBase>()>;
        std::map<std::type_index, ModelFactory> &getGlobalModelFactoryRegistry();
        std::mutex &getGlobalModelFactoryRegistryMutex();

        template <typename T>
        void registerModelFactory() {
            std::lock_guard<std::mutex> lock(getGlobalModelFactoryRegistryMutex());
            getGlobalModelFactoryRegistry()[typeid(T)] = []() {
                return std::make_unique<T>();
            };
        }

        using VoidFunc = std::function<void()>;
        std::vector<VoidFunc> &getGlobalModelFinalizerFunctions();
        std::mutex &getGlobalModelFinalizersRegistryMutex();

        template <typename ModelClass>
        void registerModelClassForFinalization() {
            std::lock_guard<std::mutex> lock(getGlobalModelFinalizersRegistryMutex());
            getGlobalModelFinalizerFunctions().push_back([]() {
                ModelClass::_finalizeModelMeta();
            });
        }

    }  // namespace internal

    void finalize_all_model_meta();

}  // namespace cpporm

#endif  // cpporm_MODEL_REGISTRY_H