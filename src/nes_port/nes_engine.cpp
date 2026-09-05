/*
 * Nes::Engine implementation.
 *
 * Object wiring replicates upstream SimpleNES src/Emulator.cpp exactly
 * (constructor member order included) so the core behaves identically on
 * the T113 board; only the SFML window / audio device are gone.
 */
#include "nes_engine.h"
#include "nes_fb.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>

/* SimpleNES core headers (submodule, unmodified). */
#include "APU/Constants.h"
#include "APU/APU.h"
#include "CPU.h"
#include "Cartridge.h"
#include "Controller.h"
#include "Log.h"
#include "MainBus.h"
#include "Mapper.h"
#include "PPU.h"
#include "PictureBus.h"

namespace Nes
{

struct Engine::Impl
{
    /* --- core objects (declaration order == construction order) --- */
    sn::CPU             m_cpu;
    sn::AudioPlayer     m_audioPlayer;
    sn::PictureBus      m_pictureBus;
    sn::VirtualScreen   m_screen;
    sn::PPU             m_ppu;
    sn::APU             m_apu;
    sn::Cartridge       m_cartridge;
    std::unique_ptr<sn::Mapper> m_mapper;
    sn::Controller      m_controller1;
    sn::Controller      m_controller2;
    sn::MainBus         m_bus;

    std::thread         m_thread;
    std::atomic<bool>   m_stop{true};
    bool                m_loaded = false;

    Impl()
        : m_cpu(m_bus)
        , m_audioPlayer(static_cast<int>(1.0 / sn::apu_clock_period_s.count()))
        , m_ppu(m_pictureBus, m_screen)
        , m_apu(m_audioPlayer, m_cpu.createIRQHandler(),
                [this](sn::Address addr) { return dmcDMA(addr); })
        , m_bus(m_ppu, m_apu, m_controller1, m_controller2,
                [this](sn::Byte page) { oamDMA(page); })
    {
        m_ppu.setInterruptCallback([this]() { m_cpu.nmiInterrupt(); });
    }

    void oamDMA(sn::Byte page)
    {
        m_cpu.skipOAMDMACycles();
        auto * page_ptr = m_bus.getPagePtr(page);
        if (page_ptr != nullptr) {
            m_ppu.doDMA(page_ptr);
        }
        else {
            LOG(sn::Error) << "Can't get pageptr for DMA" << std::endl;
        }
    }

    sn::Byte dmcDMA(sn::Address addr)
    {
        m_cpu.skipDMCDMACycles();
        return m_bus.read(addr);
    }

    bool loadRom(const std::string & path)
    {
        if (!m_cartridge.loadFromFile(path)) {
            fprintf(stderr, "[Nes] loadFromFile failed: %s\n", path.c_str());
            return false;
        }

        m_mapper = sn::Mapper::createMapper(
            static_cast<sn::Mapper::Type>(m_cartridge.getMapper()),
            m_cartridge,
            m_cpu.createIRQHandler(),
            [this]() { m_pictureBus.updateMirroring(); });

        if (!m_mapper) {
            fprintf(stderr, "[Nes] unsupported mapper for %s\n", path.c_str());
            return false;
        }
        if (!m_bus.setMapper(m_mapper.get()) ||
            !m_pictureBus.setMapper(m_mapper.get())) {
            fprintf(stderr, "[Nes] setMapper failed\n");
            return false;
        }

        m_cpu.reset();
        m_ppu.reset();
        m_loaded = true;

        /* NES buttons 0..7 -> sf-shim keys 0..7. */
        std::vector<sf::Keyboard::Key> keys;
        for (int i = 0; i < 8; i++)
            keys.push_back(static_cast<sf::Keyboard::Key>(i));
        m_controller1.setKeyBindings(keys);
        m_controller2.setKeyBindings(keys);

        fprintf(stderr, "[Nes] ROM loaded: %s (mapper %d)\n", path.c_str(),
                (int)m_cartridge.getMapper());
        return true;
    }

    /* One NES CPU clock (PPU x3 + CPU x1 + APU x1). */
    void runStep()
    {
        m_ppu.step();
        m_ppu.step();
        m_ppu.step();
        m_cpu.step();
        m_apu.step();
    }

    void loop()
    {
        using clock = std::chrono::high_resolution_clock;
        auto last   = clock::now();
        clock::duration elapsed(0);

        fprintf(stderr, "[Nes] emulation thread started\n");

        while (!m_stop.load()) {
            const auto now = clock::now();
            elapsed += now - last;
            last = now;

            bool stepped = false;
            while (elapsed > sn::cpu_clock_period_ns) {
                runStep();
                elapsed -= sn::cpu_clock_period_ns;
                stepped = true;
            }
            if (!stepped)
                std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        fprintf(stderr, "[Nes] emulation thread exit\n");
    }
};

/* ------------------------------ public ------------------------------ */

Engine::Engine() : _impl(new Impl()) {}

Engine::~Engine()
{
    stop();
}

bool Engine::loadRom(const std::string & path)
{
    return _impl->loadRom(path);
}

void Engine::start()
{
    if (!_impl->m_loaded || !_impl->m_stop.exchange(false))
        return; /* already running / nothing loaded */
    _impl->m_thread = std::thread([this]() { _impl->loop(); });
}

void Engine::stop()
{
    if (!_impl->m_stop.exchange(true)) {
        if (_impl->m_thread.joinable())
            _impl->m_thread.join();
    }
}

void Engine::setButton(Button b, bool down)
{
    NesKey::set(static_cast<int>(b), down);
}

} /* namespace Nes */
