#include "QjsEngine.h"
#include "QjsHelper.h"
#include "jspp/Macro.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>


JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

#include <string_view>
#include <utility>


namespace jspp {


Local<Null> Null::newNull() { return Local<Null>{JS_NULL}; }


Local<Undefined> Undefined::newUndefined() { return Local<Undefined>{JS_UNDEFINED}; }


Local<Boolean> Boolean::newBoolean(bool b) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<Boolean>{JS_NewBool(ctx, b)};
}


Local<Number> Number::newNumber(double d) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<Number>{JS_NewFloat64(ctx, d)};
}
Local<Number> Number::newNumber(int i) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<Number>{JS_NewInt32(ctx, i)};
}
Local<Number> Number::newNumber(float f) { return newNumber(static_cast<double>(f)); }


Local<BigInt> BigInt::newBigInt(int64_t i) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<BigInt>{JS_NewBigInt64(ctx, i)};
}
Local<BigInt> BigInt::newBigIntUnsigned(uint64_t i) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<BigInt>{JS_NewBigUint64(ctx, i)};
}


Local<String> String::newString(const char* str) { return newString(std::string_view{str}); }
Local<String> String::newString(std::string const& str) { return newString(std::string_view{str}); }
Local<String> String::newString(std::string_view sv) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();

    auto str = JS_NewStringLen(ctx, sv.data(), sv.size());
    qjs_backend::QjsHelper::rethrowException(str);

    return Local<String>{str};
}

Local<Symbol> Symbol::newSymbol() {
    // The Symbol API of QuickJs-NG does not support Symbols without descriptions
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
#if QJS_VERSION_MAJOR > 0 || (QJS_VERSION_MAJOR == 0 && QJS_VERSION_MINOR >= 15)
    // v0.15.0+ fast path (PR #1447) — safe to pass NULL
    auto sym = JS_NewSymbol(ctx, nullptr, false);
    qjs_backend::QjsHelper::rethrowException(sym);
    return Local<Symbol>{sym};
#else
    // fallback
    // TODO: PR #1447 has been merged into the QuickJs-NG mainline.
    // This fallback code will be removed after the release of v0.15.0.
    // https://github.com/quickjs-ng/quickjs/pull/1447
    auto global  = JS_GetGlobalObject(ctx);
    auto symCtor = JS_GetPropertyStr(ctx, global, "Symbol");
    JS_FreeValue(ctx, global);
    qjs_backend::QjsHelper::rethrowException(symCtor);

    auto sym = JS_Call(ctx, symCtor, JS_UNDEFINED, 0, nullptr);
    JS_FreeValue(ctx, symCtor);
    qjs_backend::QjsHelper::rethrowException(sym);
    EngineScope::currentEngineChecked().pumpPendingJobs();
    return Local<Symbol>{sym};
#endif
}
Local<Symbol> Symbol::newSymbol(std::string_view description) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    // is_global = true => Symbol.for()
    auto sym = JS_NewSymbol(ctx, description.data(), false);
    qjs_backend::QjsHelper::rethrowException(sym);
    return Local<Symbol>{sym};
}
Local<Symbol> Symbol::newSymbol(const char* description) { return newSymbol(std::string_view{description}); }
Local<Symbol> Symbol::newSymbol(std::string const& description) { return newSymbol(std::string_view{description}); }

Local<Symbol> Symbol::forKey(Local<String> const& str) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    // Since quickjs-ng does not export the JS_NewSymbolFromAtom interface,
    //  we can only fall back to the upper-level interface here.
    // This results in meaningless conversion overhead.
    // TODO: To be optimized.
    auto stdStr = str.getValue();
    auto sym    = JS_NewSymbol(ctx, stdStr.data(), true);
    qjs_backend::QjsHelper::rethrowException(sym);
    return Local<Symbol>{sym};
}


Local<Function> Function::newFunction(FunctionCallback&& cb) {
    auto ptr = std::make_unique<FunctionCallback>(std::move(cb));

    auto& engine = EngineScope::currentEngineChecked();

    auto data = JS_NewObjectClass(engine.context_, engine.functionDataClassId_);
    qjs_backend::QjsHelper::rethrowException(data);
    JS_SetOpaque(data, ptr.release());

    auto engineOpaque = engine.newPointerData(&engine);

    auto args = std::array{engineOpaque, data};

    auto func = JS_NewCFunctionData(
        engine.context_,
        [](JSContext* ctx, JSValueConst thiz, int argc, JSValueConst* argv, int /* magic */, JSValue* data) -> JSValue {
            auto externalPointerID = JS_GetClassID(data[0]);
            assert(externalPointerID != JS_INVALID_CLASS_ID);

            auto engine = static_cast<Engine*>(JS_GetOpaque(data[0], externalPointerID));
            if (externalPointerID != engine->pointerDataClassId_) [[unlikely]] {
                JS_ThrowTypeError(ctx, "Invalid engine pointer data");
                return JS_EXCEPTION;
            }

            auto callback = static_cast<FunctionCallback*>(JS_GetOpaque(data[1], engine->functionDataClassId_));

            EngineScope tracker{engine}; // for addon
            try {
                auto args   = qjs_backend::QjsHelper::wrapArguments(engine, thiz, argc, argv);
                auto result = (*callback)(args);
                return qjs_backend::QjsHelper::getDupLocal(result);
            } catch (Exception const& e) {
                return qjs_backend::QjsHelper::rethrowToScript(e);
            } catch (std::exception const& e) {
                return qjs_backend::QjsHelper::rethrowToScript(e, engine);
            } catch (...) {
                JS_ThrowPlainError(engine->context_, "Unknown C++ exception occurred");
                return JS_EXCEPTION;
            }
        },
        0,
        0,
        args.size(),
        args.data()
    );
    for (auto& arg : args) JS_FreeValue(engine.context_, arg);
    qjs_backend::QjsHelper::rethrowException(func);
    return Local<Function>{func};
}


Local<Object> Object::newObject() {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto obj = JS_NewObject(ctx);
    qjs_backend::QjsHelper::rethrowException(obj);
    return Local<Object>{obj};
}


Local<Array> Array::newArray(size_t length) {
    auto ctx   = qjs_backend::QjsHelper::currentContextChecked();
    auto array = JS_NewArray(ctx);
    qjs_backend::QjsHelper::rethrowException(array);
    if (length > 0) {
        auto code = JS_SetLength(ctx, array, static_cast<int64_t>(length));
        if (code < 0) [[unlikely]] {
            JS_FreeValue(ctx, array);
            qjs_backend::QjsHelper::rethrowException(code);
        }
    }
    return Local<Array>{array};
}


Arguments::Arguments(BackendImpl impl) : impl_(std::move(impl)) {}

Engine* Arguments::runtime() const { return impl_.engine_; }

bool Arguments::hasThiz() const { return JS_IsObject(impl_.thiz_); }

Local<Object> Arguments::thiz() const {
    if (!hasThiz()) {
        throw Exception{"Arguments::thiz(): no thiz"};
    }
    return Local<Object>{qjs_backend::QjsHelper::dupValue(impl_.thiz_, impl_.engine_->context_)};
}

size_t Arguments::length() const { return static_cast<size_t>(impl_.argc_); }

Local<Value> Arguments::operator[](size_t index) const {
    if (index >= length()) {
        return {}; // undefined
    }
    return Local<Value>{qjs_backend::QjsHelper::dupValue(impl_.argv_[index], impl_.engine_->context_)};
}


} // namespace jspp