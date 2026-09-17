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

#define DMETH_TEST_FRAME_BYTES    64U

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

    uint8_t tx_frame[DMETH_TEST_FRAME_BYTES];
    for (size_t i = 0; i < sizeof(tx_frame); i++)
        tx_frame[i] = (uint8_t)i;

    bool ok = true;

    size_t written = Dmod_FileWrite(tx_frame, 1, sizeof(tx_frame), handle);
    if (written != sizeof(tx_frame))
    {
        Dmod_Printf("ERROR: wrote %u of %u byte(s)\n", (unsigned)written, (unsigned)sizeof(tx_frame));
        ok = false;
    }

    uint8_t rx_frame[DMETH_TEST_FRAME_BYTES] = {0};
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
                    Dmod_Printf("ERROR: received frame does not match transmitted frame\n");
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
