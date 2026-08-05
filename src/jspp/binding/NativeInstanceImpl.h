#pragma once
#include "jspp/core/MetaInfo.h"
#include "jspp/core/NativeInstance.h"
#include "jspp/core/Trampoline.h"

#include "ReturnValuePolicy.h"
#include "traits/Polymorphic.h"
#include "traits/TypeTraits.h"

#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeindex>


namespace jspp::binding {

/**
 * Owns pointer ownership and manages the lifecycle of large objects (internally uses secondary heap allocation)
 *
 * @tparam The actual C++ type (e.g., MyClass)
 * @tparam The actual way the Holder is held (e.g., T*, std::unique_ptr<T>, std::shared_ptr<T>)
 */
template <typename T, typename Holder>
class PointerNativeInstance final : public NativeInstance {
public:
    using ElementType = typename std::pointer_traits<Holder>::element_type;

    Holder value_;
    void*  most_derived_ptr_;

    explicit PointerNativeInstance(ClassMeta const* meta, Holder value, void* most_derived_ptr)
    : NativeInstance(meta),
      value_(std::move(value)),
      most_derived_ptr_(most_derived_ptr) {}

    ~PointerNativeInstance() override = default;

    std::type_index type_id() const override { return std::type_index(typeid(std::remove_cv_t<ElementType>)); }

    enable_trampoline* get_trampoline() const override {
        if constexpr (std::is_base_of_v<enable_trampoline, ElementType> && std::is_polymorphic_v<ElementType>) {
            return static_cast<enable_trampoline*>(get_raw_ptr());
        } else {
            return nullptr;
        }
    }

    bool is_expired() const override {
        if constexpr (std::is_pointer_v<Holder>) {
            return value_ == nullptr;
        } else {
            return !value_;
        }
    }

    void invalidate() override {
        if constexpr (traits::is_std_smart_pointer<Holder>) {
            value_.reset();
        } else {
            value_ = nullptr;
        }
    }

    bool is_const() const override { return std::is_const_v<ElementType>; }

    void* release_ownership() override {
        if constexpr (traits::is_unique_ptr_v<Holder>) {
            // Explicitly discard const, with the outer TypeConverter responsible for safety checks
            return const_cast<void*>(static_cast<const void*>(value_.release()));
        } else {
            return nullptr; // shared_ptr or raw_ptr is not allowed to release ownership
        }
    }

    void* cast(std::type_index target_type) const override {
        // If the requested type happens to be the statically declared type held by
        // the smart pointer (for example, Base2). Directly return the underlying raw pointer,
        // because the smart pointer knows its exact Base2 address and does not need to calculate it.
        if (target_type == std::type_index(typeid(std::remove_cv_t<ElementType>))) {
            return const_cast<void*>(static_cast<const void*>(get_raw_ptr()));
        }

        // If the request is for the polymorphic real type (Derived) or another base class (Base1)
        // It is necessary to start from the polymorphic base address (most_derived_ptr_) and use
        //  the Meta chain to safely perform C castTo offset
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
            if (auto* raw = get_raw_ptr()) {
                return std::make_unique<PointerNativeInstance<ElementType, std::unique_ptr<ElementType>>>(
                    meta_,
                    std::make_unique<ElementType>(*raw),
                    most_derived_ptr_
                );
            }
        }
        [[unlikely]] throw std::logic_error("Object is not copy constructible");
    }

    bool is_owned() const override { return traits::is_unique_ptr_v<Holder> || traits::is_shared_ptr_v<Holder>; }

private:
    ElementType* get_raw_ptr() const {
        if constexpr (std::is_pointer_v<Holder>) {
            return value_;
        } else {
            return value_.get();
        }
    }
};


static constexpr size_t kInlineSizeThreshold = 64; // bytes

template <typename T>
constexpr bool isInlineOptimizable_v = !std::is_pointer_v<T> &&           //
                                       !std::is_reference_v<T> &&         //
                                       !std::is_polymorphic_v<T> &&       //
                                       std::is_copy_constructible_v<T> && //
                                       std::is_move_constructible_v<T> && //
                                       std::is_destructible_v<T> &&       //
                                       (sizeof(T) <= kInlineSizeThreshold);


/**
 * Small object optimization, inline allocation of heap memory,
 * reducing the overhead of secondary heap memory allocation and memory fragmentation
 */
template <typename T>
class ValueNativeInstance final : public NativeInstance {
public:
    static_assert(isInlineOptimizable_v<T>, "ValueNativeInstance only supports small copyable types");

    T value_;

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit ValueNativeInstance(ClassMeta const* meta, Args&&... args)
    : NativeInstance(meta),
      value_(std::forward<Args>(args)...) {}

    explicit ValueNativeInstance(ClassMeta const* meta, T&& mv) : NativeInstance(meta), value_(std::move(mv)) {}

    ~ValueNativeInstance() override = default;

    std::type_index type_id() const override { return std::type_index(typeid(std::remove_cv_t<T>)); }
    bool            is_expired() const override { return false; }
    void            invalidate() override { /* Value types cannot expire prematurely */ }
    bool            is_const() const override { return std::is_const_v<T>; }
    bool            is_owned() const override { return true; }

    void* cast(std::type_index target_type) const override {
        void* self_ptr = const_cast<void*>(static_cast<const void*>(&value_));

        if (target_type == std::type_index(typeid(std::remove_cv_t<T>))) {
            return self_ptr;
        }

        if (meta_) {
            return meta_->castTo(self_ptr, target_type);
        }
        return nullptr;
    }

    void* release_ownership() override {
        // Value types are not allowed to release control, otherwise memory will be dangling
        return nullptr;
    }

    std::unique_ptr<NativeInstance> clone() const override {
        return std::make_unique<ValueNativeInstance<T>>(meta_, value_);
    }
};

namespace traits::detail {

template <typename U>
struct ElementTypeExtractor {
    using NoRef = std::remove_reference_t<U>;   // const T& -> const T
    using type  = std::remove_pointer_t<NoRef>; // const T* -> const T
};

template <typename U>
    requires traits::is_unique_ptr_v<U> || traits::is_shared_ptr_v<U>
struct ElementTypeExtractor<U> {
    using type = typename std::remove_reference_t<U>::element_type;
};

} // namespace traits::detail

namespace factory {


/**
 * @brief Generate the underlying NativeInstance instance based on the given
 *        C++ value, strategy, and the type after resolution
 */
template <typename T>
std::unique_ptr<NativeInstance>
createNativeInstance(T&& value, ReturnValuePolicy policy, traits::detail::ResolvedCastSource const& resolved) {
    using BaseT       = std::remove_reference_t<T>;
    using ElementType = typename traits::detail::ElementTypeExtractor<T>::type;
    // 拷贝/移动创建独立副本：副本不继承源的 const 语义（const 是源对象的属性），
    // 引用类策略（kReference 系列）仍保留 const。
    using CopyElementT = std::remove_const_t<ElementType>;

    // Helper creator, automatically deduces Holder type
    auto createImpl = [&](auto&& holder) {
        using HolderT      = std::decay_t<decltype(holder)>;
        using InstElementT = typename std::pointer_traits<HolderT>::element_type;
        return std::make_unique<PointerNativeInstance<InstElementT, HolderT>>(
            resolved.meta,
            std::forward<decltype(holder)>(holder),
            const_cast<void*>(resolved.ptr)
        );
    };

    // ----------------
    // smart pointer
    // ----------------
    if constexpr (traits::is_unique_ptr_v<BaseT>) {
        if (policy == ReturnValuePolicy::kCopy) [[unlikely]] {
            throw std::logic_error("Cannot copy unique_ptr");
        }
        return createImpl(std::forward<T>(value));

    } else if constexpr (traits::is_shared_ptr_v<BaseT>) {
        return createImpl(std::forward<T>(value));

    } else { // raw pointer
        // Extract raw pointers for constructing Holder
        ElementType* rawPtr = nullptr;
        if constexpr (std::is_pointer_v<BaseT>) {
            rawPtr = value;
            if (!rawPtr) return nullptr;
        } else {
            rawPtr = &value;
        }

        // Handle the ownership and lifecycle of objects according to the strategy
        switch (policy) {
        case ReturnValuePolicy::kCopy:
            if (resolved.is_downcasted) {
                auto copy = resolved.meta->instanceMeta_.copyCloneCtor_;
                if (!copy) [[unlikely]] {
                    throw std::logic_error("Polymorphic type '" + resolved.meta->name_ + "' is not copy constructible");
                }
                void* cloned = copy(resolved.ptr);
                // Offset Derived* back to Base* (ElementType*)
                void* base = resolved.meta->castTo(cloned, typeid(ElementType));
                if (!base) [[unlikely]] {
                    if (auto dtor = resolved.meta->instanceMeta_.cloneDestructor_) {
                        dtor(cloned); // free the cloned object
                    }
                    throw std::logic_error("Failed to upcast cloned polymorphic object to base type");
                }
                ElementType* finalPtr = static_cast<ElementType*>(base);
                // cloned 来自 copyCloneCtor（new T，可变），const_cast 安全
                return createImpl(std::unique_ptr<CopyElementT>(const_cast<CopyElementT*>(finalPtr)));
            }
            // Non-polymorphic type
            if constexpr (isInlineOptimizable_v<CopyElementT>) {
                return std::make_unique<ValueNativeInstance<CopyElementT>>(resolved.meta, *rawPtr); // SOO
            } else if constexpr (std::is_copy_constructible_v<CopyElementT>) {
                return createImpl(std::make_unique<CopyElementT>(*rawPtr));
            } else {
                [[unlikely]] throw std::logic_error("Object is not copy constructible");
            }

        case ReturnValuePolicy::kMove:
            if (resolved.is_downcasted) {
                auto move = resolved.meta->instanceMeta_.moveCloneCtor_;
                if (!move) [[unlikely]] {
                    throw std::logic_error("Polymorphic type '" + resolved.meta->name_ + "' is not move constructible");
                }
                void* cloned = move(const_cast<void*>(resolved.ptr));
                // Offset Derived* back to Base* (ElementType*)
                void* base = resolved.meta->castTo(cloned, typeid(ElementType));
                if (!base) [[unlikely]] {
                    if (auto dtor = resolved.meta->instanceMeta_.cloneDestructor_) {
                        dtor(cloned); // free the cloned object
                    }
                    throw std::logic_error("Failed to upcast cloned polymorphic object to base type");
                }
                ElementType* finalPtr = static_cast<ElementType*>(base);
                return createImpl(std::unique_ptr<CopyElementT>(const_cast<CopyElementT*>(finalPtr)));
            }
            // Non-polymorphic type
            if constexpr (isInlineOptimizable_v<CopyElementT>) {
                return std::make_unique<ValueNativeInstance<CopyElementT>>(resolved.meta, std::move(*rawPtr)); // SOO
            } else if constexpr (std::is_move_constructible_v<CopyElementT>) {
                return createImpl(std::make_unique<CopyElementT>(std::move(*rawPtr)));
            } else {
                [[unlikely]] throw std::logic_error("Object is not move constructible");
            }

        case ReturnValuePolicy::kTakeOwnership:
            if constexpr (std::is_pointer_v<BaseT>) {
                return createImpl(std::unique_ptr<ElementType>(value));
            } else {
                [[unlikely]] throw std::logic_error("Cannot take ownership of non-pointer");
            }

        case ReturnValuePolicy::kReference:
        case ReturnValuePolicy::kReferenceInternal:
        case ReturnValuePolicy::kReferencePersistent:
        case ReturnValuePolicy::kReferenceInternalPersistent: {
            // 瞬态作用域跟踪在 TypeConverter::toJs 中进行（需要 wrapper 引用以保活，
            // 并需要 parent 信息以判定"从瞬态根可达"）
            return createImpl(rawPtr);
        }

        default:
            [[unlikely]] throw std::logic_error("Unknown return value policy");
        }
    }
}

template <typename T>
std::unique_ptr<NativeInstance> wrapNativeInstance(std::unique_ptr<T>&& inst) {
    auto resolve = traits::detail::resolveCastSource(inst.get());
    // For smart pointers, the ReturnValuePolicy here has no actual effect.
    return createNativeInstance(std::move(inst), ReturnValuePolicy::kAutomatic, resolve);
}

template <typename T, typename... Args>
std::unique_ptr<NativeInstance> newNativeInstance(Args&&... args)
    requires std::constructible_from<T, Args...>
{
    if constexpr (isInlineOptimizable_v<T>) {
        // SOO Optimization
        T*   unused  = nullptr;
        auto resolve = traits::detail::resolveCastSource<T>(unused);
        return std::make_unique<ValueNativeInstance<T>>(resolve.meta, std::forward<Args>(args)...);
    } else {
        auto inst = std::make_unique<T>(std::forward<Args>(args)...);
        return wrapNativeInstance(std::move(inst));
    }
}


} // namespace factory


} // namespace jspp::binding