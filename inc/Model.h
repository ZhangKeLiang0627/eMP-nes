#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

#include "View.h"
#include "common_inc.h"

namespace Page
{
    /**
     * @brief Application model for eMP-nes.
     *        Owns the NES lifecycle (select ROM -> emulate -> exit), the ROM
     *        directory configuration and the LVGL thread that pumps
     *        lv_timer_handler. The NES emulation itself runs in a separate
     *        thread owned by Nes::Engine (see src/nes_port/nes_engine.h) so
     *        the CPU interpreter never stalls the UI.
     */
    class Model
    {
    public:
        Model(std::function<void(void)> exitCb, const std::string & romArg);
        ~Model();

        /* NES 8 buttons, order matches sn::Controller::Buttons (A,B,Select,
         * Start,Up,Down,Left,Right) and the sf-shim key table. */
        enum NesButton { BTN_A = 0, BTN_B, BTN_SELECT, BTN_START, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT };

    private:
        void threadLvglHandler(void);
        void onKey(int button, bool down);

        std::thread _threadLvgl;             /* LVGL timer thread */
        std::atomic<bool> _exitFlag{false};
        std::function<void(void)> _exitCb;

        std::string _romPath;
        bool _romLoaded = false;

        View _view;
    };
}
