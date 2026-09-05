#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>

#include "View.h"
#include "common_inc.h"

namespace Nes
{
    class Engine;
}

namespace Page
{
    /**
     * @brief Application model for eMP-nes (mirrors eMP-gba).
     *
     * Lifecycle: ROM menu (or argv / EMP_NES_AUTOSTART direct launch) ->
     * game -> back to menu -> ... The menu shows every *.nes under the ROM
     * directory; the NES emulation itself runs in a separate thread owned
     * by Nes::Engine so the CPU interpreter never stalls the UI. This class
     * owns the LVGL thread that pumps lv_timer_handler and polls for the
     * "hold SELECT to return to the menu" gesture.
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
        void launch(const std::string & romPath);
        void backToMenu(void);
        void stopGame(void);
        void onKey(int button, bool down);
        static std::string resolveRomDir(void);

        std::thread _threadLvgl;             /* LVGL timer thread */
        std::atomic<bool> _exitFlag{false};
        std::function<void(void)> _exitCb;

        std::string _romDir;
        int _volume = 100;                   /* kept for a future ALSA port */
        bool _inGame = false;

        uint32_t _selectTick = 0;            /* SELECT long-press tracker */

        std::unique_ptr<Nes::Engine> _engine;   /* NES emulation (own thread) */
        View _view;
    };
}
