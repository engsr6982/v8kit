message(STATUS "[jspp] Configuring V8 Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_V8)

# set source and include directory
file(GLOB_RECURSE V8_SRC "src-v8/*.cc")
target_sources(jspp PRIVATE ${V8_SRC})
target_include_directories(jspp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src-v8>"
    "$<INSTALL_INTERFACE:include>")
