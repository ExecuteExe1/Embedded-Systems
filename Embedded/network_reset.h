#ifndef NETWORK_RESET_H
#define NETWORK_RESET_H

/* Force-toggles a network interface down/up to recover from a stuck  Wi-Fi association that the WebSocket-level reconnection loop alone 
 cannot fix (the socket keeps failing to connect because the link is  gone, not because the remote server is unreachable).*/
/* Requires passwordless sudo for `ip link set <iface> down|up` -
 * Returns 0 on success (both commands exited 0), -1 otherwise.
 */
int wifi_interface_reset(const char *iface);

#endif
