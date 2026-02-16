add_rules("mode.debug", "mode.release")

if has_config("test") then
    add_requires("catch2 v3.8.1")
end

if is_plat("windows") then
    if not has_config("vs_runtime") and has_config("test") then
        set_runtimes("MT")
    end
end

option("v8_include_dir")
    set_default("")
    set_showmenu(true)
    set_description("Path to V8 include directory (e.g., /usr/local/include/v8)")
option_end()

option("test")
    set_default(false)
    set_showmenu(true)
    set_description("Build test cases")
option_end()

option("v8_static_lib")
    set_default("")
    set_showmenu(true)
    set_description("Path to V8 static library (e.g., /usr/local/lib/libv8_base.a)")
option_end()


target("v8kit")
    set_kind("static")
    add_files("src/**.cc")
    add_includedirs("src")
    add_headerfiles("src/(v8kit/**.h)", "src/(v8kit/**.inl)")
    set_languages("cxx20")
    set_symbols("debug")

    set_toolchains("clang-cl")

    if has_config("test") then 
        set_kind("binary")
        add_packages("catch2")
        add_files("test/**.cc")
        add_syslinks("winmm", "advapi32", "dbghelp") -- add required system libraries
        add_links(get_config("v8_static_lib"))
    end

    if is_plat("windows") then 
        add_cxflags("/utf-8", "/W4", "/sdl")
        add_cxxflags("/Zc:__cplusplus", {force = true})
    elseif is_plat("linux") then
        add_cxflags("-fPIC", "-stdlib=libc++", {force = true})
        add_syslinks("dl", "pthread")
    end

    if has_config("v8_include_dir") then
        local v8_inc = get_config("v8_include_dir")
        if v8_inc ~= "" and os.isdir(v8_inc) then
            add_includedirs(v8_inc)
            print("V8 include path added: " .. v8_inc)
        end
    end

    if is_mode("debug") then
        add_defines("V8KIT_DEBUG")
    end

    after_build(function (target)
        local binDir = os.projectdir() .. "/bin"
        if not os.isdir(binDir) then
            os.mkdir(binDir)
        end
        local test = target:targetfile()
        os.cp(test, binDir)
    end)
target_end()