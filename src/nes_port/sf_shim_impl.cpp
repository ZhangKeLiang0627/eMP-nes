/*
 * Implementations of the sf shim pieces + the real sn::VirtualScreen
 * methods that PPU.cpp calls, without any SFML.
 *
 * PPU.cpp calls, at every PostRender (once per NES frame):
 *     m_screen.setPixel(x, y, color)          x: 0..255, y: 0..239
 * We convert straight to RGB565 into NesFb::buffer and bump frameSeq on
 * the last pixel so the LVGL thread knows a new frame is ready.
 */
#include "nes_fb.h"

#include <SFML/Config.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

/* ---- sf:: statics ---- */
namespace sf
{
    const Color Color::Black(0, 0, 0);
    const Color Color::White(255, 255, 255);
    const Color Color::Magenta(255, 0, 255);

    Drawable::~Drawable() {}
}

/* ---- framebuffer + key table storage ---- */
namespace NesFb
{
    uint16_t buffer[kWidth * kHeight];
    uint16_t fb2x[kOutW * kOutH];
    volatile uint32_t frameSeq = 0;
}
namespace NesKey { volatile bool state[8] = { false, false, false, false, false, false, false, false }; }

/* ---- sf::Keyboard (backs the NES buttons the core polls) ---- */
bool sf::Keyboard::isKeyPressed(Key key)
{
    const int idx = static_cast<int>(key);
    return NesKey::get(idx);
}

/* ---- sn::VirtualScreen (real header, our pixel sink) ---- */
#include "VirtualScreen.h"

void sn::VirtualScreen::create(unsigned int width, unsigned int height,
                               float /* pixel_size */, sf::Color /* color */)
{
    (void)width;
    (void)height;
}

void sn::VirtualScreen::setPixel(std::size_t x, std::size_t y, sf::Color color)
{
    if (x < NesFb::kWidth && y < NesFb::kHeight) {
        const uint16_t c = NesFb::rgb565(color.r, color.g, color.b);
        NesFb::buffer[y * NesFb::kWidth + x] = c;

        /* 2x integer pre-scale for the display framebuffer. */
        const int x2 = (int)x * NesFb::kScale;
        const int y2 = (int)y * NesFb::kScale;
        const int row0 = y2 * NesFb::kOutW;
        const int row1 = (y2 + 1) * NesFb::kOutW;
        NesFb::fb2x[row0 + x2]     = c;
        NesFb::fb2x[row0 + x2 + 1] = c;
        NesFb::fb2x[row1 + x2]     = c;
        NesFb::fb2x[row1 + x2 + 1] = c;

        if (x == (std::size_t)NesFb::kWidth - 1 && y == (std::size_t)NesFb::kHeight - 1) {
            NesFb::frameSeq++; /* full frame done */
        }
    }
}

/* Vtable anchor (declared private in the real header; never called). */
void sn::VirtualScreen::draw(sf::RenderTarget &, sf::RenderStates) const {}
