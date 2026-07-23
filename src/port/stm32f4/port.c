#define DMOD_ENABLE_REGISTRATION    ON
#include "dmeth_port.h"
#include "dmod.h"
#include "../stm32_common/stm32_common.h"
#include "port/stm32_common_regs.h"
#include "port/stm32f4_regs.h"

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    Dmod_Printf("dmeth port module initialized (stm32f4)\n");
    return 0;
}

int dmod_deinit(void)
{
    Dmod_Printf("dmeth port module deinitialized (stm32f4)\n");
    return 0;
}

/* ---- ISR handler ----
 *
 * All register-level logic lives in stm32_common.c, shared with STM32F7
 * (identical Ethernet MAC/DMA IP block on the F4 parts that have one -
 * 407/417/427/429/437/439/469/479); only the NVIC IRQ line is declared per
 * family, via DMOD_IRQ_HANDLER below. */

DMOD_IRQ_HANDLER(ETH_IRQn)
{
    stm32_eth_irq_handler();
}
