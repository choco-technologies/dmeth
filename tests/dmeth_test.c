#include "dmod.h"
#include "dmeth_ioctl.h"
#include "dmeth_types.h"
#include "dmdrvi_ioctl.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief dmeth_test - on-target MAC/PHY loopback smoke test, going through
 *        the real device node dmdevfs exposes for dmeth.
 *
 * Usage:
 *   dmeth_test <path-to-dmeth-device>
 *   e.g. dmeth_test /dev/dmeth0
 *
 * Opens the path with the ordinary VFS file interface (open/ioctl/write/
 * read/close) and drives it purely through dmdrvi_ioctl() commands,
 * exercising the real production path end to end: dmdevfs must already
 * have mounted and configured dmeth (i.e. the board's own eth0.ini, see
 * configs/board/) for this path to exist at all. A pass here means the
 * board's actual shipped configuration (phy_address, buffer counts, RMII
 * pin mux, ...) - not a synthetic one - produces a working MAC and a
 * working PHY.
 */

/* Ethernet header: 6 bytes destination MAC, 6 source, 2 length/type. */
#define DMETH_TEST_HDR_BYTES      14U
#define DMETH_TEST_FRAME_BYTES    64U
#define DMETH_TEST_PAYLOAD_BYTES  (DMETH_TEST_FRAME_BYTES - DMETH_TEST_HDR_BYTES)

/* Long enough to cover a real part's loopback settling, short enough that a
 * loopback which never round-trips gets *reported* rather than left hanging
 * the shell that started the test. */
#define DMETH_TEST_IO_TIMEOUT_MS  3000U

/**
 * @brief Build the frame this test transmits.
 *
 * It has to be a real, well-formed Ethernet frame rather than an arbitrary
 * byte pattern: both loopback modes feed the frame back through the MAC's
 * normal receive path, which applies the destination-address filter. A frame
 * not addressed to this interface is dropped there and never reaches a
 * descriptor - indistinguishable, from the read() side, from a MAC that does
 * not receive at all.
 *
 * The length/type field carries the payload length (<= 1500), which also
 * makes MACCR.APCS strip the FCS the MAC appends on transmit, so what comes
 * back is exactly the #DMETH_TEST_FRAME_BYTES that went out.
 *
 * @param frame Buffer of #DMETH_TEST_FRAME_BYTES bytes to fill.
 * @param mac   This interface's own MAC address, used as both destination
 *              and source.
 */
static void build_frame(uint8_t frame[DMETH_TEST_FRAME_BYTES], const uint8_t mac[DMDRVI_NET_MAC_ADDR_LEN])
{
    for (size_t i = 0; i < DMDRVI_NET_MAC_ADDR_LEN; i++)
    {
        frame[i] = mac[i];                              /* destination */
        frame[DMDRVI_NET_MAC_ADDR_LEN + i] = mac[i];    /* source      */
    }

    frame[12] = (uint8_t)(DMETH_TEST_PAYLOAD_BYTES >> 8);
    frame[13] = (uint8_t)(DMETH_TEST_PAYLOAD_BYTES & 0xFFU);

    for (size_t i = DMETH_TEST_HDR_BYTES; i < DMETH_TEST_FRAME_BYTES; i++)
        frame[i] = (uint8_t)i;
}

static bool run_loopback_roundtrip(void *handle, dmeth_loopback_mode_t mode, const char *mode_name)
{
    Dmod_Printf("-- %s loopback --\n", mode_name);

    if (Dmod_Ioctl(handle, DMETH_IOCTL_SET_LOOPBACK_MODE, &mode) != 0)
    {
        Dmod_Printf("ERROR: DMETH_IOCTL_SET_LOOPBACK_MODE(%s) failed\n", mode_name);
        return false;
    }

    if (Dmod_Ioctl(handle, DMDRVI_IOCTL_NET_START, NULL) != 0)
    {
        Dmod_Printf("ERROR: DMDRVI_IOCTL_NET_START failed\n");
        return false;
    }

    /* Read the MAC back *after* START: the address from the .ini is only
     * programmed into the MAC's filter registers there, so asking earlier
     * would address the frame to the filter's reset value instead. */
    bool ok = true;
    dmdrvi_net_mac_addr_t mac;
    if (Dmod_Ioctl(handle, DMDRVI_IOCTL_NET_GET_MAC_ADDR, &mac) != 0)
    {
        Dmod_Printf("ERROR: DMDRVI_IOCTL_NET_GET_MAC_ADDR failed\n");
        ok = false;
    }

    uint8_t tx_frame[DMETH_TEST_FRAME_BYTES];
    uint8_t rx_frame[DMETH_TEST_FRAME_BYTES] = {0};

    if (ok)
    {
        build_frame(tx_frame, mac.addr);
        Dmod_Printf("sending %u byte(s) to %02x:%02x:%02x:%02x:%02x:%02x\n",
                    (unsigned)sizeof(tx_frame),
                    mac.addr[0], mac.addr[1], mac.addr[2],
                    mac.addr[3], mac.addr[4], mac.addr[5]);

        size_t written = Dmod_FileWrite(tx_frame, 1, sizeof(tx_frame), handle);
        if (written != sizeof(tx_frame))
        {
            Dmod_Printf("ERROR: wrote %u of %u byte(s)\n", (unsigned)written, (unsigned)sizeof(tx_frame));
            ok = false;
        }
    }

    if (ok)
    {
        size_t received = Dmod_FileRead(rx_frame, 1, sizeof(rx_frame), handle);
        if (received != sizeof(tx_frame))
        {
            Dmod_Printf("ERROR: received %u of %u byte(s)\n", (unsigned)received, (unsigned)sizeof(tx_frame));
            ok = false;
        }
        else
        {
            /* No memcmp() - test_dmeth links without libc, so the frame is
             * compared byte-by-byte instead. */
            for (size_t i = 0; i < sizeof(tx_frame); i++)
            {
                if (rx_frame[i] != tx_frame[i])
                {
                    Dmod_Printf("ERROR: byte %u differs - sent 0x%02x, received 0x%02x\n",
                                (unsigned)i, tx_frame[i], rx_frame[i]);
                    ok = false;
                    break;
                }
            }
        }
    }

    dmeth_loopback_mode_t none = dmeth_loopback_mode_none;
    Dmod_Ioctl(handle, DMETH_IOCTL_SET_LOOPBACK_MODE, &none);
    Dmod_Ioctl(handle, DMDRVI_IOCTL_NET_STOP, NULL);

    Dmod_Printf("%s: %s loopback roundtrip\n", ok ? "PASS" : "FAIL", mode_name);
    return ok;
}

int main(int argc, char *argv[])
{
    Dmod_Printf("\n=== DMETH Device-Node Loopback Test ===\n\n");

    if (argc < 2)
    {
        Dmod_Printf("Usage: dmeth_test <path-to-dmeth-device>\n");
        Dmod_Printf("e.g. dmeth_test /dev/dmeth0\n\n");
        Dmod_Printf("Opens the given device node through dmdevfs/dmdrvi and verifies\n");
        Dmod_Printf("a MAC-internal and a PHY-internal loopback frame roundtrip\n");
        Dmod_Printf("through it, using the board's real configured settings.\n\n");
        return -1;
    }

    const char *path = argv[1];

    Dmod_Printf("Opening '%s'...\n", path);
    void *handle = Dmod_FileOpen(path, "r+");
    if (handle == NULL)
    {
        Dmod_Printf("ERROR: could not open '%s' (is dmdevfs mounted and dmeth configured?)\n", path);
        return -1;
    }

    /* A loopback that does not round-trip has to fail this test, not hang
     * the shell that started it - read()/write() block indefinitely by
     * default (see DMETH_IOCTL_SET_IO_TIMEOUT). */
    uint32_t io_timeout_ms = DMETH_TEST_IO_TIMEOUT_MS;
    if (Dmod_Ioctl(handle, DMETH_IOCTL_SET_IO_TIMEOUT, &io_timeout_ms) != 0)
    {
        Dmod_Printf("ERROR: DMETH_IOCTL_SET_IO_TIMEOUT failed\n");
        Dmod_FileClose(handle);
        return -1;
    }

    bool mac_ok = run_loopback_roundtrip(handle, dmeth_loopback_mode_mac, "MAC");
    bool phy_ok = run_loopback_roundtrip(handle, dmeth_loopback_mode_phy, "PHY");

    Dmod_FileClose(handle);

    if (mac_ok && phy_ok)
    {
        Dmod_Printf("\nPASS: '%s' - MAC and PHY loopback both roundtripped correctly\n\n", path);
        return 0;
    }

    Dmod_Printf("\nFAIL: '%s' - configuration does not pass loopback (MAC %s, PHY %s)\n\n",
                path, mac_ok ? "ok" : "FAILED", phy_ok ? "ok" : "FAILED");
    return -1;
}
