#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { NET_CONNECTING, NET_ONLINE, NET_SETUP } net_state_t;

void net_init(void);
net_state_t net_state(void);
bool net_setup_ap_on(void);           // the setup Wi-Fi is up (alone or beside a failing STA)
const char *net_ap_ssid(void);
void net_ip(char *out, int len);      // the station's address, "" when offline
uint32_t net_broadcast(void);         // subnet broadcast, network byte order; 0 when offline
int net_rssi(void);
/* JSON array of networks around: [{"s":"ssid","r":-60,"l":1},…] */
int net_scan_json(char *out, int len);

/* Networks around, scanned in the background (a scan blocks for ~2 s). */
typedef struct { char ssid[33]; int rssi; bool lock; } wifi_ap_t;
void net_scan_start(void);
/* true once the scan started last has finished; fills out[0..*n) */
bool net_scan_get(wifi_ap_t *out, int *n, int max);
