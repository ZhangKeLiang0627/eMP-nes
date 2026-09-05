#pragma once

#include "lvgl/lvgl.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

/* LVGL tick source for embedded Linux (gettimeofday based). */
extern "C" uint32_t custom_tick_get(void);

/* POSIX file system driver init (registers LVGL FS with letter '/'). */
extern "C" void lv_fs_posix_init(void);
