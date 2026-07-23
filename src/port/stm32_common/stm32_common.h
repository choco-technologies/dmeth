#ifndef DMETH_STM32_COMMON_H
#define DMETH_STM32_COMMON_H

/**
 * @brief Shared ETH IRQ handler body, called from each family's
 *        DMOD_IRQ_HANDLER(ETH_IRQn) wrapper (see src/port/stm32f4/port.c,
 *        src/port/stm32f7/port.c) - the MAC/DMA IP block and IRQ number are
 *        identical on both families, so there is nothing family-specific
 *        left to do at the call site itself.
 */
void stm32_eth_irq_handler(void);

#endif // DMETH_STM32_COMMON_H
