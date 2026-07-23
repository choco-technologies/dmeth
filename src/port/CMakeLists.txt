# ======================================================================
#               dmeth_port Module Configuration
# ======================================================================

# Name of the module
set(DMOD_MODULE_NAME        dmeth_port)

# Version is already set above and used in project()

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the DMR file for this module
set(DMOD_DMR_PATH           ${CMAKE_SOURCE_DIR}/dmeth_port.dmr)

# STM32F4 and STM32F7 share the same Ethernet MAC/DMA IP block (see
# include/port/stm32_common_regs.h), so the bulk of the logic lives in
# stm32_common and per-family port.c stays a thin dmod_init()/dmod_deinit()
# + DMOD_IRQ_HANDLER wrapper - same pattern as dmfmc/src/port/CMakeLists.txt.
set(COMMON_SOURCES stm32_common/stm32_common.c)

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    ${DMOD_CPU_FAMILY}/port.c
    ${COMMON_SOURCES}
)

# dmosi: semaphore (RX-ready signaling, ISR-safe) and minimum interrupt
#        priority query, same as dmuart_port.
# dmclk_port: runtime HCLK query (MDC clock divisor) and microsecond delays
#             (PHY reset/autonegotiation, TX-descriptor-free polling).
# dmheap: allocates the RX/TX descriptor ring + packet buffers from the
#         shared "dma" heap context (DMA-capable, non-cacheable on F7's
#         DTCM - see docs/port-implementation.md).
dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
    dmclk_port
    dmheap
)

target_include_directories(${DMOD_MODULE_NAME} BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)
