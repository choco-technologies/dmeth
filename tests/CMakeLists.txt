set(DMOD_MODULE_NAME        test_dmeth)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

# Path to the DMR file for this module - enables release package creation
set(DMOD_DMR_PATH           ${CMAKE_SOURCE_DIR}/test_dmeth.dmr)

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
# structural checks). Exercising the real DIF/port flow is what
# dmeth_test_app below does instead.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi
    dmini
)

target_link_libraries(${DMOD_MODULE_NAME} dmeth_if dmeth_port_if)

# dmeth_test_app instead opens the real device node dmdevfs exposes once
# dmeth is configured (e.g. /dev/dmeth0) and drives it purely through the
# VFS file interface (open/ioctl/write/read/close) - exercising the real
# dmdrvi DIF wiring end to end, with the board's actual .ini configuration
# applied, not a hand-built one. Same split dmdma uses between
# dmdma_test_port.c (port-direct) and dmdma_test_dev.c (device-node) - see
# dmeth_test_app.c.
set(DMOD_DMR_PATH ${CMAKE_SOURCE_DIR}/test_dmeth_app.dmr)

dmod_add_executable(test_dmeth_app ${DMOD_MODULE_VERSION}
    dmeth_test_app.c
)

target_include_directories(test_dmeth_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

dmod_link_modules(test_dmeth_app
    dmdrvi
)

target_link_libraries(test_dmeth_app dmeth_if)
