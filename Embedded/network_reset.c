#define _GNU_SOURCE
#include "network_reset.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Hardcoded to match exactly what the sudoers NOPASSWD rule authorizes
 * (see README.md). Calling plain "ip"/"wpa_cli"/"dhcpcd" here would
 * resolve through this process's $PATH, but sudo re-resolves commands
 * through its own secure_path - any mismatch between the two could
 * make `sudo -n` silently refuse to run it, so pin exact binaries. */
#define IP_BINARY      "/usr/sbin/ip"
#define WPA_CLI_BINARY "/sbin/wpa_cli"
#define DHCPCD_BINARY  "/usr/sbin/dhcpcd"

/* How long we're willing to wait, after kicking wpa_supplicant/dhcpcd,
 * for the interface to actually acquire a routable IPv4 address before
 * giving up and reporting failure to the caller. */
#define IP_WAIT_ATTEMPTS   20
#define IP_WAIT_INTERVAL_US (500 * 1000) /* 0.5s -> ~10s total budget */

/* system()'s return value is a raw wait-status, NOT a plain exit code -
 * decode it properly so any failure gets logged with a number that
 * actually matches sudo/ip's real, documented exit codes. */
static int decode_exit_status(int wait_status)
{
    if (wait_status == -1) return -1; /* system() itself failed (e.g. fork failed) */
    if (WIFEXITED(wait_status))   return WEXITSTATUS(wait_status);
    if (WIFSIGNALED(wait_status)) return -WTERMSIG(wait_status); /* killed by signal */
    return -1;
}

/* Returns 1 if `iface` currently holds a non-link-local IPv4 address
 * (i.e. it's actually been assigned a usable address by DHCP), else 0.
 * Bringing the link administratively "up" does NOT imply this - the
 * driver/wpa_supplicant can leave it unassociated indefinitely, which
 * is the exact failure mode that used to cause multi-hour outages. */
static int iface_has_ipv4(const char *iface)
{
    struct ifaddrs *ifaddr = NULL, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) return 0;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, iface) != 0) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        uint32_t addr = ntohl(sin->sin_addr.s_addr);
        /* Reject 169.254.0.0/16 link-local addresses - those mean DHCP
         * never actually succeeded, just an autoconf fallback. */
        if ((addr & 0xFFFF0000) == 0xA9FE0000) continue;

        found = 1;
        break;
    }

    freeifaddrs(ifaddr);
    return found;
}

/*
 * Force a Wi-Fi interface to drop and re-establish its link.
 *
 * Toggling the interface down/up alone only restores L2 (link) state -
 * it does NOT make wpa_supplicant re-associate with an AP, nor make
 * dhcpcd renew/acquire a lease. On the Pi's brcmfmac chipset especially,
 * that gap can leave the interface administratively "up" but never
 * actually reconnected, sometimes for the rest of the run. So after the
 * toggle we explicitly kick both of those clients, then poll for a real
 * routable IPv4 address before telling the caller we succeeded.
 *
 * Returns 0 only if the interface has a usable IPv4 address by the time
 * this returns; -1 otherwise (caller should treat this as "still down"
 * and retry sooner, not wait out the normal cooldown).
 */
int wifi_interface_reset(const char *iface)
{
    char cmd_down[160];
    char cmd_up[160];
    char cmd_wpa[160];
    char cmd_dhcp[160];

    /* `-n` (non-interactive): fail immediately instead of hanging on a
     * password prompt if passwordless sudo isn't configured for this
     * command, so a mis-configured Pi doesn't deadlock the caller
     * thread waiting on a TTY that will never answer. */
    snprintf(cmd_down, sizeof cmd_down, "sudo -n %s link set %s down", IP_BINARY, iface);
    snprintf(cmd_up,   sizeof cmd_up,   "sudo -n %s link set %s up",   IP_BINARY, iface);
    snprintf(cmd_wpa,  sizeof cmd_wpa,  "sudo -n %s -i %s reconfigure", WPA_CLI_BINARY, iface);
    snprintf(cmd_dhcp, sizeof cmd_dhcp, "sudo -n %s -n %s", DHCPCD_BINARY, iface);

    fprintf(stderr,
            "[network_reset] link has been down for a while; "
            "toggling interface '%s' down/up to force re-association\n",
            iface);

    int rc_down = decode_exit_status(system(cmd_down));
    /* Give the driver/kernel a moment between down and up; some Wi-Fi
     * chipsets (including the Pi's onboard one) need a short pause or
     * the "up" can silently no-op. */
    sleep(2);
    int rc_up = decode_exit_status(system(cmd_up));

    if (rc_down != 0 || rc_up != 0) {
        fprintf(stderr,
                "[network_reset] toggle failed (down rc=%d, up rc=%d) for '%s'. "
                "Check that passwordless sudo is configured - see README.md.\n",
                rc_down, rc_up, iface);
        return -1;
    }

    /* The toggle alone won't bring the association/lease back on its
     * own in a reasonable time - explicitly force both. Failures here
     * are logged but not immediately fatal; we still poll for an IP
     * below, since e.g. dhcpcd may recover on its own shortly after
     * wpa_supplicant reassociates. */
    int rc_wpa  = decode_exit_status(system(cmd_wpa));
    int rc_dhcp = decode_exit_status(system(cmd_dhcp));
    if (rc_wpa != 0) {
        fprintf(stderr,
                "[network_reset] wpa_cli reconfigure rc=%d for '%s' "
                "(continuing; will still verify via IP polling)\n",
                rc_wpa, iface);
    }
    if (rc_dhcp != 0) {
        fprintf(stderr,
                "[network_reset] dhcpcd rebind rc=%d for '%s' "
                "(continuing; will still verify via IP polling)\n",
                rc_dhcp, iface);
    }

    /* Don't declare victory until there's an actual usable address -
     * this is the check that was missing before, and it's what lets
     * the caller distinguish "really fixed" from "toggled but still
     * broken" instead of just waiting out a fixed cooldown blind. */
    for (int i = 0; i < IP_WAIT_ATTEMPTS; i++) {
        if (iface_has_ipv4(iface)) {
            fprintf(stderr,
                    "[network_reset] '%s' reassociated and has an IP; "
                    "reconnection loop should recover shortly\n",
                    iface);
            return 0;
        }
        usleep(IP_WAIT_INTERVAL_US);
    }

    fprintf(stderr,
            "[network_reset] '%s' still has no usable IPv4 address "
            "%.1fs after reset attempt - link toggle did not fix it\n",
            iface, (IP_WAIT_ATTEMPTS * IP_WAIT_INTERVAL_US) / 1e6);
    return -1;
}
