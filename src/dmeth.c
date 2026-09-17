#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmeth.h"
#include "dmeth_port.h"
#include "dmeth_ioctl.h"
#include "dmdrvi.h"
#include "dmdrvi_ioctl.h"
#include "dmini.h"
#include "dmnetif.h"
#include <errno.h>
#include <string.h>

/* Magic set to "ETH0" */
#define DMETH_CONTEXT_MAGIC    0x45544830

/**
 * @brief DMDRVI context structure
 */
struct dmdrvi_context
{
    uint32_t              magic;      /**< Magic number for validation */
    dmeth_config_t        config;     /**< Configuration parameters */
    bool                  running;    /**< Whether DMDRVI_IOCTL_NET_START has been applied */
    dmnetif_iface_t       iface;      /**< Handle returned by dmnetif_register(), once the devfs path is known */
    dmeth_loopback_mode_t loopback_mode; /**< Last mode applied via DMETH_IOCTL_SET_LOOPBACK_MODE (test-only, see dmeth_ioctl.h) */
};

static int is_valid_context(dmdrvi_context_t context)
{
    return (context != NULL && context->magic == DMETH_CONTEXT_MAGIC);
}

/* ---- MAC address parsing ---- */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parses "AA:BB:CC:DD:EE:FF" into 6 raw bytes. Returns true on success. */
static bool parse_mac_address(const char *s, uint8_t mac[DMETH_MAC_ADDR_LEN])
{
    if (s == NULL)
        return false;

    for (int i = 0; i < DMETH_MAC_ADDR_LEN; i++)
    {
        int hi = hex_nibble(s[0]);
        int lo = (hi >= 0) ? hex_nibble(s[1]) : -1;
        if (hi < 0 || lo < 0)
            return false;

        mac[i] = (uint8_t)((hi << 4) | lo);
        s += 2;

        if (i < DMETH_MAC_ADDR_LEN - 1)
        {
            if (*s != ':')
                return false;
            s++;
        }
    }

    return *s == '\0';
}

/* ---- Configuration ---- */

/**
 * @brief Parse this driver's .ini configuration.
 *
 * Passes NULL as the section to every dmini call rather than looking one up
 * itself: dmdevfs already resolves which section belongs to this driver
 * instance (matched by its `driver_name=dmeth` key) and locks the ini
 * context to it via dmini_set_active_section() before calling
 * dmdrvi_create() - while that restriction is active, section == NULL
 * means "the active section" (see dmini.h). No section name needs to be
 * known or guessed here.
 */
static int read_config_parameters(dmdrvi_context_t context, dmini_context_t config)
{
    context->config.instance         = (dmeth_instance_t)dmini_get_int(config, NULL, "instance", 0);
    context->config.rx_buffer_count  = (uint16_t)dmini_get_int(config, NULL, "rx_buffer_count", 10);
    context->config.tx_buffer_count  = (uint16_t)dmini_get_int(config, NULL, "tx_buffer_count", 10);
    context->config.promiscuous      = (strcmp(dmini_get_string(config, NULL, "promiscuous", "off"), "on") == 0);
    context->config.phy_address      = (uint8_t)dmini_get_int(config, NULL, "phy_address", 0);

    const char *mac_str = dmini_get_string(config, NULL, "mac_address", NULL);
    context->config.mac_address_set  = parse_mac_address(mac_str, context->config.mac_address);
    if (mac_str != NULL && !context->config.mac_address_set)
    {
        DMOD_LOG_ERROR("Invalid mac_address in configuration: '%s'\n", mac_str);
        return -EINVAL;
    }

    if (context->config.rx_buffer_count == 0 || context->config.tx_buffer_count == 0)
    {
        DMOD_LOG_ERROR("rx_buffer_count/tx_buffer_count must be non-zero\n");
        return -EINVAL;
    }

    return 0;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    DMOD_LOG_INFO("DMETH interface module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMETH interface module deinitialized\n");
    return 0;
}

/* ---- DMDRVI interface ---- */

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, dmdrvi_context_t, _create, ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (config == NULL || dev_num == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters to dmeth_dmdrvi_create\n");
        return NULL;
    }

    dmdrvi_context_t context = Dmod_Malloc(sizeof(struct dmdrvi_context));
    if (context == NULL)
        return NULL;

    memset(context, 0, sizeof(*context));
    context->magic = DMETH_CONTEXT_MAGIC;

    if (read_config_parameters(context, config) != 0)
    {
        DMOD_LOG_ERROR("Failed to create DMDRVI context with provided configuration\n");
        Dmod_Free(context);
        return NULL;
    }

    dmeth_instance_t instance_count = dmeth_port_get_instance_count();
    if (context->config.instance >= instance_count)
    {
        DMOD_LOG_ERROR("ETH instance %u out of range (this target supports %u)\n",
                        context->config.instance, instance_count);
        Dmod_Free(context);
        return NULL;
    }

    if (dmeth_port_init(context->config.instance, &context->config) != 0)
    {
        DMOD_LOG_ERROR("Failed to initialize ETH%u\n", context->config.instance);
        Dmod_Free(context);
        return NULL;
    }

    DMOD_LOG_INFO("ETH%u initialized\n", context->config.instance);

    /* One major number per MAC peripheral, per dmdrvi's network-driver
     * guidance -> /dev/dmeth0, /dev/dmeth1, ... */
    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->major = (dmdrvi_dev_id_t)context->config.instance;

    return context;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, void, _free, ( dmdrvi_context_t context ))
{
    if (is_valid_context(context))
    {
        if (context->iface != NULL)
        {
            dmnetif_unregister(context->iface);
        }
        if (context->running)
        {
            dmeth_port_stop(context->config.instance);
        }
        dmeth_port_deinit(context->config.instance);
        context->magic = 0;
        Dmod_Free(context);
    }
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, void*, _open, ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    if (!is_valid_context(context))
    {
        DMOD_LOG_ERROR("Invalid DMDRVI context in dmeth_dmdrvi_open\n");
        return NULL;
    }
    return context;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, void, _close, ( dmdrvi_context_t context, void* handle ))
{
    /* No per-handle state to release - the interface stays up until STOP. */
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, size_t, _read, ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, uint32_t offset ))
{
    if (!is_valid_context(context) || buffer == NULL || size == 0 || !context->running)
        return 0;

    size_t received = 0;
    int ret = dmeth_port_receive_frame(context->config.instance, (uint8_t *)buffer, size, &received);
    if (ret != 0)
        return 0;
    return received;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, size_t, _write, ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, uint32_t offset ))
{
    if (!is_valid_context(context) || buffer == NULL || size == 0 || !context->running)
        return 0;

    int ret = dmeth_port_transmit_frame(context->config.instance, (const uint8_t *)buffer, size);
    if (ret != 0)
        return 0;
    return size;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, int, _ioctl, ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    if (!is_valid_context(context))
    {
        DMOD_LOG_ERROR("Invalid DMDRVI context in dmeth_dmdrvi_ioctl\n");
        return -EINVAL;
    }

    /* START/STOP take no argument; every other command below needs one. */
    if (arg == NULL && command != DMDRVI_IOCTL_NET_START && command != DMDRVI_IOCTL_NET_STOP)
    {
        DMOD_LOG_ERROR("Null argument for ioctl command %d\n", command);
        return -EINVAL;
    }

    switch (command)
    {
        case DMDRVI_IOCTL_NET_SET_MAC_ADDR:
        {
            const dmdrvi_net_mac_addr_t *mac = (const dmdrvi_net_mac_addr_t *)arg;
            memcpy(context->config.mac_address, mac->addr, DMETH_MAC_ADDR_LEN);
            context->config.mac_address_set = true;
            return dmeth_port_set_mac_address(context->config.instance, context->config.mac_address);
        }

        case DMDRVI_IOCTL_NET_GET_MAC_ADDR:
        {
            dmdrvi_net_mac_addr_t *mac = (dmdrvi_net_mac_addr_t *)arg;
            return dmeth_port_get_mac_address(context->config.instance, mac->addr);
        }

        case DMDRVI_IOCTL_NET_GET_LINK_STATUS:
        {
            dmdrvi_net_link_status_t *status = (dmdrvi_net_link_status_t *)arg;
            *status = dmeth_port_get_link_status(context->config.instance) ?
                       DMDRVI_NET_LINK_UP : DMDRVI_NET_LINK_DOWN;
            return 0;
        }

        case DMDRVI_IOCTL_NET_START:
        {
            if (context->config.mac_address_set)
            {
                int ret = dmeth_port_set_mac_address(context->config.instance, context->config.mac_address);
                if (ret != 0)
                    return ret;
            }

            int ret = dmeth_port_start(context->config.instance);
            if (ret == 0)
                context->running = true;
            return ret;
        }

        case DMDRVI_IOCTL_NET_STOP:
        {
            int ret = dmeth_port_stop(context->config.instance);
            context->running = false;
            return ret;
        }

        case DMETH_IOCTL_SET_PROMISCUOUS_MODE:
        {
            context->config.promiscuous = *(const bool *)arg;
            return dmeth_port_set_promiscuous_mode(context->config.instance, context->config.promiscuous);
        }

        case DMETH_IOCTL_GET_PROMISCUOUS_MODE:
        {
            *(bool *)arg = context->config.promiscuous;
            return 0;
        }

        case DMETH_IOCTL_SET_LOOPBACK_MODE:
        {
            dmeth_loopback_mode_t mode = *(const dmeth_loopback_mode_t *)arg;
            int ret = dmeth_port_set_loopback_mode(context->config.instance, mode);
            if (ret == 0)
                context->loopback_mode = mode;
            return ret;
        }

        case DMETH_IOCTL_GET_LOOPBACK_MODE:
        {
            *(dmeth_loopback_mode_t *)arg = context->loopback_mode;
            return 0;
        }

        default:
            DMOD_LOG_ERROR("Invalid ioctl command %d\n", command);
            return -EINVAL;
    }
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    if (!is_valid_context(context))
    {
        DMOD_LOG_ERROR("Invalid DMDRVI context in dmeth_dmdrvi_flush\n");
        return -EINVAL;
    }

    /* Frames are already fully handed to the DMA by the time _write()
     * returns - nothing left buffered at this layer to flush. */
    return 0;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, int, _stat, ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    if (!is_valid_context(context) || stat == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters in dmeth_dmdrvi_stat\n");
        return -EINVAL;
    }

    stat->size = 0;   /* Stream-like device, no fixed size */
    stat->mode = 0666; /* Read-write permissions */
    return 0;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmeth, void, _path_ready, ( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num, const char* path ))
{
    if (!is_valid_context(context) || path == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters in dmeth_dmdrvi_path_ready\n");
        return;
    }

    /* Bounded by dmeth_instance_t (uint8_t) - "eth255" always fits. */
    char name[16];
    Dmod_SnPrintf(name, sizeof(name), "eth%u", context->config.instance);

    context->iface = dmnetif_register(name, path);
    if (context->iface == NULL)
    {
        DMOD_LOG_ERROR("Failed to register %s (%s) with dmnetif\n", name, path);
        return;
    }

    DMOD_LOG_INFO("Registered %s -> %s with dmnetif\n", name, path);
}
