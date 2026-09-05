/*
 * sf::Keyboard stand-in for the SimpleNES core.
 *
 * SimpleNES polls the NES buttons through sf::Keyboard::isKeyPressed(Key)
 * (see src/Controller.cpp). Instead of a real keyboard we expose the NES
 * button indices 0..7 as "keys" and back them with a global table that the
 * LVGL virtual pad writes into (see src/nes_port/nes_fb.h).
 */
#pragma once

#include <SFML/Config.hpp>

namespace sf
{
    class Keyboard
    {
    public:
        /* Our virtual keys: index == NES button index
         * (A,B,Select,Start,Up,Down,Left,Right), i.e. sn::Controller::Buttons. */
        enum Key
        {
            A = 0,
            B = 1,
            Select = 2,
            Start = 3,
            Up = 4,
            Down = 5,
            Left = 6,
            Right = 7,
            KeyCount = 8,
            Unknown = -1
        };

        static bool isKeyPressed(Key key);
    };
}
