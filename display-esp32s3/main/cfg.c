#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "cfg.h"

cfg_t g_cfg;

#define CFG_VER 1
typedef struct { uint8_t ver; cfg_t c; } blob_t;

static void defaults(void)
{
    memset(&g_cfg, 0, sizeof g_cfg);
    strcpy(g_cfg.name, "Màn hình phụ");
    g_cfg.lcd_type = LCD_ST7789;
    g_cfg.invert = 1;
    g_cfg.bright = 80;
    g_cfg.night_dim = 1;
    g_cfg.want_cal = 1;     // offer the calibration on the first start
}

void cfg_load(void)
{
    defaults();
    nvs_handle_t h;
    if (nvs_open("dbr1", NVS_READONLY, &h) != ESP_OK) return;
    blob_t b;
    size_t n = sizeof b;
    if (nvs_get_blob(h, "cfg", &b, &n) == ESP_OK && n == sizeof b && b.ver == CFG_VER) g_cfg = b.c;
    nvs_close(h);
    if (g_cfg.bright < 5 || g_cfg.bright > 100) g_cfg.bright = 80;
}

void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("dbr1", NVS_READWRITE, &h) != ESP_OK) return;
    blob_t b = { .ver = CFG_VER, .c = g_cfg };
    nvs_set_blob(h, "cfg", &b, sizeof b);
    nvs_commit(h);
    nvs_close(h);
}
