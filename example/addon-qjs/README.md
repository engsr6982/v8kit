# Example Program: QuickJS Addon

> ⚠️ **Warning**: QuickJS Addon (Native Module) mode is **unsafe** for stateful bindings. QuickJS native C modules have no cleanup hook that runs while the `JSContext` is still usable — context-scoped resources held by the addon (stored callbacks, registered classes, `Global<T>`-style references) are never released until `JS_FreeRuntime`'s GC, and leak entirely if the addon still references any JSValue from C++ (debug builds assert, release builds leak silently). This example is **experimental — for testing only**; it works in short-lived hosts (like the `qjs` CLI) where process exit reclaims everything. See [quickjs-ng/quickjs#1703](https://github.com/quickjs-ng/quickjs/issues/1703).

## Build

```bash
cmake -B build -S .

cmake --build build --config Debug --target ALL_BUILD
```

After the build is complete, you need to copy the build artifacts `qjs.exe`, `example-addon-qjs.so/dll` to the current directory.

Make sure the path structure is as follows:

```
/jspp/example/addon-qjs/
    CMakeLists.txt
    example.js
    example.cc

    qjs.exe
    example-addon-qjs.so/dll
```

## Run

```bash
./qjs.exe -m ./example.js
```

## Note

Make sure `QjsInitializeFlags` is set correctly when creating the engine.

If the target host is QuickJS embedded in another project, be sure to ensure ABI compatibility.
