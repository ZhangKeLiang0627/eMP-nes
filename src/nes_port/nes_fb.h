/*
 * Shared cross-module state for eMP-nes:
 *   - the NES framebuffer (written by the PPU pixel sink on the emulation
 *     thread, drawn by LVGL on the LVGL thread)
 *   - the virtual key table (written by the LVGL pad, polled by the core
 *     through the sf::Keyboard shim)
 *
 * The framebuffer is a plain memory window (no locks): the emulator only
 * writes it inside PPU::PostRender (once per NES frame, ~60 Hz) and LVGL
 * only re-reads it after seeing frameSeq change. A torn frame is
 * cosmetically identical to an old frame at 60 fps, so a lock is not worth
 * the contention.
 */
#pragma once

#include <cstdint>

namespace NesFb
{
    constexpr int kWidth  = 256;
    constexpr int kHeight = 240;

    /* RGB565, row-major (written by src/nes_port/sf_shim_impl.cpp). */
    extern uint16_t buffer[kWidth * kHeight];

    /* Incremented once per completed NES frame (at pixel 255,239). */
    extern volatile uint32_t frameSeq;

    /* Convert an RGB565 pixel pair for the PPU 32-bit color. */
    inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
    {
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

namespace NesKey
{
    /* Index == sn::Controller::Buttons (A=0,B=1,Select=2,Start=3,
     * Up=4,Down=5,Left=6,Right=7). */
    extern volatile bool state[8];

    inline void set(int button, bool down)
    {
        if (button >= 0 && button < 8)
            state[button] = down;
    }

    inline bool get(int button)
    {
        return (button >= 0 && button < 8) ? state[button] : false;
    }
}
