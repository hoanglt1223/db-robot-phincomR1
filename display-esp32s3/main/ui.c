/*
 * What the display shows. One screen, 320x240: a top bar (speaker, volume, clock) and one of
 * these pages, chosen from the speaker's state:
 *   status  – no Wi-Fi yet / looking for the speaker / connection lost
 *   choose  – several speakers on the network, tap one
 *   voice   – the assistant is listening or talking: a face, and what was said
 *   music   – a song, a radio station or a podcast: title, spectrum, progress, buttons
 *   idle    – a big clock, the date, the weather, the next alarm
 * and on top of everything the alarm, while it rings.
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
#define NOW_US() esp_timer_get_time()
#else
int64_t sim_now_us(void);
#define NOW_US() sim_now_us()
#endif

LV_FONT_DECLARE(font_vn14);
LV_FONT_DECLARE(font_vn18);
LV_FONT_DECLARE(font_vn24);
LV_FONT_DECLARE(font_clock72);
LV_FONT_DECLARE(font_icon40);

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
#define I_COG     ""
#define I_SUN     ""
#define I_MOON    ""
#define I_CLOUD   ""
#define I_RAIN    ""
#define I_BOLT    ""
#define I_SNOW    ""
#define I_SMOG    ""
#define I_GLOBE   ""
#define I_PIN     ""
#define I_SYNC    ""
#define I_USERS   ""
#define I_MINUS   ""
#define I_PLUS    ""
#define I_CHECK   ""

#define C_BG     0x0B1220
#define C_CARD   0x1E293B
#define C_LINE   0x334155
#define C_TEXT   0xE2E8F0
#define C_MUTED  0x94A3B8
#define C_ACCENT 0x38BDF8
#define C_GREEN  0x4ADE80
#define C_ORANGE 0xF59E0B
#define C_RED    0xF43F5E
#define C_PINK   0xF472B6

#define TOP_H 28
#define PAGE_H (LCD_V_RES - TOP_H)

typedef enum { P_STATUS, P_CHOOSE, P_VOICE, P_MUSIC, P_IDLE, P_COUNT } page_t;

static spk_state_t st;           // the copy the UI works from
static uint32_t st_seq = 0xFFFFFFFF;
static page_t cur_page = P_COUNT;
static int64_t voice_until;      // keep the voice page a little after the talk ends

static lv_obj_t *pages[P_COUNT];
static lv_obj_t *top_dot, *top_name, *top_right;
/* status */
static lv_obj_t *s_icon, *s_title, *s_text;
/* choose */
static lv_obj_t *c_list;
static char c_ids[LINK_MAX_FOUND][48];
static int c_count = -1;
static uint32_t c_hash;
/* voice */
static lv_obj_t *v_face, *v_eye[2], *v_mouth_arc, *v_mouth_o, *v_state, *v_scroll, *v_user, *v_bot;
static int v_anim;
/* music */
static lv_obj_t *m_title, *m_artist, *m_bars[LINK_BANDS], *m_bar, *m_pos, *m_dur, *m_play;
static uint8_t m_level[LINK_BANDS];
/* idle */
static lv_obj_t *i_clock, *i_date, *i_wicon, *i_temp, *i_wdesc, *i_alarm, *i_ip;
/* alarm */
static lv_obj_t *a_layer, *a_text;

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

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                        const lv_font_t *f, uint32_t bg, const char *cmd)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = label(b, f, C_TEXT, text);
    lv_obj_center(l);
    if (cmd) lv_obj_add_event_cb(b, cmd_cb, LV_EVENT_CLICKED, (void *)cmd);
    return b;
}

static void set_text(lv_obj_t *l, const char *t)
{
    const char *have = lv_label_get_text(l);
    if (!have || strcmp(have, t)) lv_label_set_text(l, t);
}

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
    if (st.al_ring[0] || cur_page == P_VOICE) b = g_cfg.bright;   // ringing / talking: full
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
}

static void build_top(lv_obj_t *scr)
{
    lv_obj_t *bar = box(scr, 0, 0, LCD_H_RES, TOP_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0F1A2E), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, top_long_press, LV_EVENT_LONG_PRESSED, NULL);
    top_dot = box(bar, 8, 10, 8, 8);
    lv_obj_set_style_radius(top_dot, 4, 0);
    lv_obj_set_style_bg_opa(top_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_dot, lv_color_hex(C_MUTED), 0);
    top_name = label(bar, &font_vn14, C_TEXT, "DB-Robot");
    lv_obj_set_pos(top_name, 22, 5);
    lv_obj_set_width(top_name, 150);
    lv_label_set_long_mode(top_name, LV_LABEL_LONG_DOT);
    top_right = label(bar, &font_vn14, C_TEXT, "");
    lv_obj_align(top_right, LV_ALIGN_RIGHT_MID, -8, 0);
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
        lv_obj_set_style_bg_color(top_dot, lv_color_hex(C_GREEN), 0);
    } else {
        snprintf(right, sizeof right, "%s", clk);
        set_text(top_name, "DB-Robot");
        lv_obj_set_style_bg_color(top_dot, lv_color_hex(st.phase == LINK_LOST ? C_ORANGE : C_MUTED), 0);
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
    lv_obj_set_pos(c_list, 8, 36);
    lv_obj_set_size(c_list, LCD_H_RES - 16, PAGE_H - 44);
    lv_obj_set_style_bg_opa(c_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c_list, 0, 0);
    lv_obj_set_style_pad_all(c_list, 0, 0);
    lv_obj_set_style_pad_row(c_list, 6, 0);
    lv_obj_set_flex_flow(c_list, LV_FLEX_FLOW_COLUMN);
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
        lv_obj_t *b = lv_button_create(c_list);
        lv_obj_set_size(b, lv_pct(100), 48);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 10, 0);
        char t[96];
        snprintf(t, sizeof t, "%s", f[i].name[0] ? f[i].name : "Loa DB-Robot");
        lv_obj_t *l = label(b, &font_vn18, C_TEXT, t);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, -7);
        snprintf(t, sizeof t, "%s · %s%s", f[i].ip, f[i].ver, f[i].playing ? " · " I_MUSIC : "");
        l = label(b, &font_vn14, C_MUTED, t);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 12);
        lv_obj_add_event_cb(b, choose_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// ── voice page ───────────────────────────────────────────────────────────────────

static void build_voice(lv_obj_t *p)
{
    v_face = box(p, 8, 14, 116, 116);
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
    lv_obj_set_pos(v_state, 0, 140);
    lv_obj_t *call = button(p, 16, 166, 100, 36, I_MIC "  Gọi", &font_vn14, C_CARD, "wake");
    (void)call;

    v_scroll = lv_obj_create(p);
    lv_obj_set_pos(v_scroll, 134, 4);
    lv_obj_set_size(v_scroll, LCD_H_RES - 140, PAGE_H - 8);
    lv_obj_set_style_bg_color(v_scroll, lv_color_hex(0x111B2E), 0);
    lv_obj_set_style_border_width(v_scroll, 0, 0);
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
    else if (!strcmp(e, "sad") || !strcmp(e, "crying")) { eye = 22; mouth = -1; col = 0x93C5FD; }
    else if (!strcmp(e, "angry")) { eye = 16; mouth = -1; col = C_RED; }
    else if (!strcmp(e, "surprised") || !strcmp(e, "shocked")) { eye = 36; mouth = 99; }
    else if (!strcmp(e, "thinking") || !strcmp(e, "confused")) { eye = 24; lift = 6; mouth = 0; }
    else if (!strcmp(e, "sleepy") || !strcmp(e, "relaxed")) { eye = 6; mouth = 0; }
    else if (!strcmp(e, "cool") || !strcmp(e, "confident") || !strcmp(e, "winking")) { eye = 12; mouth = 1; }
    else if (!strcmp(e, "embarrassed")) { eye = 20; mouth = 0; col = C_PINK; }
    if (listening) { col = C_GREEN; mouth = mouth == 99 ? 99 : 0; eye = eye < 24 ? 26 : eye; }
    v_anim++;
    if (speaking && mouth != 99) open = (int[]){ 2, 8, 14, 6, 12, 4, 10, 16 }[v_anim % 8];
    // blink now and then
    if (!speaking && (v_anim % 40) == 0) eye = 4;
    face(eye, lift, mouth, col, open);
    set_text(v_state, listening ? "Đang nghe…" : speaking ? "Đang nói…" : st.awake ? "Đang nghĩ…" : "");
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
        m_bars[i] = box(p, x0 + i * (bw + gap), 138, bw, 2);
        lv_obj_set_style_radius(m_bars[i], 2, 0);
        lv_obj_set_style_bg_opa(m_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(m_bars[i], lv_color_hsv_to_rgb(195 + i * 4, 75, 98), 0);
    }
    m_pos = label(p, &font_vn14, C_MUTED, "0:00");
    lv_obj_set_pos(m_pos, 10, 142);
    m_dur = label(p, &font_vn14, C_MUTED, "");
    lv_obj_align(m_dur, LV_ALIGN_TOP_RIGHT, -10, 142);
    m_bar = lv_bar_create(p);
    lv_obj_set_pos(m_bar, 58, 150);
    lv_obj_set_size(m_bar, LCD_H_RES - 116, 5);
    lv_obj_set_style_bg_color(m_bar, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_bar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_bar_set_range(m_bar, 0, 1000);
    const int y = 168, w = 56, g = 6, x = (LCD_H_RES - 5 * w - 4 * g) / 2;
    button(p, x, y, w, 38, I_VOLDN, &font_vn18, C_CARD, "vol-");
    m_play = button(p, x + (w + g), y, w, 38, I_PAUSE, &font_vn18, 0x0369A1, NULL);
    lv_obj_add_event_cb(m_play, play_cb, LV_EVENT_CLICKED, NULL);
    button(p, x + 2 * (w + g), y, w, 38, I_NEXT, &font_vn18, C_CARD, "next");
    button(p, x + 3 * (w + g), y, w, 38, I_STOP, &font_vn18, C_CARD, "stop");
    button(p, x + 4 * (w + g), y, w, 38, I_VOLUP, &font_vn18, C_CARD, "vol+");
}

static void update_music(void)
{
    set_text(m_title, st.ti[0] ? st.ti : "…");
    char a[340];
    const char *state = !strcmp(st.ms, "downloading") ? "Đang tải… · " : !strcmp(st.ms, "paused") ? "Tạm dừng · " : "";
    snprintf(a, sizeof a, I_MUSIC " %s%s%s%s", state, st.src, st.ar[0] ? " · " : "", st.ar);
    set_text(m_artist, a);
    lv_obj_t *pl = lv_obj_get_child(m_play, 0);
    set_text(pl, !strcmp(st.ms, "playing") ? I_PAUSE : I_PLAY);
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
    if (cur_page != P_MUSIC) return;
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
        int h = 2 + lv * 84 / 255;
        lv_obj_set_height(m_bars[i], h);
        lv_obj_set_y(m_bars[i], 138 - h);
    }
}

// ── idle page ────────────────────────────────────────────────────────────────────

static void build_idle(lv_obj_t *p)
{
    i_clock = label(p, &font_clock72, C_TEXT, "--:--");
    lv_obj_align(i_clock, LV_ALIGN_TOP_MID, 0, 0);
    i_date = label(p, &font_vn18, C_MUTED, "");
    lv_obj_align(i_date, LV_ALIGN_TOP_MID, 0, 74);

    lv_obj_t *w = box(p, 0, 100, LCD_H_RES, 50);
    lv_obj_set_flex_flow(w, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(w, 10, 0);
    i_wicon = label(w, &font_icon40, C_ORANGE, "");
    i_temp = label(w, &font_vn24, C_TEXT, "");
    lv_obj_t *col = box(w, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    i_wdesc = label(col, &font_vn14, C_TEXT, "");
    i_ip = label(col, &font_vn14, C_MUTED, "");   // reused below for the city
    (void)i_ip;

    i_alarm = label(p, &font_vn14, C_TEXT, "");
    lv_obj_set_pos(i_alarm, 12, 164);
    lv_obj_set_width(i_alarm, 180);
    lv_label_set_long_mode(i_alarm, LV_LABEL_LONG_WRAP);
    button(p, LCD_H_RES - 122, 164, 110, 40, I_MIC "  Gọi loa", &font_vn14, 0x0369A1, "wake");
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
        lv_obj_set_style_text_color(i_wicon, lv_color_hex(st.wx_c <= 1 ? (st.wx_day ? C_ORANGE : 0xFDE68A) : 0xCBD5E1), 0);
        snprintf(t, sizeof t, "%d°C", st.wx_t);
        set_text(i_temp, t);
        set_text(i_wdesc, st.wx_d);
        set_text(i_ip, st.wx_city);
    } else {
        set_text(i_wicon, "");
        set_text(i_temp, "");
        set_text(i_wdesc, "Chưa có thời tiết");
        set_text(i_ip, "(đặt vị trí trong trang loa)");
    }
    if (st.al_next[0]) snprintf(t, sizeof t, I_BELL "  %s\n%s", st.al_next, st.ip);
    else snprintf(t, sizeof t, I_GLOBE "  %s\nDB-Robot %s", st.ip, st.ver);
    set_text(i_alarm, t);
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
    lv_obj_t *h = label(a_layer, &font_vn24, C_TEXT, "Báo thức");
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 68);
    a_text = label(a_layer, &font_vn18, 0xFED7AA, "");
    lv_obj_set_width(a_text, 300);
    lv_label_set_long_mode(a_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(a_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a_text, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_t *b = button(a_layer, 60, 170, 200, 52, "TẮT BÁO THỨC", &font_vn18, 0xEA580C, "alarm_stop");
    lv_obj_set_style_radius(b, 26, 0);
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

// ── page choice and the timers ───────────────────────────────────────────────────

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
    if (talking()) {
        voice_until = now + 6 * 1000000LL;
        return P_VOICE;
    }
    if (now < voice_until && !music_active()) return P_VOICE;
    if (music_active()) return P_MUSIC;
    return P_IDLE;
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
    if ((n++ % 10) == 0) backlight_update();
}

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    build_top(scr);
    for (int i = 0; i < P_COUNT; i++) {
        pages[i] = box(scr, 0, TOP_H, LCD_H_RES, PAGE_H);
        lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    build_status(pages[P_STATUS]);
    build_choose(pages[P_CHOOSE]);
    build_voice(pages[P_VOICE]);
    build_music(pages[P_MUSIC]);
    build_idle(pages[P_IDLE]);
    build_alarm();
    tick_timer(NULL);
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
        quiet = touch_read_raw(&x, &y) ? 0 : quiet + 1;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    long sx = 0, sy = 0;
    int n = 0;
    while (n < 12) {
        if (esp_timer_get_time() > deadline) return false;
        if (touch_read_raw(&x, &y)) { sx += x; sy += y; n++; }
        else if (n) n = 0, sx = sy = 0;          // lifted too soon: start again
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    *rx = sx / n;
    *ry = sy / n;
    return true;
}

void ui_calibrate(void)
{
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
        cal_show(-100, -100, "Bỏ qua hiệu chỉnh\n(chạy lại từ trang cài đặt)");
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
