/*
 * What the display shows. 320x240 landscape: a top bar (speaker, volume, clock, theme button) and,
 * below it, two tiles you swipe between:
 *
 *   tile 0 (main)  one of these pages, chosen from the speaker's state:
 *     status  – no Wi-Fi yet / looking for the speaker / connection lost
 *     choose  – several speakers on the network, tap one
 *     voice   – the assistant is listening or talking: a face, what was said, "Tắt nghe"
 *     music   – a song, a radio station or a podcast: title, spectrum, progress, buttons
 *     idle    – a big clock, the date, the weather, the next alarm, "Tắt nghe" / "Gọi loa"
 *   tile 1 (apps)  a phone-style launcher: theme, brightness, volume, call, find speaker,
 *                  screen settings, info, touch calibration. Most open a small settings sheet.
 *
 * On top of everything the alarm, while it rings.
 *
 * Two themes, the same two the speaker's control panel has: dark (red accent) and light (the blue
 * of dbrobot.vn). The choice is kept in NVS; switching rebuilds the whole screen, which is simpler
 * and surer than restyling every object.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "lvgl.h"
#include "ui.h"
#include "link.h"
#include "net.h"
#include "cfg.h"
#include "display.h"
#include "touch.h"
#include "board.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#define NOW_US() esp_timer_get_time()
#else
int64_t sim_now_us(void);
void esp_restart(void);
#define NOW_US() sim_now_us()
#endif

LV_FONT_DECLARE(font_vn14);
LV_FONT_DECLARE(font_vn18);
LV_FONT_DECLARE(font_vn24);
LV_FONT_DECLARE(font_clock72);
LV_FONT_DECLARE(font_num52);
LV_FONT_DECLARE(font_icon40);
LV_FONT_DECLARE(font_icon30);

/* FontAwesome code points present in the fonts */
#define I_MUSIC   ""
#define I_PLAY    ""
#define I_PAUSE   ""
#define I_STOP    ""
#define I_NEXT    ""
#define I_VOLUP   ""
#define I_VOLDN   ""
#define I_VOLOFF  ""
#define I_WIFI    ""
#define I_BELL    ""
#define I_MIC     ""
#define I_MICOFF  ""
#define I_SUN     ""
#define I_MOON    ""
#define I_CLOUD   ""
#define I_RAIN    ""
#define I_BOLT    ""
#define I_SNOW    ""
#define I_SMOG    ""
#define I_GLOBE   ""
#define I_SYNC    ""
#define I_USERS   ""
#define I_MINUS   ""
#define I_PLUS    ""
#define I_CHECK   ""
#define I_BULB    ""
#define I_TV      ""
#define I_INFO    ""
#define I_HAND    ""
#define I_POWER   ""
#define I_BCAST   ""
#define I_BACK    ""

// ── themes ───────────────────────────────────────────────────────────────────────
// The colours of the control panel (assets/control.html): dark with the red accent, and the light
// theme in the blue of dbrobot.vn.

typedef struct {
    uint32_t bg, bar, bar_text, card, card2, line, text, muted;
    uint32_t acc;          // accent as text / border / icon colour
    uint32_t a1, a2;       // accent gradient (buttons, spectrum)
    uint32_t on_a;         // text on the gradient
    uint32_t ok, warn, err, sad, pink;
    uint32_t bar2;         // second colour of the top bar gradient (= bar when flat)
    bool light;
} pal_t;

static const pal_t PAL_DARK = {
    .bg = 0x07090E, .bar = 0x0B0D14, .bar_text = 0xCDD3E0, .card = 0x0F1219, .card2 = 0x161B26,
    .line = 0x252C3D, .text = 0xCDD3E0, .muted = 0x7B8AAD,
    .acc = 0xF0423D, .a1 = 0xDB1214, .a2 = 0xFF5A4A, .on_a = 0xFFFFFF,
    .ok = 0x22C55E, .warn = 0xF59E0B, .err = 0xEF4444, .sad = 0x93C5FD, .pink = 0xF472B6,
    .bar2 = 0x0B0D14, .light = false,
};
static const pal_t PAL_LIGHT = {
    .bg = 0xF4F8FF, .bar = 0x0B3FA8, .bar_text = 0xFFFFFF, .card = 0xFFFFFF, .card2 = 0xEEF4FF,
    .line = 0xDCE6F5, .text = 0x06214A, .muted = 0x46596F,
    .acc = 0x1462E8, .a1 = 0x0B3FA8, .a2 = 0x2C7BF2, .on_a = 0xFFFFFF,
    .ok = 0x16A34A, .warn = 0xD97706, .err = 0xDC2626, .sad = 0x2563EB, .pink = 0xDB2777,
    .bar2 = 0x2C7BF2, .light = true,
};
static const pal_t *P = &PAL_DARK;

#define C_BG     (P->bg)
#define C_CARD   (P->card)
#define C_CARD2  (P->card2)
#define C_LINE   (P->line)
#define C_TEXT   (P->text)
#define C_MUTED  (P->muted)
#define C_ACCENT (P->acc)
#define C_GREEN  (P->ok)
#define C_ORANGE (P->warn)
#define C_RED    (P->err)
#define C_PINK   (P->pink)

static void apply_palette(void) { P = g_cfg.light ? &PAL_LIGHT : &PAL_DARK; }

#define TOP_H 28
#define PAGE_H (LCD_V_RES - TOP_H)

typedef enum { P_STATUS, P_CHOOSE, P_VOICE, P_MUSIC, P_IDLE, P_COUNT } page_t;

enum { APP_THEME, APP_BRIGHT, APP_VOLUME, APP_CALL, APP_FIND, APP_SCREEN, APP_INFO, APP_TOUCH, APP_COUNT };

static spk_state_t st;           // the copy the UI works from
static uint32_t st_seq = 0xFFFFFFFF;
static page_t cur_page = P_COUNT;
static int64_t voice_until;      // keep the voice page a little after the talk ends
static int64_t suppress_voice_until;   // after "Tắt nghe": stay on the main page for a moment

static lv_obj_t *pages[P_COUNT];
static lv_obj_t *top_dot, *top_name, *top_right, *top_theme;
static lv_obj_t *tv, *tile_main, *tile_apps, *dot[2];
static lv_obj_t *app_icon_lbl[APP_COUNT];
/* status */
static lv_obj_t *s_icon, *s_title, *s_text;
/* choose */
static lv_obj_t *c_list;
static char c_ids[LINK_MAX_FOUND][48];
static int c_count = -1;
static uint32_t c_hash;
/* voice */
static lv_obj_t *v_face, *v_eye[2], *v_mouth_arc, *v_mouth_o, *v_state, *v_scroll, *v_user, *v_bot, *v_btn;
static int v_anim;
/* music */
static lv_obj_t *m_title, *m_artist, *m_bars[LINK_BANDS], *m_bar, *m_pos, *m_dur, *m_play;
static uint8_t m_level[LINK_BANDS];
/* idle */
static lv_obj_t *i_clock, *i_date, *i_wicon, *i_temp, *i_wdesc, *i_city, *i_info, *i_mute;
/* alarm */
static lv_obj_t *a_layer, *a_text;
/* settings sheet */
static lv_obj_t *sheet, *sheet_title, *sheet_body;
static int sheet_app = -1;
static lv_obj_t *vol_num, *vol_bar, *bri_num;

static void request_rebuild(void);
static void open_sheet(int app);
static void close_sheet(void);

// ── helpers ──────────────────────────────────────────────────────────────────────

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void cmd_cb(lv_event_t *e) { link_cmd((const char *)lv_event_get_user_data(e)); }

/* A button drawn from scratch (no theme styles to fight): primary = the accent gradient, otherwise
 * a bordered card. The label is the first child. */
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                        const lv_font_t *f, bool primary, const char *cmd)
{
    lv_obj_t *b = box(parent, x, y, w, h);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    if (primary) {
        lv_obj_set_style_bg_color(b, lv_color_hex(P->a1), 0);
        lv_obj_set_style_bg_grad_color(b, lv_color_hex(P->a2), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, 0);
    } else {
        lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD2), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
    }
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_t *l = label(b, f, primary ? P->on_a : C_TEXT, text);
    lv_obj_center(l);
    if (cmd) lv_obj_add_event_cb(b, cmd_cb, LV_EVENT_CLICKED, (void *)cmd);
    return b;
}

static void set_text(lv_obj_t *l, const char *t)
{
    const char *have = lv_label_get_text(l);
    if (!have || strcmp(have, t)) lv_label_set_text(l, t);
}

static void btn_text(lv_obj_t *b, const char *t) { set_text(lv_obj_get_child(b, 0), t); }

static bool clock_ok(void) { return time(NULL) > 1700000000; }

static const char *const WEEKDAY[] = { "Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy" };

static void fmt_time(char *out, int n, int secs)
{
    if (secs < 0) secs = 0;
    if (secs >= 3600) snprintf(out, n, "%d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60);
    else snprintf(out, n, "%d:%02d", secs / 60, secs % 60);
}

static const char *weather_icon(int code, bool day)
{
    if (code == 0) return day ? I_SUN : I_MOON;
    if (code >= 1 && code <= 3) return code == 1 ? (day ? I_SUN : I_MOON) : I_CLOUD;
    if (code == 45 || code == 48) return I_SMOG;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return I_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return I_SNOW;
    if (code >= 95) return I_BOLT;
    return I_CLOUD;
}

static bool music_active(void)
{
    return st.has_m && (!strcmp(st.ms, "playing") || !strcmp(st.ms, "paused") || !strcmp(st.ms, "downloading"));
}

static bool talking(void)
{
    if (!st.awake && strcmp(st.vs, "LISTENING") && strcmp(st.vs, "SPEAKING")) return false;
    if (!strcmp(st.vs, "LISTENING")) return true;
    // SPEAKING while a local song plays is the song, not the assistant
    if (!strcmp(st.vs, "SPEAKING")) return !(st.has_m && !strcmp(st.ms, "playing"));
    return st.awake && !music_active();
}

static bool apps_tile_active(void) { return tv && lv_tileview_get_tile_active(tv) == tile_apps; }

static void go_main(void)
{
    close_sheet();
    if (tv) lv_tileview_set_tile_by_index(tv, 0, 0, LV_ANIM_ON);
}

// ── backlight ────────────────────────────────────────────────────────────────────

static void backlight_update(void)
{
    int b = g_cfg.bright;
    if (g_cfg.night_dim && clock_ok()) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        if (tm.tm_hour >= 22 || tm.tm_hour < 6) b = b * 25 / 100;
    }
    // ringing, talking, or setting the brightness: the real level
    if (st.al_ring[0] || cur_page == P_VOICE || sheet_app == APP_BRIGHT) b = g_cfg.bright;
    if (b < 3) b = 3;
    static int last = -1;
    if (b != last) { display_backlight(b); last = b; }
}

// ── top bar ──────────────────────────────────────────────────────────────────────

static void top_long_press(lv_event_t *e)
{
    // switch speaker: forget the choice and look again
    g_cfg.spk_id[0] = 0;
    link_research();
    go_main();
}

static void theme_cb(lv_event_t *e)
{
    g_cfg.light = !g_cfg.light;
    cfg_save();
    request_rebuild();
}

static void build_top(lv_obj_t *scr)
{
    lv_obj_t *bar = box(scr, 0, 0, LCD_H_RES, TOP_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(P->bar), 0);
    if (P->light) {   // the blue navigation bar of dbrobot.vn
        lv_obj_set_style_bg_grad_color(bar, lv_color_hex(P->bar2), 0);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
    } else {
        lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_hex(C_LINE), 0);
    }
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, top_long_press, LV_EVENT_LONG_PRESSED, NULL);
    top_dot = box(bar, 8, 10, 8, 8);
    lv_obj_set_style_radius(top_dot, 4, 0);
    lv_obj_set_style_bg_opa(top_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_dot, lv_color_hex(C_MUTED), 0);
    top_name = label(bar, &font_vn14, P->bar_text, "DB-Robot");
    lv_obj_set_pos(top_name, 22, 5);
    lv_obj_set_width(top_name, 112);
    lv_label_set_long_mode(top_name, LV_LABEL_LONG_DOT);
    top_right = label(bar, &font_vn14, P->bar_text, "");
    lv_obj_align(top_right, LV_ALIGN_RIGHT_MID, -38, 0);
    // the sun / moon button, as in the panel's header: shows the mode you are in, tap to switch
    top_theme = box(bar, LCD_H_RES - 34, 0, 34, TOP_H);
    lv_obj_add_flag(top_theme, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ic = label(top_theme, &font_vn18, P->bar_text, P->light ? I_SUN : I_MOON);
    lv_obj_center(ic);
    lv_obj_add_event_cb(top_theme, theme_cb, LV_EVENT_CLICKED, NULL);
}

static void update_top(void)
{
    char right[112] = "", clk[16] = "--:--";
    if (clock_ok()) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        snprintf(clk, sizeof clk, "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
    if (st.phase == LINK_CONNECTED) {
        char grp[32] = "", vol[40] = "";
        if (st.group > 0) snprintf(grp, sizeof grp, I_USERS " %d   ", st.group + 1);
        if (st.vol >= 0) snprintf(vol, sizeof vol, "%s %d%%   ", st.vol == 0 ? I_VOLOFF : st.vol < 50 ? I_VOLDN : I_VOLUP, st.vol);
        snprintf(right, sizeof right, "%s%s%s%s", grp, st.mute ? I_MICOFF "   " : "", vol, clk);
        set_text(top_name, st.name[0] ? st.name : "Loa DB-Robot");
        lv_obj_set_style_bg_color(top_dot, lv_color_hex(P->light ? 0x7CF2A8 : C_GREEN), 0);
    } else {
        snprintf(right, sizeof right, "%s", clk);
        set_text(top_name, "DB-Robot");
        lv_obj_set_style_bg_color(top_dot, lv_color_hex(st.phase == LINK_LOST ? C_ORANGE : (P->light ? 0xBBD0F5 : C_MUTED)), 0);
    }
    set_text(top_right, right);
}

// ── status page ──────────────────────────────────────────────────────────────────

static void build_status(lv_obj_t *p)
{
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 8, 0);
    lv_obj_set_style_pad_bottom(p, 10, 0);
    s_icon = label(p, &font_vn24, C_ACCENT, I_WIFI);
    s_title = label(p, &font_vn24, C_TEXT, "");
    lv_obj_set_width(s_title, 300);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    s_text = label(p, &font_vn14, C_MUTED, "");
    lv_obj_set_width(s_text, 296);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
}

static void update_status(void)
{
    char ip[16], t[300];
    net_ip(ip, sizeof ip);
    net_state_t ns = net_state();
    if (ns == NET_SETUP) {
        set_text(s_icon, I_WIFI);
        set_text(s_title, g_cfg.ssid[0] ? "Chưa vào được Wi-Fi" : "Cài đặt Wi-Fi");
        snprintf(t, sizeof t, "Dùng điện thoại vào Wi-Fi\n%s\nrồi mở trình duyệt: 192.168.4.1\n(trang cài đặt thường tự mở ra)", net_ap_ssid());
    } else if (ns == NET_CONNECTING) {
        set_text(s_icon, I_WIFI);
        set_text(s_title, "Đang vào Wi-Fi…");
        snprintf(t, sizeof t, "%s", g_cfg.ssid);
    } else if (st.phase == LINK_LOST) {
        set_text(s_icon, I_SYNC);
        set_text(s_title, "Mất kết nối với loa");
        snprintf(t, sizeof t, "%s — đang tìm lại…\nKiểm tra loa còn bật và chung mạng Wi-Fi.\nCài đặt màn hình: http://%s", st.name, ip);
    } else {
        set_text(s_icon, I_SYNC);
        set_text(s_title, "Đang tìm loa…");
        snprintf(t, sizeof t, "Loa Phicomm R1 (DB-Robot 1.5.15 trở lên) cần bật và chung mạng Wi-Fi với màn hình.\nCài đặt màn hình: http://%s", ip);
    }
    set_text(s_text, t);
}

// ── choose page ──────────────────────────────────────────────────────────────────

static void choose_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < c_count) link_choose(c_ids[i]);
}

static void build_choose(lv_obj_t *p)
{
    lv_obj_t *t = label(p, &font_vn18, C_TEXT, "Chọn loa để hiển thị");
    lv_obj_set_pos(t, 12, 6);
    c_list = lv_obj_create(p);
    lv_obj_remove_style_all(c_list);
    lv_obj_set_pos(c_list, 8, 36);
    lv_obj_set_size(c_list, LCD_H_RES - 16, PAGE_H - 48);
    lv_obj_set_style_pad_row(c_list, 6, 0);
    lv_obj_set_flex_flow(c_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(c_list, LV_SCROLLBAR_MODE_OFF);
}

static void update_choose(void)
{
    speaker_t f[LINK_MAX_FOUND];
    int n = link_found(f, LINK_MAX_FOUND);
    uint32_t h = n;
    for (int i = 0; i < n; i++) for (const char *s = f[i].id; *s; s++) h = h * 31 + (uint8_t)*s;
    for (int i = 0; i < n; i++) for (const char *s = f[i].name; *s; s++) h = h * 31 + (uint8_t)*s;
    if (n == c_count && h == c_hash) return;
    c_count = n;
    c_hash = h;
    lv_obj_clean(c_list);
    for (int i = 0; i < n; i++) {
        strcpy(c_ids[i], f[i].id);
        lv_obj_t *b = lv_obj_create(c_list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, lv_pct(100), 48);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_radius(b, 10, 0);
        char t[96];
        snprintf(t, sizeof t, "%s", f[i].name[0] ? f[i].name : "Loa DB-Robot");
        lv_obj_t *l = label(b, &font_vn18, C_TEXT, t);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, -7);
        snprintf(t, sizeof t, "%s · %s%s", f[i].ip, f[i].ver, f[i].playing ? " · " I_MUSIC : "");
        l = label(b, &font_vn14, C_MUTED, t);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 12);
        lv_obj_add_event_cb(b, choose_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// ── voice page ───────────────────────────────────────────────────────────────────

static void voice_btn_cb(lv_event_t *e)
{
    if (st.awake || talking()) {
        // "Tắt nghe": end the conversation (sleep), stop listening for the wake word (mute, speakers
        // from 1.5.16; an older one just ends the conversation), and go back to the main page.
        link_cmd("sleep");
        link_cmd("mute");
        voice_until = 0;
        suppress_voice_until = NOW_US() + 2500000;
    } else {
        link_cmd("wake");
    }
}

static void build_voice(lv_obj_t *p)
{
    v_face = box(p, 8, 10, 116, 116);
    lv_obj_set_style_radius(v_face, 58, 0);
    lv_obj_set_style_bg_color(v_face, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(v_face, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(v_face, 3, 0);
    lv_obj_set_style_border_color(v_face, lv_color_hex(C_ACCENT), 0);
    for (int i = 0; i < 2; i++) {
        v_eye[i] = box(v_face, 0, 0, 20, 30);
        lv_obj_set_style_radius(v_eye[i], 10, 0);
        lv_obj_set_style_bg_opa(v_eye[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(v_eye[i], lv_color_hex(C_TEXT), 0);
    }
    v_mouth_arc = lv_arc_create(v_face);
    lv_obj_remove_style(v_mouth_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(v_mouth_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(v_mouth_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(v_mouth_arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(v_mouth_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(v_mouth_arc, lv_color_hex(C_TEXT), LV_PART_MAIN);
    lv_obj_set_style_pad_all(v_mouth_arc, 0, 0);
    v_mouth_o = box(v_face, 0, 0, 18, 18);
    lv_obj_set_style_radius(v_mouth_o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(v_mouth_o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(v_mouth_o, lv_color_hex(C_TEXT), 0);

    v_state = label(p, &font_vn14, C_ACCENT, "");
    lv_obj_set_width(v_state, 132);
    lv_obj_set_style_text_align(v_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(v_state, 0, 130);
    v_btn = button(p, 12, 158, 108, 38, I_MICOFF "  Tắt nghe", &font_vn14, false, NULL);
    lv_obj_add_event_cb(v_btn, voice_btn_cb, LV_EVENT_CLICKED, NULL);

    v_scroll = box(p, 134, 4, LCD_H_RES - 140, PAGE_H - 24);
    lv_obj_add_flag(v_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(v_scroll, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(v_scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(v_scroll, 1, 0);
    lv_obj_set_style_border_color(v_scroll, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_radius(v_scroll, 10, 0);
    lv_obj_set_style_pad_all(v_scroll, 8, 0);
    lv_obj_set_style_pad_row(v_scroll, 6, 0);
    lv_obj_set_flex_flow(v_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(v_scroll, LV_SCROLLBAR_MODE_OFF);
    v_user = label(v_scroll, &font_vn14, C_MUTED, "");
    lv_obj_set_width(v_user, lv_pct(100));
    lv_label_set_long_mode(v_user, LV_LABEL_LONG_WRAP);
    v_bot = label(v_scroll, &font_vn18, C_TEXT, "");
    lv_obj_set_width(v_bot, lv_pct(100));
    lv_label_set_long_mode(v_bot, LV_LABEL_LONG_WRAP);
}

/* The face: eye height, eye lift, mouth (smile > 0, frown < 0, 'o' = 99, flat = 0), colour. */
static void face(int eye_h, int lift, int mouth, uint32_t color, int open)
{
    const int cx = 58 - 3, cy = 58 - 3;   // inside the border
    for (int i = 0; i < 2; i++) {
        int w = eye_h > 30 ? 24 : 20;
        lv_obj_set_size(v_eye[i], w, eye_h);
        lv_obj_set_style_radius(v_eye[i], eye_h < 10 ? 3 : w / 2, 0);
        lv_obj_set_pos(v_eye[i], cx + (i ? 14 : -14 - w), cy - 14 - eye_h / 2 - lift);
        lv_obj_set_style_bg_color(v_eye[i], lv_color_hex(color), 0);
    }
    lv_obj_set_style_border_color(v_face, lv_color_hex(color == C_TEXT ? C_ACCENT : color), 0);
    if (mouth == 99 || open > 0) {
        int d = mouth == 99 ? 18 : 6 + open;
        lv_obj_remove_flag(v_mouth_o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(v_mouth_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(v_mouth_o, mouth == 99 ? 18 : 26, d);
        lv_obj_set_style_radius(v_mouth_o, mouth == 99 ? LV_RADIUS_CIRCLE : 8, 0);
        lv_obj_set_pos(v_mouth_o, cx - (mouth == 99 ? 9 : 13), cy + 22 - d / 2);
        lv_obj_set_style_bg_color(v_mouth_o, lv_color_hex(color), 0);
        return;
    }
    lv_obj_add_flag(v_mouth_o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(v_mouth_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_arc_color(v_mouth_arc, lv_color_hex(color), LV_PART_MAIN);
    if (mouth > 0) {          // smile: the lower part of a circle
        int r = mouth > 1 ? 22 : 18;
        lv_obj_set_size(v_mouth_arc, r * 2, r * 2);
        lv_obj_set_pos(v_mouth_arc, cx - r, cy + 4 - r + (mouth > 1 ? 0 : 4));
        lv_arc_set_bg_angles(v_mouth_arc, 30, 150);
    } else if (mouth < 0) {   // frown: the upper part
        lv_obj_set_size(v_mouth_arc, 36, 36);
        lv_obj_set_pos(v_mouth_arc, cx - 18, cy + 26);
        lv_arc_set_bg_angles(v_mouth_arc, 215, 325);
    } else {                  // flat: a very wide, shallow smile
        lv_obj_set_size(v_mouth_arc, 80, 80);
        lv_obj_set_pos(v_mouth_arc, cx - 40, cy + 22 - 80 + 4);
        lv_arc_set_bg_angles(v_mouth_arc, 76, 104);
    }
}

static void update_face(void)
{
    const char *e = st.emo;
    bool listening = !strcmp(st.vs, "LISTENING");
    bool speaking = !strcmp(st.vs, "SPEAKING");
    int eye = 30, lift = 0, mouth = 1, open = 0;
    uint32_t col = C_TEXT;
    if (!strcmp(e, "happy") || !strcmp(e, "laughing") || !strcmp(e, "funny") || !strcmp(e, "loving") ||
        !strcmp(e, "delicious") || !strcmp(e, "kissy") || !strcmp(e, "silly")) { eye = 14; mouth = 2; }
    else if (!strcmp(e, "sad") || !strcmp(e, "crying")) { eye = 22; mouth = -1; col = P->sad; }
    else if (!strcmp(e, "angry")) { eye = 16; mouth = -1; col = P->err; }
    else if (!strcmp(e, "surprised") || !strcmp(e, "shocked")) { eye = 36; mouth = 99; }
    else if (!strcmp(e, "thinking") || !strcmp(e, "confused")) { eye = 24; lift = 6; mouth = 0; }
    else if (!strcmp(e, "sleepy") || !strcmp(e, "relaxed")) { eye = 6; mouth = 0; }
    else if (!strcmp(e, "cool") || !strcmp(e, "confident") || !strcmp(e, "winking")) { eye = 12; mouth = 1; }
    else if (!strcmp(e, "embarrassed")) { eye = 20; mouth = 0; col = P->pink; }
    if (listening) { col = P->ok; mouth = mouth == 99 ? 99 : 0; eye = eye < 24 ? 26 : eye; }
    v_anim++;
    if (speaking && mouth != 99) open = (int[]){ 2, 8, 14, 6, 12, 4, 10, 16 }[v_anim % 8];
    // blink now and then
    if (!speaking && (v_anim % 40) == 0) eye = 4;
    face(eye, lift, mouth, col, open);
    set_text(v_state, listening ? "Đang nghe…" : speaking ? "Đang nói…" : st.awake ? "Đang nghĩ…" : "");
    // while awake the button ends the conversation; otherwise it calls the speaker
    btn_text(v_btn, (st.awake || talking()) ? I_MICOFF "  Tắt nghe" : I_MIC "  Gọi loa");
}

static void update_voice_text(void)
{
    char u[920];
    snprintf(u, sizeof u, "%s", st.user[0] ? st.user : "");
    set_text(v_user, u);
    set_text(v_bot, st.bot);
    lv_obj_update_layout(v_scroll);
    int32_t b = lv_obj_get_scroll_bottom(v_scroll);
    if (b > 0) lv_obj_scroll_by(v_scroll, 0, -b, LV_ANIM_OFF);
}

// ── music page ───────────────────────────────────────────────────────────────────

static void play_cb(lv_event_t *e) { link_cmd(!strcmp(st.ms, "playing") ? "pause" : "resume"); }

static void build_music(lv_obj_t *p)
{
    m_title = label(p, &font_vn24, C_TEXT, "");
    lv_obj_set_pos(m_title, 10, 2);
    lv_obj_set_width(m_title, LCD_H_RES - 20);
    lv_label_set_long_mode(m_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    m_artist = label(p, &font_vn14, C_MUTED, "");
    lv_obj_set_pos(m_artist, 10, 36);
    lv_obj_set_width(m_artist, LCD_H_RES - 20);
    lv_label_set_long_mode(m_artist, LV_LABEL_LONG_DOT);
    const int bw = 7, gap = 2, x0 = (LCD_H_RES - LINK_BANDS * (bw + gap) + gap) / 2;
    for (int i = 0; i < LINK_BANDS; i++) {
        m_bars[i] = box(p, x0 + i * (bw + gap), 132, bw, 2);
        lv_obj_set_style_radius(m_bars[i], 2, 0);
        lv_obj_set_style_bg_opa(m_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(m_bars[i],
            lv_color_mix(lv_color_hex(P->a2), lv_color_hex(P->a1), (uint8_t)(i * 255 / (LINK_BANDS - 1))), 0);
    }
    m_pos = label(p, &font_vn14, C_MUTED, "0:00");
    lv_obj_set_pos(m_pos, 10, 136);
    m_dur = label(p, &font_vn14, C_MUTED, "");
    lv_obj_align(m_dur, LV_ALIGN_TOP_RIGHT, -10, 136);
    m_bar = lv_bar_create(p);
    lv_obj_remove_style_all(m_bar);
    lv_obj_set_pos(m_bar, 58, 145);
    lv_obj_set_size(m_bar, LCD_H_RES - 116, 5);
    lv_obj_set_style_radius(m_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_bar, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_radius(m_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(m_bar, lv_color_hex(P->a1), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(m_bar, lv_color_hex(P->a2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(m_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_bar_set_range(m_bar, 0, 1000);
    const int y = 160, w = 56, g = 6, x = (LCD_H_RES - 5 * w - 4 * g) / 2;
    button(p, x, y, w, 38, I_VOLDN, &font_vn18, false, "vol-");
    m_play = button(p, x + (w + g), y, w, 38, I_PAUSE, &font_vn18, true, NULL);
    lv_obj_add_event_cb(m_play, play_cb, LV_EVENT_CLICKED, NULL);
    button(p, x + 2 * (w + g), y, w, 38, I_NEXT, &font_vn18, false, "next");
    button(p, x + 3 * (w + g), y, w, 38, I_STOP, &font_vn18, false, "stop");
    button(p, x + 4 * (w + g), y, w, 38, I_VOLUP, &font_vn18, false, "vol+");
}

static void update_music(void)
{
    set_text(m_title, st.ti[0] ? st.ti : "…");
    char a[340];
    const char *state = !strcmp(st.ms, "downloading") ? "Đang tải… · " : !strcmp(st.ms, "paused") ? "Tạm dừng · " : "";
    snprintf(a, sizeof a, I_MUSIC " %s%s%s%s", state, st.src, st.ar[0] ? " · " : "", st.ar);
    set_text(m_artist, a);
    btn_text(m_play, !strcmp(st.ms, "playing") ? I_PAUSE : I_PLAY);
}

static void update_progress(void)
{
    int pos = st.pos;
    if (!strcmp(st.ms, "playing")) pos += (int)((NOW_US() - st.got_us) / 1000000);
    if (st.dur > 0 && pos > st.dur) pos = st.dur;
    char t[16];
    fmt_time(t, sizeof t, pos);
    set_text(m_pos, t);
    if (st.dur > 0) {
        fmt_time(t, sizeof t, st.dur);
        set_text(m_dur, t);
        lv_bar_set_value(m_bar, pos * 1000 / st.dur, LV_ANIM_OFF);
        lv_obj_remove_flag(m_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_text(m_dur, "LIVE");
        lv_obj_add_flag(m_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void spectrum_timer(lv_timer_t *t)
{
    if (cur_page != P_MUSIC || !m_bars[0]) return;
    uint8_t in[LINK_BANDS];
    link_spectrum(in);
    bool playing = !strcmp(st.ms, "playing");
    for (int i = 0; i < LINK_BANDS; i++) {
        int v = playing ? in[i] : 0;
        // rise at once, fall slowly
        int lv = m_level[i];
        lv = v > lv ? v : (lv > 12 ? lv - 12 : 0);
        if (lv == m_level[i] && t) continue;
        m_level[i] = lv;
        int h = 2 + lv * 80 / 255;
        lv_obj_set_height(m_bars[i], h);
        lv_obj_set_y(m_bars[i], 132 - h);
    }
}

// ── idle page ────────────────────────────────────────────────────────────────────

static void mute_cb(lv_event_t *e) { link_cmd(st.mute ? "unmute" : "mute"); }

static void build_idle(lv_obj_t *p)
{
    i_clock = label(p, &font_clock72, C_TEXT, "--:--");
    lv_obj_align(i_clock, LV_ALIGN_TOP_MID, 0, -2);
    i_date = label(p, &font_vn18, C_MUTED, "");
    lv_obj_align(i_date, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *w = box(p, 0, 94, LCD_H_RES, 46);
    lv_obj_set_flex_flow(w, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(w, 10, 0);
    i_wicon = label(w, &font_icon40, C_ORANGE, "");
    i_temp = label(w, &font_vn24, C_TEXT, "");
    lv_obj_t *col = box(w, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    i_wdesc = label(col, &font_vn14, C_TEXT, "");
    i_city = label(col, &font_vn14, C_MUTED, "");

    i_info = label(p, &font_vn14, C_MUTED, "");
    lv_obj_set_width(i_info, LCD_H_RES - 20);
    lv_obj_set_style_text_align(i_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(i_info, LV_LABEL_LONG_DOT);
    lv_obj_align(i_info, LV_ALIGN_TOP_MID, 0, 142);

    // the panel's two floating buttons: Tắt nghe / Bật nghe on the left, Gọi loa on the right
    i_mute = button(p, 10, 164, 146, 40, I_MICOFF "  Tắt nghe", &font_vn14, false, NULL);
    lv_obj_add_event_cb(i_mute, mute_cb, LV_EVENT_CLICKED, NULL);
    button(p, LCD_H_RES - 156, 164, 146, 40, I_MIC "  Gọi loa", &font_vn14, true, "wake");
}

static void update_idle(void)
{
    char t[160];
    if (clock_ok()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(t, sizeof t, "%02d:%02d", tm.tm_hour, tm.tm_min);
        set_text(i_clock, t);
        snprintf(t, sizeof t, "%s, %d tháng %d, %d", WEEKDAY[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
        set_text(i_date, t);
    } else {
        set_text(i_clock, "--:--");
        set_text(i_date, "");
    }
    if (st.has_wx) {
        set_text(i_wicon, weather_icon(st.wx_c, st.wx_day));
        lv_obj_set_style_text_color(i_wicon, lv_color_hex(st.wx_c <= 1 ? (st.wx_day ? C_ORANGE : (P->light ? 0x6B7FB0 : 0xFDE68A)) : (P->light ? 0x6B7FB0 : 0xCBD5E1)), 0);
        snprintf(t, sizeof t, "%d°C", st.wx_t);
        set_text(i_temp, t);
        set_text(i_wdesc, st.wx_d);
        set_text(i_city, st.wx_city);
    } else {
        set_text(i_wicon, "");
        set_text(i_temp, "");
        set_text(i_wdesc, "Chưa có thời tiết");
        set_text(i_city, "(đặt vị trí trong trang loa)");
    }
    if (st.al_next[0]) snprintf(t, sizeof t, I_BELL "  %s", st.al_next);
    else snprintf(t, sizeof t, I_GLOBE "  %s · DB-Robot %s", st.ip, st.ver);
    set_text(i_info, t);
    btn_text(i_mute, st.mute ? I_MIC "  Bật nghe" : I_MICOFF "  Tắt nghe");
}

// ── alarm overlay ────────────────────────────────────────────────────────────────

static void build_alarm(void)
{
    a_layer = box(lv_layer_top(), 0, 0, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_opa(a_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(a_layer, lv_color_hex(0x7C2D12), 0);
    lv_obj_add_flag(a_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ic = label(a_layer, &font_icon40, 0xFDE68A, I_BELL);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_t *h = label(a_layer, &font_vn24, 0xFFFFFF, "Báo thức");
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 68);
    a_text = label(a_layer, &font_vn18, 0xFED7AA, "");
    lv_obj_set_width(a_text, 300);
    lv_label_set_long_mode(a_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(a_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a_text, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_t *b = box(a_layer, 60, 170, 200, 52);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xEA580C), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_t *l = label(b, &font_vn18, 0xFFFFFF, "TẮT BÁO THỨC");
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cmd_cb, LV_EVENT_CLICKED, "alarm_stop");
    lv_obj_add_flag(a_layer, LV_OBJ_FLAG_HIDDEN);
}

static void update_alarm(void)
{
    bool ring = st.phase == LINK_CONNECTED && st.al_ring[0];
    if (ring) {
        set_text(a_text, st.al_ring);
        lv_obj_remove_flag(a_layer, LV_OBJ_FLAG_HIDDEN);
        static int k;
        k++;
        lv_obj_set_style_bg_color(a_layer, lv_color_hex(k % 10 < 5 ? 0x7C2D12 : 0x9A3412), 0);
    } else {
        lv_obj_add_flag(a_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── apps: the launcher and the settings sheets ───────────────────────────────────

typedef struct { const char *icon; const char *label; uint32_t color; } app_t;

static const app_t APPS[APP_COUNT] = {
    [APP_THEME]  = { I_MOON,  "Giao diện", 0xF59E0B },
    [APP_BRIGHT] = { I_BULB,  "Độ sáng",   0xEAB308 },
    [APP_VOLUME] = { I_VOLUP, "Âm lượng",  0x16A34A },
    [APP_CALL]   = { I_MIC,   "Gọi loa",   0xEF4444 },
    [APP_FIND]   = { I_BCAST, "Tìm loa",   0x8B5CF6 },
    [APP_SCREEN] = { I_TV,    "Màn hình",  0x0891B2 },
    [APP_INFO]   = { I_INFO,  "Thông tin", 0x2563EB },
    [APP_TOUCH]  = { I_HAND,  "Cảm ứng",   0xDB2777 },
};

static void app_cb(lv_event_t *e)
{
    int app = (int)(intptr_t)lv_event_get_user_data(e);
    switch (app) {
    case APP_THEME:
        g_cfg.light = !g_cfg.light;
        cfg_save();
        request_rebuild();
        break;
    case APP_CALL:
        link_cmd("wake");
        go_main();
        break;
    case APP_FIND:
        g_cfg.spk_id[0] = 0;
        link_research();
        go_main();
        break;
    default:
        open_sheet(app);
    }
}

static void build_apps(lv_obj_t *p)
{
    const int cw = LCD_H_RES / 4, ch = 100;
    for (int i = 0; i < APP_COUNT; i++) {
        int x = (i % 4) * cw, y = 4 + (i / 4) * ch;
        lv_obj_t *cell = box(p, x + 3, y, cw - 6, ch - 4);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(cell, 14, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(C_CARD2), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_t *sq = box(cell, (cw - 6 - 54) / 2, 8, 54, 54);
        lv_obj_remove_flag(sq, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(sq, 15, 0);
        lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(sq, lv_color_hex(APPS[i].color), 0);
        lv_obj_set_style_bg_grad_color(sq, lv_color_mix(lv_color_white(), lv_color_hex(APPS[i].color), 70), 0);
        lv_obj_set_style_bg_grad_dir(sq, LV_GRAD_DIR_VER, 0);
        const char *ic = APPS[i].icon;
        if (i == APP_THEME) ic = P->light ? I_SUN : I_MOON;
        app_icon_lbl[i] = label(sq, &font_icon30, 0xFFFFFF, ic);
        lv_obj_center(app_icon_lbl[i]);
        lv_obj_t *l = label(cell, &font_vn14, C_TEXT, APPS[i].label);
        lv_obj_set_width(l, cw - 6);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, 0, 68);
        lv_obj_add_event_cb(cell, app_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// the sheet: a back arrow and a title above the body

static void back_cb(lv_event_t *e) { close_sheet(); }

static void build_sheet(lv_obj_t *scr)
{
    sheet = box(scr, 0, TOP_H, LCD_H_RES, PAGE_H);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(C_BG), 0);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);       // nothing underneath hears the touches
    lv_obj_t *back = box(sheet, 4, 2, 52, 34);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(C_CARD2), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t *ic = label(back, &font_vn18, C_ACCENT, I_BACK);
    lv_obj_center(ic);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    sheet_title = label(sheet, &font_vn18, C_TEXT, "");
    lv_obj_set_pos(sheet_title, 66, 8);
    sheet_body = box(sheet, 0, 40, LCD_H_RES, PAGE_H - 40);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN);
    sheet_app = -1;
}

static void close_sheet(void)
{
    if (!sheet) return;
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN);
    sheet_app = -1;
    vol_num = vol_bar = bri_num = NULL;
}

static lv_obj_t *note(lv_obj_t *parent, int x, int y, int w, const char *text, uint32_t color)
{
    lv_obj_t *l = label(parent, &font_vn14, color, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void style_slider(lv_obj_t *s)
{
    lv_obj_set_style_bg_color(s, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(P->a1), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s, lv_color_hex(P->a2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(P->light ? 0xFFFFFF : 0xE8ECF6), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(s, 3, LV_PART_KNOB);
    lv_obj_set_style_border_color(s, lv_color_hex(C_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
}

static void style_switch(lv_obj_t *s)
{
    lv_obj_set_style_bg_color(s, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
}

/* brightness */
static void bri_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    g_cfg.bright = (uint8_t)v;
    display_backlight(v);
    char t[16];
    snprintf(t, sizeof t, "%d%%", v);
    if (bri_num) lv_label_set_text(bri_num, t);
}

static void bri_done_cb(lv_event_t *e) { cfg_save(); }

static void sheet_brightness(lv_obj_t *b)
{
    char t[16];
    snprintf(t, sizeof t, "%d%%", g_cfg.bright);
    bri_num = label(b, &font_num52, C_TEXT, t);
    lv_obj_align(bri_num, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *s = lv_slider_create(b);
    lv_obj_set_size(s, 250, 14);
    lv_obj_set_pos(s, 35, 90);
    lv_slider_set_range(s, 5, 100);
    lv_slider_set_value(s, g_cfg.bright, LV_ANIM_OFF);
    style_slider(s);
    lv_obj_add_event_cb(s, bri_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, bri_done_cb, LV_EVENT_RELEASED, NULL);
    note(b, 16, 124, 288, "Ban đêm (22:00–6:00) màn hình tự giảm sáng nếu bật trong mục Màn hình.", C_MUTED);
}

/* volume of the speaker */
static void sheet_volume(lv_obj_t *b)
{
    vol_num = label(b, &font_num52, C_TEXT, "--");
    lv_obj_align(vol_num, LV_ALIGN_TOP_MID, 0, 4);
    vol_bar = lv_bar_create(b);
    lv_obj_remove_style_all(vol_bar);
    lv_obj_set_size(vol_bar, 250, 8);
    lv_obj_set_pos(vol_bar, 35, 78);
    lv_obj_set_style_radius(vol_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(vol_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(vol_bar, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_radius(vol_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(vol_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vol_bar, lv_color_hex(P->a1), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(vol_bar, lv_color_hex(P->a2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(vol_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_bar_set_range(vol_bar, 0, 100);
    button(b, 36, 100, 110, 56, I_MINUS, &font_vn24, false, "vol-");
    button(b, 174, 100, 110, 56, I_PLUS, &font_vn24, true, "vol+");
}

static void update_volume_sheet(void)
{
    if (!vol_num) return;
    char t[16];
    if (st.vol >= 0) snprintf(t, sizeof t, "%d%%", st.vol); else snprintf(t, sizeof t, "--");
    set_text(vol_num, t);
    lv_bar_set_value(vol_bar, st.vol < 0 ? 0 : st.vol, LV_ANIM_OFF);
}

/* screen */
static lv_obj_t *row_switch(lv_obj_t *parent, int y, const char *text, bool on, lv_event_cb_t cb)
{
    lv_obj_t *r = box(parent, 8, y, LCD_H_RES - 16, 38);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(C_LINE), 0);
    lv_obj_t *l = label(r, &font_vn14, C_TEXT, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *s = lv_switch_create(r);
    lv_obj_set_size(s, 46, 24);
    lv_obj_align(s, LV_ALIGN_RIGHT_MID, -10, 0);
    style_switch(s);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return s;
}

static void sw_theme_cb(lv_event_t *e)
{
    g_cfg.light = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_save();
    request_rebuild();
}

static void sw_flip_cb(lv_event_t *e)
{
    g_cfg.rot180 = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_save();
    display_set_flip(g_cfg.rot180);
}

static void sw_invert_cb(lv_event_t *e)
{
    g_cfg.invert = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_save();
    display_set_invert(g_cfg.invert);
}

static void sw_night_cb(lv_event_t *e)
{
    g_cfg.night_dim = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_save();
}

static void sheet_screen(lv_obj_t *b)
{
    row_switch(b, 0, "Giao diện sáng (tắt = tối)", g_cfg.light, sw_theme_cb);
    row_switch(b, 42, "Xoay ngược 180°", g_cfg.rot180, sw_flip_cb);
    row_switch(b, 84, "Đảo màu (nếu màu bị âm bản)", g_cfg.invert, sw_invert_cb);
    row_switch(b, 126, "Giảm sáng ban đêm 22:00–6:00", g_cfg.night_dim, sw_night_cb);
}

/* info */
static void restart_cb(lv_event_t *e) { esp_restart(); }

static void sheet_info(lv_obj_t *b)
{
    char ip[16], t[400];
    net_ip(ip, sizeof ip);
    int n = 0;
    if (st.phase == LINK_CONNECTED)
        n += snprintf(t + n, sizeof t - n, "Loa: %s · %s\nDB-Robot %s\n", st.name, st.ip, st.ver);
    else
        n += snprintf(t + n, sizeof t - n, "Loa: chưa nối\n");
    if (ip[0]) {
        int rssi = net_rssi();
        n += snprintf(t + n, sizeof t - n, "Wi-Fi: %s (%d dBm)\n", g_cfg.ssid, rssi);
        n += snprintf(t + n, sizeof t - n, "Màn hình: %s · v%s\nCài đặt: http://%s", ip, FW_VERSION, ip);
    } else {
        n += snprintf(t + n, sizeof t - n, "Wi-Fi: chưa vào mạng\nMàn hình: v%s", FW_VERSION);
    }
    note(b, 14, 0, 292, t, C_TEXT);
    lv_obj_t *r = button(b, 60, 118, 200, 38, I_POWER "  Khởi động lại", &font_vn14, false, NULL);
    lv_obj_add_event_cb(r, restart_cb, LV_EVENT_CLICKED, NULL);
}

/* touch calibration: runs at start-up, so ask for a restart */
static void calib_cb(lv_event_t *e)
{
    g_cfg.want_cal = 1;
    cfg_save();
    esp_restart();
}

static void sheet_touch(lv_obj_t *b)
{
    note(b, 14, 2, 292, "Màn hình khởi động lại rồi hiện 3 dấu +. Chạm vào giữa từng dấu để chỉnh cảm ứng cho chính xác.", C_TEXT);
    lv_obj_t *g = button(b, 40, 96, 240, 46, I_HAND "  Bắt đầu hiệu chỉnh", &font_vn18, true, NULL);
    lv_obj_add_event_cb(g, calib_cb, LV_EVENT_CLICKED, NULL);
}

static void open_sheet(int app)
{
    if (!sheet) return;
    lv_obj_clean(sheet_body);
    vol_num = vol_bar = bri_num = NULL;
    sheet_app = app;
    lv_label_set_text(sheet_title, APPS[app].label);
    switch (app) {
    case APP_BRIGHT: sheet_brightness(sheet_body); break;
    case APP_VOLUME: sheet_volume(sheet_body); update_volume_sheet(); break;
    case APP_SCREEN: sheet_screen(sheet_body); break;
    case APP_INFO:   sheet_info(sheet_body); break;
    case APP_TOUCH:  sheet_touch(sheet_body); break;
    default: break;
    }
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_HIDDEN);
}

// ── page choice, timers, rebuild ─────────────────────────────────────────────────

static void show(page_t p)
{
    if (p == cur_page) return;
    for (int i = 0; i < P_COUNT; i++) {
        if (i == p) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    cur_page = p;
    if (p == P_MUSIC) memset(m_level, 0, sizeof m_level), spectrum_timer(NULL);
}

static page_t choose_page(void)
{
    if (net_state() != NET_ONLINE) return P_STATUS;
    if (st.phase == LINK_CHOOSE) return P_CHOOSE;
    if (st.phase != LINK_CONNECTED) return P_STATUS;
    int64_t now = NOW_US();
    bool quiet = now < suppress_voice_until;     // just told to stop listening
    if (!quiet && talking()) {
        voice_until = now + 6 * 1000000LL;
        return P_VOICE;
    }
    if (!quiet && now < voice_until && !music_active()) return P_VOICE;
    if (music_active()) return P_MUSIC;
    return P_IDLE;
}

static void update_dots(void)
{
    bool apps = apps_tile_active();
    for (int i = 0; i < 2; i++) {
        bool on = (i == 1) == apps;
        lv_obj_set_size(dot[i], on ? 16 : 6, 6);
        lv_obj_set_x(dot[i], i == 0 ? (on ? LCD_H_RES / 2 - 16 : LCD_H_RES / 2 - 10) : (on ? LCD_H_RES / 2 + 1 : LCD_H_RES / 2 + 4));
        lv_obj_set_style_bg_color(dot[i], lv_color_hex(on ? C_ACCENT : C_LINE), 0);
    }
}

static void tick_timer(lv_timer_t *t)
{
    static int n;
    bool fresh = link_state(&st, &st_seq);
    page_t p = choose_page();
    show(p);
    update_top();
    switch (p) {
    case P_STATUS: update_status(); break;
    case P_CHOOSE: update_choose(); break;
    case P_VOICE:
        update_face();
        if (fresh || (n % 10) == 0) update_voice_text();
        break;
    case P_MUSIC:
        if (fresh) update_music();
        update_progress();
        break;
    case P_IDLE: if (fresh || (n % 10) == 0) update_idle(); break;
    default: break;
    }
    if (p != P_CHOOSE) c_count = -1;
    update_alarm();
    update_dots();
    if (sheet_app == APP_VOLUME) update_volume_sheet();

    // someone starts talking to the speaker, or nobody has touched the screen for a while:
    // leave the launcher and its sheets and go back to the main page
    static bool was_talking;
    bool tk = st.phase == LINK_CONNECTED && talking() && NOW_US() >= suppress_voice_until;
    if (tk && !was_talking && (apps_tile_active() || sheet_app >= 0)) go_main();
    was_talking = tk;
    if ((apps_tile_active() || sheet_app >= 0) && lv_display_get_inactive_time(NULL) > 45000) go_main();

    if ((n++ % 10) == 0) backlight_update();
}

static void build_all(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    cur_page = P_COUNT;
    c_count = -1;
    st_seq = 0xFFFFFFFF;          // the new widgets get the whole state again
    memset(m_level, 0, sizeof m_level);

    build_top(scr);
    tv = lv_tileview_create(scr);
    lv_obj_remove_style_all(tv);
    lv_obj_set_pos(tv, 0, TOP_H);
    lv_obj_set_size(tv, LCD_H_RES, PAGE_H);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    tile_main = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    tile_apps = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
    for (int i = 0; i < P_COUNT; i++) {
        pages[i] = box(tile_main, 0, 0, LCD_H_RES, PAGE_H);
        lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    build_status(pages[P_STATUS]);
    build_choose(pages[P_CHOOSE]);
    build_voice(pages[P_VOICE]);
    build_music(pages[P_MUSIC]);
    build_idle(pages[P_IDLE]);
    build_apps(tile_apps);
    for (int i = 0; i < 2; i++) {
        dot[i] = box(scr, LCD_H_RES / 2, LCD_V_RES - 9, 6, 6);
        lv_obj_remove_flag(dot[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(dot[i], 3, 0);
        lv_obj_set_style_bg_opa(dot[i], LV_OPA_COVER, 0);
    }
    build_sheet(scr);              // after the dots: it covers them while it is open
    a_layer = NULL;
    build_alarm();
    tick_timer(NULL);
}

static bool rb_pending;
static int rb_tile, rb_sheet;

static void rebuild_cb(void *arg)
{
    rb_pending = false;
    apply_palette();
    if (a_layer) lv_obj_delete(a_layer);
    a_layer = NULL;
    lv_obj_clean(lv_screen_active());
    build_all();
    if (rb_tile) lv_tileview_set_tile_by_index(tv, 1, 0, LV_ANIM_OFF);
    if (rb_sheet >= 0) open_sheet(rb_sheet);
    lv_obj_invalidate(lv_screen_active());
}

/* Switching the theme: let the object that was tapped finish its event, then rebuild everything. */
static void request_rebuild(void)
{
    if (rb_pending) return;
    rb_pending = true;
    rb_tile = apps_tile_active();
    rb_sheet = sheet_app;
    lv_async_call(rebuild_cb, NULL);
}

void ui_init(void)
{
    apply_palette();
    build_all();
    lv_timer_create(tick_timer, 100, NULL);
    lv_timer_create(spectrum_timer, 40, (void *)1);
}

// ── touch calibration ────────────────────────────────────────────────────────────

#ifdef ESP_PLATFORM
static lv_obj_t *cal_cross, *cal_msg;

static void cal_show(int x, int y, const char *msg)
{
    display_lock(-1);
    lv_obj_set_pos(cal_cross, x - 15, y - 15);
    lv_label_set_text(cal_msg, msg);
    display_unlock();
}

/* Wait for a press, average it, wait for release. false on timeout. */
static bool cal_point(int *rx, int *ry, int64_t deadline)
{
    int x, y, quiet = 0;
    while (quiet < 15) {                         // released for 150 ms first
        if (esp_timer_get_time() > deadline) return false;

        display_lock(-1);
        bool touched = touch_read_raw(&x, &y);
        display_unlock();

        quiet = touched ? 0 : quiet + 1;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    long sx = 0, sy = 0;
    int n = 0;
    while (n < 12) {
        if (esp_timer_get_time() > deadline) return false;

        display_lock(-1);
        bool touched = touch_read_raw(&x, &y);
        display_unlock();

        if (touched) { sx += x; sy += y; n++; }
        else if (n) n = 0, sx = sy = 0;          // lifted too soon: start again
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    *rx = sx / n;
    *ry = sy / n;
    return true;
}

void ui_calibrate(void)
{
    apply_palette();
    display_touch_enable(false);
    display_lock(-1);
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    cal_msg = label(scr, &font_vn18, C_TEXT, "");
    lv_obj_set_width(cal_msg, 260);
    lv_obj_set_style_text_align(cal_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(cal_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(cal_msg, LV_ALIGN_CENTER, 0, 10);
    cal_cross = label(scr, &font_vn24, C_ORANGE, I_PLUS);
    lv_obj_set_size(cal_cross, 30, 30);
    lv_obj_set_style_text_align(cal_cross, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *old = lv_screen_active();
    lv_screen_load(scr);
    display_unlock();
    display_backlight(g_cfg.bright);

    const int px[3] = { CAL_A, CAL_B, CAL_A }, py[3] = { CAL_A, CAL_A, CAL_C };
    int rx[3], ry[3];
    bool ok = true;
    int64_t deadline = esp_timer_get_time() + 40 * 1000000LL;
    for (int tries = 0; tries < 3; tries++) {
        ok = true;
        for (int i = 0; i < 3 && ok; i++) {
            char m[96];
            snprintf(m, sizeof m, "Hiệu chỉnh cảm ứng\nChạm vào dấu + (%d/3)", i + 1);
            cal_show(px[i], py[i], m);
            ok = cal_point(&rx[i], &ry[i], deadline);
        }
        if (!ok) break;
        // which raw axis moved when the finger moved left→right?
        bool swap = abs(ry[1] - ry[0]) > abs(rx[1] - rx[0]);
        int x0 = swap ? ry[0] : rx[0], x1 = swap ? ry[1] : rx[1];
        int y0 = swap ? rx[0] : ry[0], y1 = swap ? rx[2] : ry[2];
        if (abs(x1 - x0) > 600 && abs(y1 - y0) > 400) {
            g_cfg.cal_swap = swap;
            g_cfg.cal_x0 = x0; g_cfg.cal_x1 = x1; g_cfg.cal_y0 = y0; g_cfg.cal_y1 = y1;
            g_cfg.cal_ok = 1;
            cal_show(-100, -100, I_CHECK " Đã lưu hiệu chỉnh");
            vTaskDelay(pdMS_TO_TICKS(900));
            break;
        }
        ok = false;
        cal_show(-100, -100, "Chưa khớp, làm lại nhé");
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
    if (!ok) {
        cal_show(-100, -100, "Bỏ qua hiệu chỉnh\n(chạy lại trong Ứng dụng › Cảm ứng)");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    g_cfg.want_cal = 0;
    cfg_save();
    display_lock(-1);
    lv_screen_load(old);
    lv_obj_delete(scr);
    display_unlock();
    display_touch_enable(true);
}
#else
void ui_calibrate(void) {}
#endif
