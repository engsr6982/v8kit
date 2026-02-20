#pragma once
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

} // namespace v8kit::binding::traits