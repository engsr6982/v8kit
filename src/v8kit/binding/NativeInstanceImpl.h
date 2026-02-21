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
    // 从 Holder 推导元素类型 (处理 T* vs shared_ptr<T> 的区别)
    using ElementType = typename std::pointer_traits<
        typename std::conditional<std::is_pointer_v<Holder>, Holder, typename Holder::element_type*>::type>::
        element_type;

    Holder value_;

    explicit NativeInstanceImpl(ClassMeta const* meta, Holder value) : NativeInstance(meta), value_(std::move(value)) {}

    ~NativeInstanceImpl() override = default;


    std::type_index type_id() const override { return std::type_index(typeid(std::remove_cv_t<ElementType>)); }

    bool is_const() const override { return std::is_const_v<ElementType>; }

    void* cast(std::type_index target_type) const override {
        ElementType* ptr = get_raw_ptr();
        if (!ptr) return nullptr;

        if (target_type == std::type_index(typeid(std::remove_cv_t<ElementType>))) {
            return const_cast<void*>(static_cast<const void*>(ptr));
        }

        // 2. 继承路径：利用 ClassMeta 链进行安全的指针偏移
        // 注意：这里传入的是 ptr (T*)，meta_->castTo 会利用 upcaster 一层层转上去
        // 即使 ptr 是多重继承的中间部分，upcaster 也能把它修成 Base*
        if (meta_) {
            // 必须将 T* 转为 void* 传入，假设 Meta 注册的是 T 的 Meta
            // 这里有个前提：meta_ 必须对应 T。如果 T 是多态获取的 Derived，meta_ 是 DerivedMeta。
            return meta_->castTo(static_cast<void*>(ptr), target_type);
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
                    std::make_unique<ElementType>(*raw)
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

    // 提取裸指针用于构建 Holder
    ElementType* rawPtr = nullptr;
    if constexpr (std::is_pointer_v<RawType>) {
        rawPtr = value;
        if (!rawPtr) return nullptr;
    } else if constexpr (traits::is_unique_ptr_v<RawType> || traits::is_shared_ptr_v<RawType>) {
        rawPtr = value.get();
        if (!rawPtr) return nullptr;
    } else {
        rawPtr = &value;
    }

    // 辅助创建器，自动推导 Holder 类型
    auto createImpl = [&](auto&& holder) {
        using HolderT = std::decay_t<decltype(holder)>;
        // NativeInstanceImpl 的 is_owned 已经根据 HolderT (is_unique_ptr_v) 实现了自动推导
        return std::make_unique<NativeInstanceImpl<ElementType, HolderT>>(
            resolved.meta,
            std::forward<decltype(holder)>(holder)
        );
    };

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