#pragma once
#include "lvgl.h"

#define EMOJI_SIZE 96
/* The face for an emotion the xiaozhi server sent ("happy", "sad", "thinking", ...); neutral for anything else. */
const lv_image_dsc_t *emoji_face(const char *emotion);
