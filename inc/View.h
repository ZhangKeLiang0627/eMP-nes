#pragma once

#include "common_inc.h"
#include <string>
#include <functional>

namespace Page
{
    /* Full-app exit callback (signal / 退出 button). */
    using ExitCb = std::function<void(void)>;
    /* Virtual pad input callback: (button, pressed). Button indices follow
     * Model::NesButton == sn::Controller::Buttons (A=0..Right=7). */
    using KeyCb = std::function<void(int, bool)>;

    /**
     * @brief Presentation layer for eMP-nes.
     *        Owns the LVGL widgets: the NES game image (256x240 framebuffer
     *        rendered by the SimpleNES core, zoomed to the 480x480 panel)
     *        and the on-screen virtual pad. The View never touches the
     *        emulator core directly.
     */
    class View
    {
    public:
        void createGame(KeyCb keyCb);

        /* Call when the emulator finished a new frame (NesFb::frameSeq
         * changed). Must run in the LVGL thread. */
        void notifyFrame(void);

        void showError(const char * msg);

        void clearScreen(void);

    private:
        lv_obj_t * _screen = nullptr;
        lv_obj_t * _gameImg = nullptr;
    };
}
