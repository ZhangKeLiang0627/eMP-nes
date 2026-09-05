/*
 * Nes::Engine - thin embed wrapper around the SimpleNES core for eMP-nes.
 *
 * The SimpleNES core (libs/simplenes) is driven exactly like its upstream
 * sf::RenderWindow loop, minus the window: one NES CPU clock = PPU x3 +
 * CPU x1 + APU x1, paced against the wall clock (cpu_clock_period_ns =
 * 559 ns) so the game runs at real time. Rendered pixels arrive through
 * sn::VirtualScreen::setPixel (implemented in sf_shim_impl.cpp) which
 * writes NesFb::buffer; the LVGL thread only watches NesFb::frameSeq.
 *
 * Everything the core needs from "SFML" (sf::Color, sf::Keyboard,
 * sn::AudioPlayer, sn::VirtualScreen) is provided by src/sf_shim, so the
 * submodule itself is compiled unmodified.
 */
#pragma once

#include <memory>
#include <string>

namespace Nes
{
    class Engine
    {
    public:
        /* NES button indices (== sn::Controller::Buttons == sf-shim key). */
        enum Button
        {
            BTN_A = 0,
            BTN_B,
            BTN_SELECT,
            BTN_START,
            BTN_UP,
            BTN_DOWN,
            BTN_LEFT,
            BTN_RIGHT
        };

        Engine();
        ~Engine();

        /* Load a .nes ROM and wire cartridge/mapper/bus. Returns false and
         * prints the reason to stderr on failure (unsupported mapper etc). */
        bool loadRom(const std::string & path);

        /* Start the emulation thread (60 fps pacing). */
        void start();

        /* Stop the emulation thread and join it. */
        void stop();

        void setButton(Button b, bool down);

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
