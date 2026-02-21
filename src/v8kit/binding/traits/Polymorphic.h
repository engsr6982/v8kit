#pragma once
#include "v8kit/core/Engine.h"
#include "v8kit/core/EngineScope.h"

#include <type_traits>
#include <typeinfo>

namespace v8kit::binding::traits {

namespace internal {

template <typename T>
struct PolymorphicTypeHookBase {
    static const void* get(const T* src, const std::type_info*& type) {
        type = src ? &typeid(T) : nullptr;
        return src;
    }
};

// 特化：如果是多态类型，使用 dynamic_cast<void*> 获取最派生对象的起始地址
template <typename T>
    requires std::is_polymorphic_v<T>
struct PolymorphicTypeHookBase<T> {
    static const void* get(const T* src, const std::type_info*& type) {
        if (src) {
            type = &typeid(*src);                  // RTTI 获取真实类型
            return dynamic_cast<const void*>(src); // 获取最顶层地址 (Most Derived Address)
        }
        type = nullptr;
        return nullptr;
    }
};

} // namespace internal

/**
 * @brief 多态钩子
 * @return pair<最派生对象的void指针, 真实类型的type_info>
 */
template <typename T>
struct PolymorphicTypeHook : internal::PolymorphicTypeHookBase<T> {};


namespace detail {

struct ResolvedCastSource {
    void const*      ptr;           // 最终决定使用的 C++ 指针（可能经过了偏移）
    ClassMeta const* meta;          // 最终决定使用的 JS 类定义
    bool             is_downcasted; // 是否发生了成功的多态向下转型
};

template <typename T>
ResolvedCastSource resolveCastSource(T* value) {
    auto& engine = EngineScope::currentRuntimeChecked();

    // 1. 获取动态类型和基址
    const std::type_info* dynamicType = nullptr;
    const void*           dynamicPtr  = traits::PolymorphicTypeHook<T>::get(value, dynamicType);

    // 2. 尝试决议动态类型 (Downcast)
    if (dynamicType && dynamicPtr) {
        if (auto* meta = engine.getClassDefine(std::type_index(*dynamicType))) {
            return {dynamicPtr, meta, true}; // 完美命中子类
        }
    }

    // 3. Fallback 回退到静态类型 (Original)
    std::type_index staticIdx(typeid(T));
    auto*           staticMeta = engine.getClassDefine(staticIdx);
    if (!staticMeta) {
        throw Exception("Class not registered: " + std::string(staticIdx.name()));
    }

    return {static_cast<const void*>(value), staticMeta, false};
}

} // namespace detail

} // namespace v8kit::binding::traits