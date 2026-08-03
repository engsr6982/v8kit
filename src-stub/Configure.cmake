message(STATUS "[jspp] Configuring Stub Backend")

target_compile_definitions(jspp PUBLIC JSPP_BACKEND_STUB)

# set source and include directory
file(GLOB_RECURSE STUB_SRC "src-stub/*.cc")
target_sources(jspp PRIVATE ${STUB_SRC})
target_include_directories(jspp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src-stub>"
    "$<INSTALL_INTERFACE:include>")

# TODO: Stub backend, if need to use external library, add here
# external include
# if(JSPP_EXTERNAL_INC)
#     target_include_directories(jspp PUBLIC ${JSPP_EXTERNAL_INC})
# endif()
# if(JSPP_EXTERNAL_LIB)
#     target_link_libraries(jspp PUBLIC ${JSPP_EXTERNAL_LIB})
# endif()
