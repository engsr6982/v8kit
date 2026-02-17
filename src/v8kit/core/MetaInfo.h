#pragma once
#include "Fwd.h"

#include <string>
#include <typeindex>

namespace v8kit {


struct StaticMemberMeta {
    struct Property {
        std::string const    name_;
        GetterCallback const getter_;
        SetterCallback const setter_;

        explicit Property(std::string name, GetterCallback getter, SetterCallback setter)
        : name_(std::move(name)),
          getter_(std::move(getter)),
          setter_(std::move(setter)) {}
    };
    struct Function {
        std::string const      name_;
        FunctionCallback const callback_;

        explicit Function(std::string name, FunctionCallback callback)
        : name_(std::move(name)),
          callback_(std::move(callback)) {}
    };

    std::vector<Property> const property_;
    std::vector<Function> const functions_;

    explicit StaticMemberMeta(std::vector<Property> property, std::vector<Function> functions)
    : property_(std::move(property)),
      functions_(std::move(functions)) {}
};

struct InstanceMemberMeta {
    struct Property {
        std::string const            name_;
        InstanceGetterCallback const getter_;
        InstanceSetterCallback const setter_;

        explicit Property(std::string name, InstanceGetterCallback getter, InstanceSetterCallback setter)
        : name_(std::move(name)),
          getter_(std::move(getter)),
          setter_(std::move(setter)) {}
    };
    struct Method {
        std::string const            name_;
        InstanceMethodCallback const callback_;

        explicit Method(std::string name, InstanceMethodCallback callback)
        : name_(std::move(name)),
          callback_(std::move(callback)) {}
    };

    ConstructorCallback const   constructor_;
    std::vector<Property> const property_;
    std::vector<Method> const   methods_;
    size_t const                classSize_{0}; // sizeof(C) for instance class

    // script helper
    using InstanceEqualsCallback = bool (*)(void* lhs, void* rhs);
    InstanceEqualsCallback const equals_{nullptr};

    explicit InstanceMemberMeta(
        ConstructorCallback    constructor,
        std::vector<Property>  property,
        std::vector<Method>    functions,
        size_t                 classSize,
        InstanceEqualsCallback equals
    )
    : constructor_(std::move(constructor)),
      property_(std::move(property)),
      methods_(std::move(functions)),
      classSize_(classSize),
      equals_(equals) {}
};

struct ClassMeta {
    std::string const        name_;
    StaticMemberMeta const   staticMeta_;
    InstanceMemberMeta const instanceMeta_;
    ClassMeta const*         base_;
    std::type_index const    typeId_;

    [[nodiscard]] inline bool hasConstructor() const { return instanceMeta_.constructor_ != nullptr; }

    [[nodiscard]] inline bool isA(std::type_index typeIdx, bool recursion = true) const {
        if (typeId_ == typeIdx) return true;
        if (!recursion || !base_) return false;
        ClassMeta const* curr = base_;
        while (curr != nullptr) {
            if (curr->typeId_ == typeIdx) return true;
            curr = curr->base_;
        }
        return false;
    }

    [[nodiscard]] inline bool isA(ClassMeta const& meta, bool recursion = true) const {
        return isA(meta.typeId_, recursion);
    }

    template <typename T>
    [[nodiscard]] inline bool isA(bool recursion = true) const {
        return isA(std::type_index(typeid(T)), recursion);
    }

    explicit ClassMeta(
        std::string        name,
        StaticMemberMeta   staticMeta,
        InstanceMemberMeta instanceMeta,
        ClassMeta const*   base,
        std::type_index    typeId
    )
    : name_(std::move(name)),
      staticMeta_(std::move(staticMeta)),
      instanceMeta_(std::move(instanceMeta)),
      base_(base),
      typeId_(typeId) {}
};

struct EnumMeta {
public:
    struct Entry {
        std::string const name_;
        int64_t const     value_;

        explicit Entry(std::string name, int64_t value) : name_(std::move(name)), value_(value) {}
    };

    std::string const        name_;
    std::vector<Entry> const entries_;

    explicit EnumMeta(std::string name, std::vector<Entry> entries)
    : name_(std::move(name)),
      entries_(std::move(entries)) {}
};


} // namespace v8kit