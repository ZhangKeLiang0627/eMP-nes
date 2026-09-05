/*
 * eMP-nes entry point.
 *
 * Architecture mirrors eMP-gba / eMP-tokenMonitor:
 *   HAL::Init()  -> LVGL + fbdev (/dev/fb0) + multi-touch evdev input
 *   Page::Model  -> NES lifecycle + LVGL thread (+ Nes::Engine thread)
 *   Page::View   -> game framebuffer image + virtual pad
 *
 * Usage:
 *   ./eMP_nes [/path/to/game.nes]
 * ROM fallback order: argv[1] > EMP_NES_AUTOSTART > first /mnt/UDISK/*.nes (top dir)
 */
#include "common_inc.h"
#include "HAL.h"
#include "Model.h"

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char * argv[])
{
    fprintf(stderr, "[main] eMP-nes starting ...\n");
    fflush(stderr);

    std::string romArg;
    if (argc > 1)
        romArg = argv[1];

#if LV_USE_LOG
    lv_log_register_print_cb([](lv_log_level_t level, const char * buf) {
        LV_UNUSED(level);
        fprintf(stderr, "%s", buf);
        fflush(stderr);
    });
#endif

    /* Hardware init: LVGL + POSIX FS + fbdev + multi-touch evdev */
    HAL::Init();
    fprintf(stderr, "[main] HAL::Init done\n");

    /* Build the UI + start the LVGL thread + the NES emulation thread */
    Page::Model model([]() { exit(0); }, romArg);
    fprintf(stderr, "[main] Model created\n");

    /* Main thread: idle. Everything else runs in its own thread. */
    while (1) {
        usleep(10 * 1000 * 1000);
    }

    return 0;
}
