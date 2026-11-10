/*
 * wlan_circle.cpp — the on-board Wi-Fi and its WPA supplicant, for the board.
 *
 * See wlan_circle.h. Three pieces, each standing in for something Circle's
 * own WLAN sample takes from elsewhere:
 *
 * - The scheduler. CBcm4343Device runs its reader and timer as Circle tasks
 *   (ether4330.c, kproc "wifireader" and "wifitimer") and the supplicant is a
 *   third. Okapia had no CScheduler at all; it is created here, once, the first
 *   time Wi-Fi is asked for, and the emulation thread becomes its main task.
 *   The tasks run only when that thread yields: WlanYield(), from the engine's
 *   1 kHz seam and from the boot menu's loop.
 *
 * - The supplicant's link layer. Upstream's l2_packet_circle.cpp reaches the
 *   EAPOL frames through CNetSubSystem's link layer, which would also take
 *   every IP and ARP frame meant for the Macintosh. The l2_packet functions
 *   below replace it — the linker never pulls that member out of
 *   libwpa_supplicant.a, because every symbol it would provide is already
 *   defined — and read from a queue the board fills.
 *
 * - The configuration file. wpa_supplicant reads its networks with fopen()
 *   (config_file.c:339). Writing one on the card at every start would be a
 *   FAT and directory update that a pulled plug can catch half done, which is
 *   the one thing the card's rules forbid (docs/topics/storage.md). fopen is
 *   wrapped at the final link instead: one name, CONFIG_NAME, is answered from
 *   memory, and every other name goes to newlib's own.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "wlan_circle.h"

#include <circle/logger.h>
#include <circle/macaddress.h>
#include <circle/sched/scheduler.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_config_circle.h"

#define FROM "okapia-wlan"

// The Broadcom firmware, as Circle's addon/wlan/firmware/Makefile fetches it:
// brcmfmac43455-sdio.bin, .txt and .clm_blob for a Pi 4. make-pi-sd.sh copies
// them; the card mounts as "SD:" (hal_circle.cpp).
static const char FIRMWARE_PATH[] = "SD:/firmware/";

// Not a path FatFs could resolve: a name only __wrap_fopen() answers.
static const char CONFIG_NAME[] = "okapia:wpa_supplicant.conf";

static CBcm4343Device  *s_pWLAN;
static CWPASupplicant  *s_pSupplicant;
static bool             s_bTried;
static char             s_SSID[33];
static char             s_Config[512];
static size_t           s_nConfig;

/*
 *  The configuration, from memory
 */

extern "C" FILE *__real_fopen (const char *pName, const char *pMode);

extern "C" FILE *__wrap_fopen (const char *pName, const char *pMode)
{
    if (pName != 0 && strcmp (pName, CONFIG_NAME) == 0)
    {
        if (s_nConfig == 0 || pMode == 0 || pMode[0] != 'r')
        {
            errno = ENOENT;
            return 0;
        }
        return fmemopen (s_Config, s_nConfig, "r");
    }
    return __real_fopen (pName, pMode);
}

// hostap's own, in libwpa_supplicant.a (src/crypto/sha1-pbkdf2.c).
extern "C" int pbkdf2_sha1 (const char *pPassphrase, const u8 *pSSID, size_t nSSIDLength,
                            int nIterations, u8 *pBuffer, size_t nLength);

static bool DerivePSK (const char *pPassphrase, const unsigned char *pSSID,
                       size_t nSSIDLength, unsigned char *pPSK)
{
    return pbkdf2_sha1 (pPassphrase, pSSID, nSSIDLength, 4096, pPSK, 32) == 0;
}

/*
 *  The EAPOL frames, between the board and the supplicant
 *
 *  Filled by BoardNetPoll() on the main task, emptied by the supplicant's task
 *  in l2_packet_receive(). The scheduler is cooperative and neither side yields
 *  while it holds a slot, so a ring needs no lock. Eight is generous: a 4-way
 *  handshake is four frames, a group rekey two.
 */

static const unsigned EAPOL_SLOTS = 8;
static const unsigned EAPOL_SIZE  = 1600;

static u8       s_EAPOL[EAPOL_SLOTS][EAPOL_SIZE];
static unsigned s_EAPOLLength[EAPOL_SLOTS];
static unsigned s_nEAPOLIn, s_nEAPOLOut;
static unsigned s_nEAPOLDropped;

void WlanTakeEAPOL (const u8 *pFrame, unsigned nLength)
{
    if (nLength > EAPOL_SIZE || nLength < 14 || s_nEAPOLIn - s_nEAPOLOut >= EAPOL_SLOTS)
    {
        s_nEAPOLDropped++;
        return;
    }
    const unsigned nSlot = s_nEAPOLIn % EAPOL_SLOTS;
    memcpy (s_EAPOL[nSlot], pFrame, nLength);
    s_EAPOLLength[nSlot] = nLength;
    s_nEAPOLIn++;
}

/*
 *  The supplicant's l2_packet layer
 *
 *  The interface is hostap's src/l2_packet/l2_packet.h, declared here rather
 *  than included: its headers want hostap's own build flags, and these C
 *  signatures are all the linker compares. The behaviour follows Circle's
 *  addon/wlan/hostap/src/l2_packet/l2_packet_circle.cpp by R. Stange (GPLv2,
 *  or alternatively BSD — used here under the BSD terms), with the link layer
 *  replaced by the queue above and SendFrame().
 */

extern "C"
{

typedef void (*eloop_sock_handler) (int sock, void *eloop_ctx, void *sock_ctx);
typedef void (*eloop_timeout_handler) (void *eloop_ctx, void *user_ctx);
int  eloop_register_read_sock (int sock, eloop_sock_handler handler,
                               void *eloop_data, void *user_data);
void eloop_unregister_read_sock (int sock);
int  eloop_register_timeout (unsigned int secs, unsigned int usecs,
                             eloop_timeout_handler handler,
                             void *eloop_data, void *user_data);
int  eloop_cancel_timeout (eloop_timeout_handler handler,
                           void *eloop_data, void *user_data);

typedef void TL2Receive (void *ctx, const u8 *src_addr, const u8 *buf, size_t len);

struct l2_packet_data
{
    unsigned short  protocol;
    TL2Receive     *rx_callback;
    void           *rx_callback_ctx;
    u8              own_addr[6];
};

// eloop_circle.c polls every registered reader on each pass; the number only
// has to be the one it was registered with.
#define L2_SOCK_FD  1
#define L2_AUTH_DURATION_SECS 5

static int s_bAuthActive;

static void l2_packet_receive (int sock, void *eloop_ctx, void *sock_ctx)
{
    l2_packet_data *l2 = (l2_packet_data *) eloop_ctx;
    while (l2 != 0 && s_nEAPOLOut != s_nEAPOLIn)
    {
        const unsigned nSlot = s_nEAPOLOut % EAPOL_SLOTS;
        const u8 *pFrame = s_EAPOL[nSlot];
        const unsigned nLength = s_EAPOLLength[nSlot];
        // The callback may yield; take the slot first, and copy it, so the
        // board cannot reuse it underneath.
        static u8 Copy[EAPOL_SIZE];
        memcpy (Copy, pFrame, nLength);
        s_nEAPOLOut++;
        (*l2->rx_callback) (l2->rx_callback_ctx, Copy + 6, Copy + 14, nLength - 14);
    }
}

l2_packet_data *l2_packet_init (const char *ifname, const u8 *own_addr,
                                unsigned short protocol, TL2Receive *rx_callback,
                                void *rx_callback_ctx, int l2_hdr)
{
    if (s_pWLAN == 0 || protocol != WLAN_ETHERTYPE_EAPOL || l2_hdr != 0)
    {
        return 0;
    }
    l2_packet_data *l2 = (l2_packet_data *) calloc (1, sizeof *l2);
    if (l2 == 0)
    {
        return 0;
    }
    l2->protocol = protocol;
    l2->rx_callback = rx_callback;
    l2->rx_callback_ctx = rx_callback_ctx;
    s_pWLAN->GetMACAddress ()->CopyTo (l2->own_addr);
    s_bAuthActive = 0;
    eloop_register_read_sock (L2_SOCK_FD, l2_packet_receive, l2, 0);
    return l2;
}

l2_packet_data *l2_packet_init_bridge (const char *br_ifname, const char *ifname,
                                       const u8 *own_addr, unsigned short protocol,
                                       TL2Receive *rx_callback, void *rx_callback_ctx,
                                       int l2_hdr)
{
    return l2_packet_init (br_ifname, own_addr, protocol, rx_callback, rx_callback_ctx, l2_hdr);
}

void l2_packet_deinit (l2_packet_data *l2)
{
    if (l2 != 0)
    {
        eloop_unregister_read_sock (L2_SOCK_FD);
        free (l2);
    }
}

int l2_packet_get_own_addr (l2_packet_data *l2, u8 *addr)
{
    memcpy (addr, l2->own_addr, 6);
    return 0;
}

int l2_packet_send (l2_packet_data *l2, const u8 *dst_addr, u16 proto,
                    const u8 *buf, size_t len)
{
    static u8 Frame[FRAME_BUFFER_SIZE];
    if (l2 == 0 || s_pWLAN == 0 || len == 0 || len + 14 > sizeof Frame)
    {
        return -1;
    }
    memcpy (Frame, dst_addr, 6);
    memcpy (Frame + 6, l2->own_addr, 6);
    Frame[12] = (u8) (l2->protocol >> 8);
    Frame[13] = (u8) l2->protocol;
    memcpy (Frame + 14, buf, len);
    return s_pWLAN->SendFrame (Frame, (unsigned) (len + 14)) ? 0 : -1;
}

static void l2_packet_auth_end (void *eloop_ctx, void *timeout_ctx)
{
    s_bAuthActive = 0;
}

// While a handshake runs, eloop_circle.c polls every 10 ms instead of 200.
void l2_packet_notify_auth_start (l2_packet_data *l2)
{
    s_bAuthActive = 1;
    eloop_cancel_timeout (l2_packet_auth_end, l2, 0);
    eloop_register_timeout (L2_AUTH_DURATION_SECS, 0, l2_packet_auth_end, l2, 0);
}

int l2_packet_auth_active (void)
{
    return s_bAuthActive;
}

int l2_packet_get_ip_addr (l2_packet_data *l2, char *buf, size_t len)
{
    return -1;
}

// enum l2_packet_filter_type, passed as an int by the C ABI.
int l2_packet_set_packet_filter (l2_packet_data *l2, int type)
{
    return -1;
}

}   // extern "C"

/*
 *  Starting it
 */

CNetDevice *WlanStart (const char *pSSID, const char *pPassword, const char *pCountry)
{
    if (s_bTried)
    {
        return s_pWLAN;
    }
    s_bTried = true;

    CLogger *pLog = CLogger::Get ();

#if defined (CIRCLE_QEMU) || (RASPPI <= 3 && defined (NO_SDHOST))
    // The WLAN chip sits on the controller that is the SD card's in this
    // build: starting it would take the card away from the Macintosh.
    pLog->Write (FROM, LogWarning, "No Wi-Fi in this build (QEMU, or the card on the Wi-Fi's controller)");
    return 0;
#endif

    // Everything that can be refused is refused before the hardware is touched.
    const char *pError = 0;
    if (!NetWpaConfigBuild (s_Config, sizeof s_Config, pSSID, pPassword, pCountry,
                            DerivePSK, &pError))
    {
        pLog->Write (FROM, LogError, "Wi-Fi not started: %s", pError);
        return 0;
    }
    s_nConfig = strlen (s_Config);
    NetCopyValue (s_SSID, sizeof s_SSID, pSSID);

    if (!CScheduler::IsActive ())
    {
        new CScheduler;
    }

    pLog->Write (FROM, LogNotice, "Loading the Wi-Fi firmware from %s", FIRMWARE_PATH);
    s_pWLAN = new CBcm4343Device (FIRMWARE_PATH);
    if (s_pWLAN == 0 || !s_pWLAN->Initialize ())
    {
        // Circle cannot take the device back: it stays, unused.
        pLog->Write (FROM, LogError,
                     "Wi-Fi did not start: is brcmfmac43455-sdio.bin, .txt and .clm_blob "
                     "in /firmware on the card?");
        s_pWLAN = 0;
        return 0;
    }

    s_pSupplicant = new CWPASupplicant (CONFIG_NAME);
    if (s_pSupplicant == 0 || !s_pSupplicant->Initialize ())
    {
        pLog->Write (FROM, LogError, "The WPA supplicant did not start");
        return 0;
    }

    CString MAC;
    s_pWLAN->GetMACAddress ()->Format (&MAC);
    pLog->Write (FROM, LogNotice, "Wi-Fi %s, joining \"%s\" (%s, country %s)",
                 (const char *) MAC, s_SSID,
                 pPassword == 0 || pPassword[0] == '\0' ? "open" : "WPA2", pCountry);
    return s_pWLAN;
}

void WlanYield (void)
{
    if (s_pWLAN != 0)
    {
        CScheduler::Get ()->Yield ();
    }
}

const char *WlanSSID (void)
{
    return s_SSID;
}
