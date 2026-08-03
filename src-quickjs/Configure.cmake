message(STATUS "[jspp] Configuring QuickJS Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_QUICKJS)

# set source and include directory
file(GLOB_RECURSE QUICKJS_SRC "src-quickjs/*.cc")
target_sources(jspp PRIVATE ${QUICKJS_SRC})
target_include_directories(jspp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src-quickjs>"
    "$<INSTALL_INTERFACE:include>")

if(UNIX AND NOT APPLE)
    target_link_libraries(jspp PUBLIC dl m)
endif()