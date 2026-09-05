#pragma once

#include "common_inc.h"
#include <string>
#include <functional>

namespace Page
{
    /* Full-app exit callback (退出 on the ROM menu). */
    using ExitCb = std::function<void(void)>;
    /* Game-page exit callback (退出 on the in-game top bar: back to menu). */
    using GameExitCb = std::function<void(void)>;
    /* ROM picked from the menu: (full path). */
    using RomSelectedCb = std::function<void(const std::string &)>;
    /* Virtual pad input callback: (button, pressed). Button indices follow
     * Model::NesButton == sn::Controller::Buttons (A=0..Right=7). */
    using KeyCb = std::function<void(int, bool)>;

    /**
     * @brief Presentation layer for eMP-nes (two pages, mirrors eMP-gba).
     *
     *  - ROM menu page: lv_list of *.nes + a PINNED top bar
     *    (截图 / 音量 / 退出). Gestures are disabled on this page.
     *  - Game page: the NES framebuffer image filling the 480x480 panel
     *    (2x integer zoom, overscan-cropped) + translucent virtual pad at
     *    the bottom. A HIDDEN top bar and a volume bar are revealed by
     *    gestures: swipe down/up toggles the top bar, swipe left/right
     *    toggles the volume bar (same animation style as eMP-gba).
     *
     * The whole page canvas has scrolling disabled (no pan/drag).
     * Screenshot (截图) snapshots the current LVGL screen to a PPM file.
     */
    class View
    {
    public:
        /* ROM menu page. */
        void showMenu(const std::string & romDir, RomSelectedCb romCb, ExitCb exitCb);

        /* Game page. keyCb feeds the virtual pad; gameExitCb fires when the
         * in-game top bar 退出 is pressed (async, safe for object teardown). */
        void showGame(KeyCb keyCb, GameExitCb gameExitCb);

        /* Call when the emulator finished a new frame (NesFb::frameSeq
         * changed). Must run in the LVGL thread. */
        void notifyFrame(void);

        void showError(const char * msg);

        /* Delete the current page (children + overlays). */
        void clearScreen(void);

    private:
        struct Overlay;

        lv_obj_t * _screen = nullptr;
        lv_obj_t * _gameImg = nullptr;
        Overlay * _ov = nullptr;
    };
}
