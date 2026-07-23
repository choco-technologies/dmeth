set(DMOD_MODULE_NAME        test_dmeth)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmeth_test.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# dmeth/dmeth_port build as standalone loadable modules (separate
# executables) rather than libraries - dmdrvi_dmeth_create() and friends are
# resolved dynamically at runtime by the DMOD loader, not through ordinary
# static linking, so this test binary cannot link against them directly (see
# dmeth_test.c for what that leaves testable at this level: header-only
# structural checks). Exercising the real DIF/port flow needs an on-target
# Application in the style of dmdma's dmdma_test_dev.c/dmdma_test_port.c
# instead (dmod_add_executable, opens /dev/dmeth0 via Dmod_FileOpen against a
# fully running system with dmeth+dmeth_port loaded) - not implemented here.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi
    dmini
)

target_link_libraries(${DMOD_MODULE_NAME} dmeth_if dmeth_port_if)
