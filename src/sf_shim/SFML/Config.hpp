/*
 * Minimal SFML type shims for embedding the SimpleNES core WITHOUT SFML.
 *
 * SimpleNES core headers (Controller.h / PPU.h / PaletteColors.h /
 * VirtualScreen.h) and the APU units include <SFML/...> headers but only
 * ever use a handful of scalar types (sf::Uint32 etc). This directory
 * provides compile-time stand-ins so the core builds on the T113 cross
 * toolchain with no SFML dependency. Nothing here is part of real SFML.
 */
#pragma once

#include <cstdint>

namespace sf
{
    typedef std::int8_t   Int8;
    typedef std::int16_t  Int16;
    typedef std::int32_t  Int32;
    typedef std::int64_t  Int64;

    typedef std::uint8_t  Uint8;
    typedef std::uint16_t Uint16;
    typedef std::uint32_t Uint32;
    typedef std::uint64_t Uint64;
}
