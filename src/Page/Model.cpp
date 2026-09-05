/*
 * Model for eMP-nes: NES lifecycle + LVGL thread (mirrors eMP-gba).
 *
 * Entry order:
 *   1. argv[1] (a concrete ROM)                 -> straight to the game page
 *   2. env EMP_NES_AUTOSTART                    -> straight to the game page
 *   3. otherwise                                -> ROM picker menu page
 *
 * The menu scans the ROM directory (env EMP_NES_ROM_DIR, else the first
 * candidate directory that contains *.nes). Game -> menu: press 退出 in the
 * slide-down top bar or hold SELECT for 2 seconds.
 */
#include "Model.h"
#include "nes_engine.h"
#include "nes_fb.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <dirent.h>

/* Swipe setter (input driver owns it); cleared on page teardown. */
extern "C" void lv_nes_emu_set_swipe_cb(
    void (*cb)(lv_dir_t dir, int start_x, int start_y, void * user_data),
    void * user_data);

using namespace Page;

namespace
{
bool dirHasNes(const std::string & dir)
{
    DIR * d = opendir(dir.c_str());
    if(!d) return false;
    bool found = false;
    struct dirent * ent;
    while((ent = readdir(d)) != nullptr) {
        const std::string name = ent->d_name;
        if(name.size() > 4 && name.compare(name.size() - 4, 4, ".nes") == 0) {
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}
} /* namespace */

std::string Model::resolveRomDir(void)
{
    const char * env = getenv("EMP_NES_ROM_DIR");
    if(env && env[0] && dirHasNes(env)) return env;

    static const char * const candidates[] = {
        "/mnt/UDISK/nes_roms",
        "/mnt/UDISK/roms",
        "/root/nes_roms",
        "/mnt/UDISK",
    };
    for(const char * c : candidates) {
        if(dirHasNes(c)) return c;
    }
    /* fall back to the env value (even if empty) so the menu can report it */
    return (env && env[0]) ? env : "/mnt/UDISK/nes_roms";
}

Model::Model(std::function<void(void)> exitCb, const std::string & romArg)
{
    _exitFlag = false;
    _exitCb = std::move(exitCb);

    _romDir = resolveRomDir();

    const char * vol = getenv("EMP_NES_VOLUME");
    _volume = (vol && vol[0]) ? atoi(vol) : 100;
    if(_volume < 0) _volume = 0;
    if(_volume > 100) _volume = 100;

    /* Demo/self-test hooks: force-open the top bar / volume bar so the
     * overlays can be screenshotted without a touchscreen gesture. */
    _demoTop = getenv("EMP_NES_DEMO_TOP") != nullptr;
    _demoVol = getenv("EMP_NES_DEMO_VOL") != nullptr;

    LV_LOG_USER("[Model] ROM dir = %s, volume = %d", _romDir.c_str(), _volume);

    if(!romArg.empty()) {
        LV_LOG_USER("[Model] argv ROM: %s", romArg.c_str());
        launch(romArg);
    }
    else {
        const char * autoRom = getenv("EMP_NES_AUTOSTART");
        if(autoRom && autoRom[0]) {
            LV_LOG_USER("[Model] autostart ROM: %s", autoRom);
            launch(autoRom);
        }
        else {
            _view.showMenu(_romDir,
                           [this](const std::string & p) { this->launch(p); },
                           _exitCb);
            if(_demoVol) _view.openVolBar();
        }
    }

    /* Spawn the LVGL thread that drives lv_timer_handler (+ animations). */
    _threadLvgl = std::thread([](Model * p) { p->threadLvglHandler(); }, this);
    _threadLvgl.detach();
}

Model::~Model()
{
    _exitFlag = true;
    if(_engine)
        _engine->stop();
    if(_threadLvgl.joinable())
        _threadLvgl.join();
}

void Model::stopGame(void)
{
    _inGame = false;
    _selectTick = 0;

    if(_engine) {
        _engine->stop();
        _engine.reset();
    }
    for(int i = 0; i < 8; i++)
        NesKey::set(i, false);
}

void Model::launch(const std::string & romPath)
{
    /* stop any previous game, release the game page's swipe owner */
    stopGame();
    lv_nes_emu_set_swipe_cb(nullptr, nullptr);
    _view.clearScreen();

    /* try to start the emulator BEFORE building the page: on failure we
     * fall straight back to the picker with a message */
    std::unique_ptr<Nes::Engine> eng(new Nes::Engine());
    if(!eng->loadRom(romPath)) {
        LV_LOG_WARN("[Model] load ROM failed: %s", romPath.c_str());
        _view.showMenu(_romDir,
                       [this](const std::string & p) { this->launch(p); },
                       _exitCb);
        if(_demoVol) _view.openVolBar();
        _view.showError("ROM load failed - pick another");
        return;
    }

    _engine = std::move(eng);
    _inGame = true;

    _view.showGame(romPath,
                   [this](int b, bool down) { onKey(b, down); },
                   [this]() { backToMenu(); });

    if(_demoTop) _view.openTopBar();
    if(_demoVol) _view.openVolBar();

    _engine->start();
    LV_LOG_USER("[Model] playing %s", romPath.c_str());
}

void Model::backToMenu(void)
{
    /* Runs from an LVGL async task / the LVGL thread loop (never from the
     * emulation thread), so widget teardown is safe here. */
    stopGame();
    lv_nes_emu_set_swipe_cb(nullptr, nullptr);
    _view.clearScreen();
    _view.showMenu(_romDir,
                   [this](const std::string & p) { this->launch(p); },
                   _exitCb);
}

void Model::onKey(int button, bool down)
{
    if(_engine)
        _engine->setButton((Nes::Engine::Button)button, down);
}

void Model::threadLvglHandler(void)
{
    uint32_t lastSeq = 0;

    while(!_exitFlag) {
        uint32_t wait = lv_timer_handler();

        if(_inGame) {
            /* push freshly rendered NES frames to the image widget */
            const uint32_t seq = NesFb::frameSeq;
            if(seq != lastSeq) {
                lastSeq = seq;
                _view.notifyFrame();
            }

            /* hold SELECT 2s -> back to the ROM menu (mirrors eMP-gba) */
            if(NesKey::get(BTN_SELECT)) {
                if(_selectTick == 0)
                    _selectTick = lv_tick_get();
                else if(lv_tick_elaps(_selectTick) > 2000) {
                    LV_LOG_USER("[Model] SELECT long-press: back to menu");
                    backToMenu();
                    continue;
                }
            }
            else {
                _selectTick = 0;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(wait > 0 ? wait : 2));
    }
    LV_LOG_USER("[Model] LVGL thread exit");
}
