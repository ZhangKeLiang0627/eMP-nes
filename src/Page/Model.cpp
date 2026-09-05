/*
 * Model for eMP-nes: NES lifecycle + LVGL thread.
 *
 * ROM selection order: command line argument > env EMP_NES_AUTOSTART >
 * first *.nes under /mnt/UDISK (the media-player volume convention).
 */
#include "Model.h"
#include "nes_engine.h"
#include "nes_fb.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <dirent.h>

using namespace Page;

namespace
{
std::string firstNesInDir(const std::string & dir)
{
    DIR * d = opendir(dir.c_str());
    if (!d)
        return std::string();
    std::string found;
    struct dirent * ent;
    while ((ent = readdir(d)) != nullptr) {
        const std::string name = ent->d_name;
        if (name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".nes") == 0) {
            found = dir + "/" + name;
            break;
        }
    }
    closedir(d);
    return found;
}
}

Model::Model(std::function<void(void)> exitCb, const std::string & romArg)
{
    _exitFlag = false;
    _exitCb = exitCb;

    /* Determine the ROM to launch. */
    if (!romArg.empty())
        _romPath = romArg;
    else {
        const char * autoRom = getenv("EMP_NES_AUTOSTART");
        if (autoRom != nullptr && autoRom[0] != '\0')
            _romPath = autoRom;
        else
            _romPath = firstNesInDir("/mnt/UDISK");
    }

    LV_LOG_USER("[Model] NES ROM = %s", _romPath.empty() ? "(none)" : _romPath.c_str());

    /* Build the UI (single-threaded still: no LVGL thread yet). */
    _view.createGame([this](int b, bool down) { onKey(b, down); });

    if (!_romPath.empty()) {
        _engine.reset(new Nes::Engine());
        if (_engine->loadRom(_romPath)) {
            _romLoaded = true;
            _engine->start();
        }
        else {
            _romLoaded = false;
            _engine.reset();
            _view.showError("ROM load failed");
            LV_LOG_WARN("[Model] load ROM failed: %s", _romPath.c_str());
        }
    }
    else {
        _view.showError("no .nes found (argv / EMP_NES_AUTOSTART / /mnt/UDISK)");
    }

    /* Spawn the LVGL thread that drives lv_timer_handler + frame pump. */
    _threadLvgl = std::thread([](Model * p) { p->threadLvglHandler(); }, this);
    _threadLvgl.detach();
}

Model::~Model()
{
    _exitFlag = true;
    if (_engine)
        _engine->stop();
    if (_threadLvgl.joinable())
        _threadLvgl.join();
}

void Model::onKey(int button, bool down)
{
    if (_engine)
        _engine->setButton((Nes::Engine::Button)button, down);
}

void Model::threadLvglHandler(void)
{
    uint32_t lastSeq = 0;

    while (!_exitFlag) {
        uint32_t wait = lv_timer_handler();

        /* Push a freshly rendered NES frame to the image widget. */
        if (_romLoaded) {
            const uint32_t seq = NesFb::frameSeq;
            if (seq != lastSeq) {
                lastSeq = seq;
                _view.notifyFrame();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(wait > 0 ? wait : 2));
    }
    LV_LOG_USER("[Model] LVGL thread exit");
}
