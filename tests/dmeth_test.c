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

/* The frame is received into a buffer bigger than the frame itself, on
 * purpose. Reading into an exactly-sized one would cap the reported length at
 * what was asked for (dmeth copies min(frame_len, size)), hiding a driver that
 * hands up more bytes than it was sent - which is exactly the FCS-stripping
 * difference the two frame shapes below exist to catch. */
#define DMETH_TEST_RX_BUF_BYTES   128U

/* An EtherType, i.e. > 1500, so the length/type field is *not* a length.
 * MACCR.APCS only strips the FCS when that field is <= 1500, so this shape
 * takes a different path through the MAC's receive side than an 802.3 length
 * frame does - and it is the shape all real traffic uses (ARP, IPv4, ...).
 * 0x0806 (ARP) is used here simply because it is the smallest real one. */
#define DMETH_TEST_ETHERTYPE_ARP  0x0806U

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
 * Whatever the caller puts in the length/type field, the frame that comes
 * back has to be exactly the #DMETH_TEST_FRAME_BYTES that went out: the MAC
 * appends an FCS on transmit, and a driver that does not strip it again on
 * receive hands its caller four bytes that were never sent.
 *
 * @param frame       Buffer of #DMETH_TEST_FRAME_BYTES bytes to fill.
 * @param mac         This interface's own MAC address, used as both
 *                    destination and source.
 * @param len_or_type Value for the length/type field - a payload length
 *                    (<= 1500) makes it an 802.3 frame, anything above that
 *                    an Ethernet II frame of that EtherType.
 */
static void build_frame(uint8_t frame[DMETH_TEST_FRAME_BYTES],
                        const uint8_t mac[DMDRVI_NET_MAC_ADDR_LEN],
                        uint16_t len_or_type)
{
    for (size_t i = 0; i < DMDRVI_NET_MAC_ADDR_LEN; i++)
    {
        frame[i] = mac[i];                              /* destination */
        frame[DMDRVI_NET_MAC_ADDR_LEN + i] = mac[i];    /* source      */
    }

    frame[12] = (uint8_t)(len_or_type >> 8);
    frame[13] = (uint8_t)(len_or_type & 0xFFU);

    for (size_t i = DMETH_TEST_HDR_BYTES; i < DMETH_TEST_FRAME_BYTES; i++)
        frame[i] = (uint8_t)i;
}

/**
 * @brief Send one frame and verify the same bytes come back.
 *
 * @param handle      Open device handle, already started and in a loopback mode.
 * @param mac         Interface's own MAC, used as the frame's destination.
 * @param len_or_type Length/type field to build the frame with.
 * @param shape_name  Human-readable name of the frame shape, for the log.
 * @return true if exactly #DMETH_TEST_FRAME_BYTES came back unchanged.
 */
static bool roundtrip_frame(void *handle, const uint8_t mac[DMDRVI_NET_MAC_ADDR_LEN],
                            uint16_t len_or_type, const char *shape_name)
{
    uint8_t tx_frame[DMETH_TEST_FRAME_BYTES];
    uint8_t rx_frame[DMETH_TEST_RX_BUF_BYTES] = {0};

    build_frame(tx_frame, mac, len_or_type);

    size_t written = Dmod_FileWrite(tx_frame, 1, sizeof(tx_frame), handle);
    if (written != sizeof(tx_frame))
    {
        Dmod_Printf("ERROR: %s: wrote %u of %u byte(s)\n",
                    shape_name, (unsigned)written, (unsigned)sizeof(tx_frame));
        return false;
    }

    size_t received = Dmod_FileRead(rx_frame, 1, sizeof(rx_frame), handle);
    if (received != sizeof(tx_frame))
    {
        Dmod_Printf("ERROR: %s: received %u byte(s), expected %u%s\n",
                    shape_name, (unsigned)received, (unsigned)sizeof(tx_frame),
                    (received == sizeof(tx_frame) + 4U) ? " (FCS not stripped)" : "");
        return false;
    }

    /* No memcmp() - test_dmeth links without libc, so the frame is
     * compared byte-by-byte instead. */
    for (size_t i = 0; i < sizeof(tx_frame); i++)
    {
        if (rx_frame[i] != tx_frame[i])
        {
            Dmod_Printf("ERROR: %s: byte %u differs - sent 0x%02x, received 0x%02x\n",
                        shape_name, (unsigned)i, tx_frame[i], rx_frame[i]);
            return false;
        }
    }

    Dmod_Printf("   ok: %s\n", shape_name);
    return true;
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

    if (ok)
    {
        Dmod_Printf("sending %u byte(s) to %02x:%02x:%02x:%02x:%02x:%02x\n",
                    (unsigned)DMETH_TEST_FRAME_BYTES,
                    mac.addr[0], mac.addr[1], mac.addr[2],
                    mac.addr[3], mac.addr[4], mac.addr[5]);

        /* Both frame shapes, because they take different paths through the
         * MAC's receive side (see DMETH_TEST_ETHERTYPE_ARP) and only one of
         * them is what real traffic looks like. */
        ok  = roundtrip_frame(handle, mac.addr, DMETH_TEST_PAYLOAD_BYTES, "802.3 length frame");
        ok &= roundtrip_frame(handle, mac.addr, DMETH_TEST_ETHERTYPE_ARP, "EtherType frame");
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
