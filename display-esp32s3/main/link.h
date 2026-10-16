#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LINK_BANDS 32
#define LINK_MAX_FOUND 8

typedef struct {
    char id[48], name[40], ip[16], ver[16];
    bool playing;
} speaker_t;

typedef enum { LINK_OFFLINE, LINK_SEARCHING, LINK_CHOOSE, LINK_CONNECTED, LINK_LOST } link_phase_t;

typedef struct {
    uint32_t seq;          // bumped on every new state
    link_phase_t phase;
    char name[40], ip[16], ver[16];
    char vs[12], emo[16];
    bool awake, mute;
    int vol;
    char user[900], bot[900];
    bool has_m;
    char ms[14], ti[400], ar[260], src[12];
    int pos, dur;
    bool has_wx, wx_day;
    int wx_t, wx_c;
    char wx_d[64], wx_city[80];
    char al_next[80], al_ring[120];
    int group;
    int64_t got_us;        // esp_timer time of this state
} spk_state_t;

void link_init(void);
/* Copy the latest state when it changed since *seq (true) — cheap to call often. */
bool link_state(spk_state_t *out, uint32_t *seq);
/* The latest spectrum, 0..255 per band; false when nothing is playing. */
bool link_spectrum(uint8_t out[LINK_BANDS]);
/* Speakers found by the last search. */
int link_found(speaker_t *out, int max);
void link_choose(const char *id);     // follow this speaker (saved)
void link_research(void);             // forget the choice and search again
void link_cmd(const char *c);         // wake, sleep, pause, resume, next, stop, vol+, vol-, alarm_stop
