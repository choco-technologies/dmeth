#ifndef DMETH_TYPES_H
#define DMETH_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Ethernet MAC instance type
 */
typedef uint8_t dmeth_instance_t;

/**
 * @brief Length in bytes of an Ethernet MAC address (mirrors DMDRVI_NET_MAC_ADDR_LEN)
 */
#define DMETH_MAC_ADDR_LEN 6

/**
 * @brief dmeth driver configuration structure
 *
 * Deliberately kept in this header rather than dmeth.h: dmeth.h pulls in
 * dmeth_defs.h (dmeth core's own generated Built-in API macros), which only
 * exists in the dmeth module's own build - dmeth_port.h needs this struct
 * too (dmeth_port_init() takes the whole thing in one call, unlike
 * dmuart_port's many-individual-setters split), and dmeth_port is a
 * separate module build that has no access to dmeth's generated defs file.
 */
typedef struct
{
    dmeth_instance_t instance;                        /**< Ethernet MAC instance number */
    uint8_t          mac_address[DMETH_MAC_ADDR_LEN];  /**< Static MAC address from .ini (valid when mac_address_set) */
    bool             mac_address_set;                  /**< Whether mac_address came from .ini */
    uint16_t         rx_buffer_count;                  /**< Number of RX descriptors/buffers */
    uint16_t         tx_buffer_count;                  /**< Number of TX descriptors/buffers */
    bool             promiscuous;                      /**< Start in promiscuous mode */
    uint8_t          phy_address;                       /**< MDIO address of the PHY chip (board-specific) */
} dmeth_config_t;

/**
 * @brief Opaque driver context type (forward declaration)
 */
struct dmdrvi_context;
typedef struct dmdrvi_context *dmdrvi_context_t;

#endif /* DMETH_TYPES_H */
