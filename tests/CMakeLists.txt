set(DMOD_MODULE_NAME        test_dmeth)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

# Path to the DMR file for this module - enables release package creation
set(DMOD_DMR_PATH           ${CMAKE_SOURCE_DIR}/test_dmeth.dmr)

# On-target application (not a dmod_add_test()/DMOD_TEST_STEP unit test -
# it needs argv and is meant to be run manually from the shell, the same
# way dmdma_test_dev.c is for dmdma). Opens the real device node dmdevfs
# exposes once dmeth is configured (e.g. /dev/dmeth0) and drives it purely
# through the VFS file interface (open/ioctl/write/read/close) -
# exercising the real dmdrvi DIF wiring end to end, with the board's
# actual .ini configuration applied, not a hand-built one.
dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmeth_test.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi
)

target_link_libraries(${DMOD_MODULE_NAME} dmeth_if)

if(DMOD_TOOLS_NAME STREQUAL "arch/x86_64")
    find_package(Threads REQUIRED)
    add_executable(test_tx_ring tx_ring_test.c)
    target_compile_definitions(test_tx_ring PRIVATE DMOD_dmeth_port DMOD_CURRENT_ALLOCATOR="test_tx_ring")
    get_target_property(PORT_INCLUDES dmeth_port INCLUDE_DIRECTORIES)
    target_include_directories(test_tx_ring PRIVATE ${PORT_INCLUDES})
    target_link_libraries(test_tx_ring PRIVATE dmeth_port_if dmod_inc Threads::Threads)
    target_compile_options(test_tx_ring PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(test_tx_ring PRIVATE -Wl,--gc-sections)
    add_test(NAME tx_ring COMMAND test_tx_ring)
    set_tests_properties(tx_ring PROPERTIES TIMEOUT 10)
endif()
