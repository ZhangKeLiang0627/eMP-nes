/*
 * sf::Graphics.hpp stand-in for the SimpleNES core.
 *
 * Real SFML types that the SimpleNES *core* (PPU/VirtualScreen headers)
 * references but that we must not link against:
 *   - sf::Color          : 32-bit RGBA color (PPU picture buffer)
 *   - sf::Drawable       : empty base the real VirtualScreen inherits from
 *   - sf::VertexArray / sf::Vector2u / sf::RenderTarget / sf::RenderStates :
 *     only used as VirtualScreen private members / declarations; the real
 *     VirtualScreen.cpp is NOT compiled - we re-implement its two public
 *     methods (create/setPixel) ourselves in src/nes_port/sf_shim_impl.cpp.
 *
 * VirtualScreen::setPixel is where the emulator hands us every rendered
 * pixel; our implementation writes directly into the RGB565 buffer that
 * LVGL displays, so the whole "screen" is a zero-copy pixel sink.
 */
#pragma once

#include <SFML/Config.hpp>
#include <cstddef>

namespace sf
{
    /* Complete stand-ins so by-value parameters in the (never linked)
     * VirtualScreen::draw signature are valid types. */
    class RenderStates {};
    class RenderTarget {};
    class Texture {};

    class Color
    {
    public:
        Uint8 r, g, b, a;

        Color() : r(0), g(0), b(0), a(255) {}
        Color(Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 255)
            : r(red), g(green), b(blue), a(alpha) {}
        /* 0xAARRGGBB */
        explicit Color(Uint32 color)
            : r(static_cast<Uint8>((color >> 16) & 0xFF)),
              g(static_cast<Uint8>((color >> 8) & 0xFF)),
              b(static_cast<Uint8>(color & 0xFF)),
              a(static_cast<Uint8>((color >> 24) & 0xFF)) {}

        static const Color Black;
        static const Color White;
        static const Color Magenta;
    };

    /* Non-pure draw: keeps sn::VirtualScreen instantiable (the real
     * VirtualScreen.cpp - and its draw() - is never compiled/linked). */
    class Drawable
    {
    public:
        virtual ~Drawable();
    protected:
        virtual void draw(RenderTarget & target, RenderStates states) const {}
    };

    /* Trivial stand-ins (real VirtualScreen.cpp is not compiled). */
    class Vector2u
    {
    public:
        Uint32 x, y;
        Vector2u() : x(0), y(0) {}
    };

    class VertexArray
    {
    public:
        VertexArray() {}
    };
}
