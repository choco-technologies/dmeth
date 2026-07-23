#define DMOD_ENABLE_REGISTRATION    ON
#include "dmeth_port.h"
#include "dmod.h"
#include "../stm32_common/stm32_common.h"
#include "port/stm32_common_regs.h"
#include "port/stm32f7_regs.h"

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    Dmod_Printf("dmeth port module initialized (stm32f7)\n");
    return 0;
}

int dmod_deinit(void)
{
    Dmod_Printf("dmeth port module deinitialized (stm32f7)\n");
    return 0;
}

/* ---- ISR handler ----
 *
 * All register-level logic lives in stm32_common.c, shared with STM32F4
 * (identical Ethernet MAC/DMA IP block); only the NVIC IRQ line is declared
 * per family, via DMOD_IRQ_HANDLER below. */

DMOD_IRQ_HANDLER(ETH_IRQn)
{
    stm32_eth_irq_handler();
}
