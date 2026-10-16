/* Wi-Fi: join the saved network, or open a setup network with a captive portal. */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include "net.h"
#include "cfg.h"

static const char *TAG = "net";
static esp_netif_t *s_sta, *s_ap;
static volatile net_state_t s_state = NET_CONNECTING;
static volatile bool s_ap_on;
static volatile bool s_got_ip;
static char s_ap_ssid[32];
static esp_ip4_addr_t s_ip, s_mask;
static TaskHandle_t s_dns_task;
static int64_t s_boot_us;
static SemaphoreHandle_t s_scan_lock;

static void dns_task(void *arg);

static void ap_enable(bool on)
{
    if (on == s_ap_on) return;
    s_ap_on = on;
    wifi_mode_t m = on ? (g_cfg.ssid[0] ? WIFI_MODE_APSTA : WIFI_MODE_APSTA) : WIFI_MODE_STA;
    esp_wifi_set_mode(m);
    if (on) {
        wifi_config_t ap = { 0 };
        strcpy((char *)ap.ap.ssid, s_ap_ssid);
        ap.ap.ssid_len = strlen(s_ap_ssid);
        ap.ap.channel = 6;
        ap.ap.max_connection = 4;
        ap.ap.authmode = WIFI_AUTH_OPEN;
        esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (!s_dns_task) xTaskCreate(dns_task, "dns", 3072, NULL, 3, &s_dns_task);
        ESP_LOGI(TAG, "setup Wi-Fi \"%s\" on, http://192.168.4.1", s_ap_ssid);
    }
    if (!s_got_ip) s_state = on ? NET_SETUP : NET_CONNECTING;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (g_cfg.ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_got_ip = false;
        s_ip.addr = 0;
        if (!s_ap_on) s_state = NET_CONNECTING;
        else s_state = NET_SETUP;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        s_ip = e->ip_info.ip;
        s_mask = e->ip_info.netmask;
        s_got_ip = true;
        s_state = NET_ONLINE;
        ESP_LOGI(TAG, "online " IPSTR, IP2STR(&s_ip));
    }
}

/* Keep trying the saved network; open the setup network when it does not work out. */
static void wifi_task(void *arg)
{
    int64_t down_since = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now = esp_timer_get_time();
        if (s_got_ip) {
            down_since = now;
            // the setup network was opened because the saved one failed: close it once online,
            // unless someone is on it right now
            if (s_ap_on && g_cfg.ssid[0]) {
                wifi_sta_list_t l;
                if (esp_wifi_ap_get_sta_list(&l) == ESP_OK && l.num == 0) ap_enable(false);
            }
            continue;
        }
        if (!g_cfg.ssid[0]) continue;
        if (!s_ap_on && now - down_since > 25 * 1000000LL) ap_enable(true);
        static int64_t last_try;
        if (now - last_try > 12 * 1000000LL) {
            last_try = now;
            esp_wifi_disconnect();
            esp_wifi_connect();
        }
    }
}

void net_init(void)
{
    s_boot_us = esp_timer_get_time();
    s_scan_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    char host[32];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ap_ssid, sizeof s_ap_ssid, "DBR1-Display-%02X%02X", mac[4], mac[5]);
    snprintf(host, sizeof host, "dbr1-display-%02x%02x", mac[4], mac[5]);
    esp_netif_set_hostname(s_sta, host);

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);

    if (g_cfg.ssid[0]) {
        wifi_config_t sc = { 0 };
        strncpy((char *)sc.sta.ssid, g_cfg.ssid, sizeof sc.sta.ssid);
        strncpy((char *)sc.sta.password, g_cfg.pass, sizeof sc.sta.password);
        sc.sta.threshold.authmode = g_cfg.pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
        sc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sc);
        esp_wifi_start();
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    } else {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_start();
        ap_enable(true);
        s_state = NET_SETUP;
    }
    xTaskCreate(wifi_task, "wifi", 3072, NULL, 3, NULL);
}

net_state_t net_state(void) { return s_state; }
bool net_setup_ap_on(void) { return s_ap_on; }
const char *net_ap_ssid(void) { return s_ap_ssid; }

void net_ip(char *out, int len)
{
    if (!s_got_ip) { out[0] = 0; return; }
    snprintf(out, len, IPSTR, IP2STR(&s_ip));
}

uint32_t net_broadcast(void)
{
    if (!s_got_ip) return 0;
    return (s_ip.addr & s_mask.addr) | ~s_mask.addr;
}

int net_rssi(void)
{
    wifi_ap_record_t r;
    if (!s_got_ip || esp_wifi_sta_get_ap_info(&r) != ESP_OK) return 0;
    return r.rssi;
}

static void json_str(char *out, int len, int *n, const char *s)
{
    if (*n < len - 1) out[(*n)++] = '"';
    for (; *s && *n < len - 8; s++) {
        unsigned char c = *s;
        if (c == '"' || c == '\\') { out[(*n)++] = '\\'; out[(*n)++] = c; }
        else if (c < 0x20) *n += snprintf(out + *n, len - *n, "\\u%04x", c);
        else out[(*n)++] = c;
    }
    if (*n < len - 1) out[(*n)++] = '"';
    out[*n] = 0;
}

int net_scan_json(char *out, int len)
{
    xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    int n = snprintf(out, len, "[");
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        uint16_t cnt = 20;
        wifi_ap_record_t *rec = calloc(cnt, sizeof *rec);
        if (rec && esp_wifi_scan_get_ap_records(&cnt, rec) == ESP_OK) {
            for (int i = 0; i < cnt && n < len - 120; i++) {
                if (!rec[i].ssid[0]) continue;
                bool dup = false;
                for (int j = 0; j < i; j++) if (!strcmp((char *)rec[j].ssid, (char *)rec[i].ssid)) dup = true;
                if (dup) continue;
                if (n > 1) out[n++] = ',';
                n += snprintf(out + n, len - n, "{\"s\":");
                json_str(out, len, &n, (char *)rec[i].ssid);
                n += snprintf(out + n, len - n, ",\"r\":%d,\"l\":%d}", rec[i].rssi, rec[i].authmode != WIFI_AUTH_OPEN);
            }
        }
        free(rec);
    }
    n += snprintf(out + n, len - n, "]");
    xSemaphoreGive(s_scan_lock);
    return n;
}

/* Captive DNS: on the setup network every name is 192.168.4.1, so phones open the portal. */
static void dns_task(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) < 0) { ESP_LOGE(TAG, "dns bind"); vTaskDelete(NULL); }
    uint8_t b[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(s, b, sizeof b - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12 || !s_ap_on) continue;
        if ((b[2] & 0x80) || b[4] != 0 || b[5] != 1) continue;   // one question, a query
        int p = 12;
        while (p < n && b[p]) p += b[p] + 1;
        p += 5;                                                   // zero label + type + class
        if (p > n) continue;
        bool type_a = b[p - 4] == 0 && b[p - 3] == 1;
        b[2] = 0x81; b[3] = 0x80;                                 // response, recursion available
        b[6] = 0; b[7] = type_a ? 1 : 0;                          // answers
        b[8] = b[9] = b[10] = b[11] = 0;
        n = p;
        if (type_a) {
            const uint8_t ans[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
            memcpy(b + n, ans, sizeof ans);
            n += sizeof ans;
        }
        sendto(s, b, n, 0, (struct sockaddr *)&from, fl);
    }
}
