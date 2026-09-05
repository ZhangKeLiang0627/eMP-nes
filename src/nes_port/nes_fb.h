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

    /* Output scale: 2x integer zoom -> 512x480, centered on the 480x480
     * panel by cropping 16px of NES overscan each side (pixels map 1:4,
     * no resampling). */
    constexpr int kScale  = 2;
    constexpr int kOutW   = kWidth * kScale;    /* 512 */
    constexpr int kOutH   = kHeight * kScale;   /* 480 */
    constexpr int kCropX  = (kOutW - 480) / 2;  /* 16 px per side (8 NES px) */

    /* RGB565, row-major, PPU-native 256x240 (written by the pixel sink). */
    extern uint16_t buffer[kWidth * kHeight];

    /* RGB565 2x pre-scaled framebuffer fed straight to the LVGL image. */
    extern uint16_t fb2x[kOutW * kOutH];

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
