#pragma once
#include "v8kit/core/Exception.h"
#include "v8kit/core/MetaInfo.h"
#include "v8kit/core/NativeInstance.h"
#include "v8kit/core/Reference.h"
#include "v8kit/core/Value.h"

#include "ReturnValuePolicy.h"
#include "traits/Polymorphic.h"
#include "traits/TypeTraits.h"

#include <memory>
#include <type_traits>
#include <typeindex>


namespace v8kit::binding {

/**
 * @brief 具体类型的实例持有者
 *
 * @tparam T 实际的 C++ 类型 (e.g., MyClass)
 * @tparam Holder 实际的持有方式 (e.g., T*, std::unique_ptr<T>, std::shared_ptr<T>)
 */
template <typename T, typename Holder>
class NativeInstanceImpl final : public NativeInstance {
public:
    using ElementType = typename std::pointer_traits<
        typename std::conditional<std::is_pointer_v<Holder>, Holder, typename Holder::element_type*>::type>::
        element_type;

    Holder value_;
    void*  most_derived_ptr_;

    explicit NativeInstanceImpl(ClassMeta const* meta, Holder value, void* most_derived_ptr)
    : NativeInstance(meta),
      value_(std::move(value)),
      most_derived_ptr_(most_derived_ptr) {}

    ~NativeInstanceImpl() override = default;


    std::type_index type_id() const override { return std::type_index(typeid(std::remove_cv_t<ElementType>)); }

    bool is_const() const override { return std::is_const_v<ElementType>; }

    void* cast(std::type_index target_type) const override {
        // 如果请求的类型恰好是智能指针持有的静态声明类型 (例如 Base2)
        // 直接返回底层的裸指针，因为智能指针知道自己准确的 Base2 地址，无需计算
        if (target_type == std::type_index(typeid(std::remove_cv_t<ElementType>))) {
            return const_cast<void*>(static_cast<const void*>(get_raw_ptr()));
        }

        // 如果请求的是多态真实类型 (Derived) 或其他基类 (Base1)
        // 必须从多态首地址 (most_derived_ptr_) 出发，利用 Meta 链条进行安全的 C++ castTo 偏移
        if (meta_) {
            return meta_->castTo(most_derived_ptr_, target_type);
        }
        return nullptr;
    }

    std::shared_ptr<void> get_shared_ptr() const override {
        if constexpr (traits::is_shared_ptr_v<Holder>) {
            return value_;
        }
        return nullptr;
    }

    std::unique_ptr<NativeInstance> clone() const override {
        if constexpr (std::is_copy_constructible_v<ElementType>) {
            auto* raw = get_raw_ptr();
            if (raw) {
                return std::make_unique<NativeInstanceImpl<ElementType, std::unique_ptr<ElementType>>>(
                    meta_,
                    std::make_unique<ElementType>(*raw),
                    most_derived_ptr_
                );
            }
        }
        throw Exception("Object is not copy constructible");
    }

    bool is_owned() const override { return traits::is_unique_ptr_v<Holder>; }

private:
    ElementType* get_raw_ptr() const {
        if constexpr (std::is_pointer_v<Holder>) {
            return value_;
        } else {
            return value_.get();
        }
    }
};


namespace traits::detail {
// 专门用于提取 裸指针、值、以及智能指针的底层元素类型
template <typename U>
struct ElementTypeExtractor {
    using type = std::remove_pointer_t<U>;
};

template <typename U>
    requires traits::is_unique_ptr_v<U> || traits::is_shared_ptr_v<U>
struct ElementTypeExtractor<U> {
    using type = typename U::element_type;
};
} // namespace traits::detail

namespace factory {


/**
 * @brief 根据给定的 C++ 值、策略和决议后的类型，生成底层的 NativeInstance 实例
 */
template <typename T>
std::unique_ptr<NativeInstance>
createNativeInstance(T&& value, ReturnValuePolicy policy, traits::detail::ResolvedCastSource const& resolved) {
    using RawType     = std::decay_t<T>;
    using ElementType = typename traits::detail::ElementTypeExtractor<RawType>::type;

    // 辅助创建器，自动推导 Holder 类型
    auto createImpl = [&](auto&& holder) {
        using HolderT = std::decay_t<decltype(holder)>;
        return std::make_unique<NativeInstanceImpl<ElementType, HolderT>>(
            resolved.meta,
            std::forward<decltype(holder)>(holder),
            const_cast<void*>(resolved.ptr)
        );
    };

    // ----------------
    // smart pointer
    // ----------------
    if constexpr (traits::is_unique_ptr_v<RawType>) {
        if (policy == ReturnValuePolicy::kCopy) {
            throw Exception("Cannot copy unique_ptr");
        }
        return createImpl(std::forward<T>(value));
    } else if constexpr (traits::is_shared_ptr_v<RawType>) {
        return createImpl(std::forward<T>(value));
    }


    // ----------------
    // raw pointer
    // ----------------

    // 提取裸指针用于构建 Holder
    ElementType* rawPtr = nullptr;
    if constexpr (std::is_pointer_v<RawType>) {
        rawPtr = value;
        if (!rawPtr) return nullptr;
    } else {
        rawPtr = &value;
    }

    // 根据策略处理对象的所有权和生命周期
    switch (policy) {
    case ReturnValuePolicy::kCopy:
        if (resolved.is_downcasted) {
            auto copy = resolved.meta->instanceMeta_.copyCloneCtor_;
            if (!copy) {
                throw Exception("Polymorphic type '" + resolved.meta->name_ + "' is not copy constructible");
            }
            void* cloned = copy(resolved.ptr);

            // 将 Derived* 偏移回 Base* (ElementType*)
            void* base = resolved.meta->castTo(cloned, typeid(ElementType));
            if (!base) {
                throw Exception("Failed to upcast cloned polymorphic object to base type");
            }
            ElementType* finalPtr = static_cast<ElementType*>(base);
            return createImpl(std::unique_ptr<ElementType>(finalPtr));
        }
        // 非多态，普通拷贝
        if constexpr (std::is_copy_constructible_v<ElementType>) {
            return createImpl(std::make_unique<ElementType>(*rawPtr));
        } else {
            throw Exception("Object is not copy constructible");
        }

    case ReturnValuePolicy::kMove:
        if (resolved.is_downcasted) {
            auto copy = resolved.meta->instanceMeta_.moveCloneCtor_;
            if (!copy) {
                throw Exception("Polymorphic type '" + resolved.meta->name_ + "' is not move constructible");
            }
            void* cloned = copy(const_cast<void*>(resolved.ptr));
            // 将 Derived* 偏移回 Base* (ElementType*)
            void* base = resolved.meta->castTo(cloned, typeid(ElementType));
            if (!base) {
                throw Exception("Failed to upcast cloned polymorphic object to base type");
            }
            ElementType* finalPtr = static_cast<ElementType*>(base);
            return createImpl(std::unique_ptr<ElementType>(finalPtr));
        }

        if constexpr (std::is_move_constructible_v<ElementType>) {
            return createImpl(std::make_unique<ElementType>(std::move(*rawPtr)));
        } else {
            throw Exception("Object is not move constructible");
        }

    case ReturnValuePolicy::kTakeOwnership:
        if constexpr (std::is_pointer_v<RawType>) {
            return createImpl(std::unique_ptr<ElementType>(value));
        } else {
            throw Exception("Cannot take ownership of non-pointer");
        }

    case ReturnValuePolicy::kReference:
    case ReturnValuePolicy::kReferenceInternal:
        // 引用持有裸指针 (takeOwnership = false)
        return createImpl(rawPtr);

    default:
        [[unlikely]] throw Exception("Unknown return value policy");
    }
}


} // namespace factory


} // namespace v8kit::binding