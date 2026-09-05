/*
 * View for eMP-nes: two pages + page-level gesture overlays, mirroring
 * eMP-gba (gba_menu.c / gba_view.c / gba_overlay.c style).
 *
 *   ROM menu page : lv_list of *.nes + PINNED top bar (截图 / 音量 / 退出).
 *                   Gestures disabled.
 *   Game page      : NES framebuffer lv_image filling the 480x480 panel
 *                    (2x integer zoom, overscan-cropped) + translucent
 *                    virtual pad at the bottom + HIDDEN top bar and volume
 *                    bar revealed by gestures:
 *                      swipe down/up      -> toggle top bar
 *                      swipe left/right   -> toggle volume bar
 *
 * Scrolling is disabled on every full-screen canvas (no pan on drag).
 * 截图 saves the raw /dev/fb0 480x480 area as a PPM under
 * /mnt/UDISK/screenshots (env EMP_NES_SHOT_DIR overrides) and pops a toast.
 */
#include "View.h"
#include "nes_fb.h"
#include "nes_font.h"

#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* Swipe setter implemented in src/HAL/input_mt.cpp (C linkage). */
extern "C" void lv_nes_emu_set_swipe_cb(
    void (*cb)(lv_dir_t dir, int start_x, int start_y, void * user_data),
    void * user_data);

#ifndef NES_SCREEN_W
#define NES_SCREEN_W 480
#endif
#ifndef NES_SCREEN_H
#define NES_SCREEN_H 480
#endif

#define OVL_TOP_BAR_H 38
#define OVL_VOL_BAR_W 124
#define OVL_VOL_BAR_H 192
#define OVL_VOL_BAR_Y 60
#define OVL_VOL_BAR_X (NES_SCREEN_W - OVL_VOL_BAR_W - 10)
#define OVL_ANIM_MS 400

namespace Page
{
/* NES button indices (== sn::Controller::Buttons == sf-shim key order). */
enum : int
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

/* ------------------------------------------------------------------ */
/* overlay state (one per page)                                        */
/* ------------------------------------------------------------------ */

struct Overlay
{
    lv_obj_t * top_bar = nullptr;
    lv_obj_t * vol_bar = nullptr;
    lv_obj_t * vol_slider = nullptr;
    lv_obj_t * vol_label = nullptr;
    bool top_visible = false;
    bool vol_visible = false;

    /* what 退出 does on this page (game: back to menu; menu: app exit) */
    std::function<void(void)> onExit;
};

namespace
{

/* ------------------------------------------------------------------ */
/* tiny helpers                                                        */
/* ------------------------------------------------------------------ */

void noScroll(lv_obj_t * obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void nes_anim_path(lv_obj_t * obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to,
                   uint32_t time, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, time);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

void nes_anim(lv_obj_t * obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t time)
{
    nes_anim_path(obj, exec, from, to, time, lv_anim_path_ease_out);
}

/* Overlay text font: SmileySans (CJK-capable) when available. */
const lv_font_t * ovl_font(int size)
{
    lv_font_t * f = nes_font_get(size);
    return f ? f : (const lv_font_t *)&lv_font_montserrat_14;
}

/* ------------------------------------------------------------------ */
/* floating bar show / hide (eMP-video / eMP-gba ease-out + expand)    */
/* ------------------------------------------------------------------ */

void top_bar_show(Overlay * ov)
{
    ov->top_visible = true;
    const int w = NES_SCREEN_W * 9 / 10;
    nes_anim(ov->top_bar, (lv_anim_exec_xcb_t)lv_obj_set_y, -OVL_TOP_BAR_H, 0, OVL_ANIM_MS);
    nes_anim(ov->top_bar, (lv_anim_exec_xcb_t)lv_obj_set_width, 20, w, OVL_ANIM_MS);
    nes_anim(ov->top_bar, (lv_anim_exec_xcb_t)lv_obj_set_x,
             (NES_SCREEN_W - 20) / 2, (NES_SCREEN_W - w) / 2, OVL_ANIM_MS);
}

void top_bar_hide(Overlay * ov)
{
    ov->top_visible = false;
    const int w = NES_SCREEN_W * 9 / 10;
    nes_anim(ov->top_bar, (lv_anim_exec_xcb_t)lv_obj_set_y, 0, -OVL_TOP_BAR_H, OVL_ANIM_MS);
    nes_anim(ov->top_bar, (lv_anim_exec_xcb_t)lv_obj_set_width, w, 20, OVL_ANIM_MS);
    nes_anim(ov->top_bar, (lv_anim_exec_xcb_t)lv_obj_set_x,
             (NES_SCREEN_W - w) / 2, (NES_SCREEN_W - 20) / 2, OVL_ANIM_MS);
}

void vol_bar_show(Overlay * ov)
{
    ov->vol_visible = true;
    nes_anim(ov->vol_bar, (lv_anim_exec_xcb_t)lv_obj_set_x, NES_SCREEN_W, OVL_VOL_BAR_X, OVL_ANIM_MS);
    nes_anim(ov->vol_bar, (lv_anim_exec_xcb_t)lv_obj_set_height, 30, OVL_VOL_BAR_H, OVL_ANIM_MS);
}

void vol_bar_hide(Overlay * ov)
{
    ov->vol_visible = false;
    nes_anim(ov->vol_bar, (lv_anim_exec_xcb_t)lv_obj_set_x, OVL_VOL_BAR_X, NES_SCREEN_W, OVL_ANIM_MS);
    nes_anim(ov->vol_bar, (lv_anim_exec_xcb_t)lv_obj_set_height, OVL_VOL_BAR_H, 30, OVL_ANIM_MS);
}

/* ------------------------------------------------------------------ */
/* toast (slide in from the right, rest, drop + fade, delete)          */
/* ------------------------------------------------------------------ */

void toast_set_opa(void * obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void toast_drop_ready(lv_anim_t * a)
{
    lv_obj_t * pop = (lv_obj_t *)a->var;
    lv_obj_delete(pop);
}

void toast_timer_cb(lv_timer_t * t)
{
    lv_obj_t * pop = (lv_obj_t *)lv_timer_get_user_data(t);
    if(pop == nullptr) return;

    const int x = lv_obj_get_x(pop);
    const int y = lv_obj_get_y(pop);
    nes_anim_path(pop, (lv_anim_exec_xcb_t)lv_obj_set_x, x, x - 30, 500, lv_anim_path_linear);
    nes_anim_path(pop, (lv_anim_exec_xcb_t)lv_obj_set_y, y, y + 100, 500, lv_anim_path_ease_in);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pop);
    lv_anim_set_exec_cb(&a, toast_set_opa);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 500);
    lv_anim_set_ready_cb(&a, toast_drop_ready);
    lv_anim_start(&a);
}

void toast_create(const char * tips)
{
    const int w = 150, h = 40;
    lv_obj_t * pop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(pop);
    noScroll(pop);
    lv_obj_set_size(pop, w, h);
    lv_obj_set_style_bg_opa(pop, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pop, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_radius(pop, 10, 0);

    lv_obj_set_pos(pop, NES_SCREEN_W, NES_SCREEN_H / 2);
    nes_anim(pop, (lv_anim_exec_xcb_t)lv_obj_set_x, NES_SCREEN_W, NES_SCREEN_W - w + 11, 700);

    lv_obj_t * label = lv_label_create(pop);
    lv_obj_set_style_text_font(label, ovl_font(16), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
    lv_label_set_text(label, tips);

    lv_timer_t * t = lv_timer_create(toast_timer_cb, 1500, pop);
    lv_timer_set_repeat_count(t, 1);
}

/* ------------------------------------------------------------------ */
/* screenshot (raw /dev/fb0 first 480x480 -> PPM)                      */
/* ------------------------------------------------------------------ */

bool save_fb0_ppm(const char * dir_path)
{
    const size_t px = (size_t)NES_SCREEN_W * NES_SCREEN_H;

    int fd = ::open("/dev/fb0", O_RDONLY);
    if(fd < 0) return false;

    std::vector<uint8_t> buf(px * 4);
    ssize_t got = 0;
    while(got < (ssize_t)buf.size()) {
        ssize_t r = ::read(fd, buf.data() + got, buf.size() - (size_t)got);
        if(r <= 0) break;
        got += r;
    }
    ::close(fd);
    if(got < (ssize_t)buf.size()) return false;

    ::mkdir(dir_path, 0777); /* ignore EEXIST; parent expected to exist */

    char path[512];
    ::snprintf(path, sizeof(path), "%s/emp_nes_%u.ppm", dir_path, (unsigned)(lv_tick_get() / 10));
    FILE * f = ::fopen(path, "wb");
    if(!f) return false;

    ::fprintf(f, "P6\n%d %d\n255\n", NES_SCREEN_W, NES_SCREEN_H);
    /* fb0: XRGB8888 little-endian -> bytes [B, G, R, X] per pixel */
    for(size_t i = 0; i < px; i++) {
        const uint8_t * p = buf.data() + i * 4;
        ::fputc(p[2], f); /* R */
        ::fputc(p[1], f); /* G */
        ::fputc(p[0], f); /* B */
    }
    ::fclose(f);
    LV_LOG_USER("[shot] saved %s", path);
    return true;
}

/* ------------------------------------------------------------------ */
/* overlay widgets                                                     */
/* ------------------------------------------------------------------ */

lv_obj_t * overlay_btn_create(lv_obj_t * parent, const char * text,
                              lv_color_t bg, lv_color_t pressed, int w, int h, uintptr_t id)
{
    lv_obj_t * btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    noScroll(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_ext_click_area(btn, 8);
    lv_obj_set_user_data(btn, (void *)id);

    lv_obj_t * label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, ovl_font(18), 0);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return btn;
}

/* eMP-video / eMP-gba slider look */
lv_obj_t * vol_slider_create(lv_obj_t * parent, int value)
{
    lv_obj_t * obj = lv_slider_create(parent);
    lv_obj_remove_style_all(obj);
    lv_slider_set_mode(obj, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_range(obj, 0, 100);
    lv_slider_set_value(obj, value, LV_ANIM_OFF);

    lv_obj_set_size(obj, lv_pct(40), lv_pct(90));
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, -6);

    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 0, LV_PART_KNOB);
    lv_obj_set_style_radius(obj, 10, LV_PART_KNOB);
    lv_obj_set_style_pad_all(obj, 1, LV_PART_KNOB);

    lv_obj_set_style_radius(obj, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3c9ba6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_radius(obj, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xa4d9b2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_t * icon = lv_img_create(obj);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);
    lv_img_set_src(icon, LV_SYMBOL_VOLUME_MAX);
    return obj;
}

/* ------------------------------------------------------------------ */
/* overlay events                                                      */
/* ------------------------------------------------------------------ */

void overlay_event_cb(lv_event_t * e)
{
    Overlay * ov = (Overlay *)lv_event_get_user_data(e);
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_current_target(e);
    if(ov == nullptr || btn == nullptr) return;

    const uintptr_t id = (uintptr_t)lv_obj_get_user_data(btn);

    switch(id) {
    case 1: { /* 截图 */
        const char * dir = getenv("EMP_NES_SHOT_DIR");
        std::string d = (dir && dir[0]) ? dir : "/mnt/UDISK/screenshots";
        toast_create(save_fb0_ppm(d.c_str()) ? "截图成功" : "截图失败");
        break;
    }
    case 2: /* 音量 */
        if(ov->vol_visible) vol_bar_hide(ov);
        else vol_bar_show(ov);
        break;
    case 3: /* 退出 */
        if(ov->onExit) {
            /* copy: the page (and ov) may be destroyed while the async runs */
            auto * req = new std::function<void(void)>(ov->onExit);
            lv_async_call([](void * p) {
                auto * fn = (std::function<void(void)> *)p;
                if(*fn) (*fn)();
                delete fn;
            }, req);
        }
        break;
    default:
        break;
    }
}

void vol_slider_event_cb(lv_event_t * e)
{
    Overlay * ov = (Overlay *)lv_event_get_user_data(e);
    if(ov == nullptr) return;

    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        int v = (int)lv_slider_get_value(ov->vol_slider);
        lv_label_set_text_fmt(ov->vol_label, "音量 %d", v);
        /* ALSA output is not wired yet (v0): the value is kept on the slider
         * so a future audio port can read it. */
    }
}

/* game-page swipe handler (same gating as eMP-gba overlay) */
void overlay_swipe_cb(lv_dir_t dir, int start_x, int start_y, void * user_data)
{
    Overlay * ov = (Overlay *)user_data;
    if(ov == nullptr) return;

    bool in_vol = (ov->vol_visible &&
                   start_x >= OVL_VOL_BAR_X && start_x < OVL_VOL_BAR_X + OVL_VOL_BAR_W &&
                   start_y >= OVL_VOL_BAR_Y && start_y < OVL_VOL_BAR_Y + OVL_VOL_BAR_H);

    switch(dir) {
    case LV_DIR_BOTTOM:
        if(!ov->top_visible && !in_vol) top_bar_show(ov);
        break;
    case LV_DIR_TOP:
        if(ov->top_visible && !in_vol) top_bar_hide(ov);
        break;
    case LV_DIR_LEFT:
        if(!ov->vol_visible) vol_bar_show(ov);
        break;
    case LV_DIR_RIGHT:
        if(ov->vol_visible) vol_bar_hide(ov);
        break;
    default:
        break;
    }
}

void overlay_delete_cb(lv_event_t * e)
{
    Overlay * ov = static_cast<Overlay *>(lv_event_get_user_data(e));
    if(ov) delete ov;
}

int volume_default(void)
{
    static int s_vol = -1;
    if(s_vol < 0) {
        const char * v = getenv("EMP_NES_VOLUME");
        s_vol = (v && v[0]) ? atoi(v) : 100;
        if(s_vol < 0) s_vol = 0;
        if(s_vol > 100) s_vol = 100;
    }
    return s_vol;
}

/* Build the top bar (pinned on menu, hidden on game) + volume bar + toast
 * wiring on `root`; registers/clears the global swipe callback. */
Overlay * overlay_create(lv_obj_t * root, const char * title,
                               bool top_pinned, bool gesture_on,
                               std::function<void(void)> onExit)
{
    Overlay * ov = new Overlay();
    ov->onExit = std::move(onExit);

    /* ---- top bar (90% width, #EEEEEE @90) ---- */
    lv_obj_t * bar = lv_obj_create(root);
    lv_obj_remove_style_all(bar);
    noScroll(bar);
    const int bar_w = NES_SCREEN_W * 9 / 10;
    lv_obj_set_size(bar, bar_w, OVL_TOP_BAR_H);
    lv_obj_set_pos(bar, (NES_SCREEN_W - bar_w) / 2, top_pinned ? 0 : -OVL_TOP_BAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_radius(bar, 5, 0);
    ov->top_bar = bar;
    ov->top_visible = top_pinned;

    lv_obj_t * b = overlay_btn_create(bar, "截图", lv_color_hex(0x0078BA), lv_color_hex(0x005E93), 54, 34, 1);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(b, overlay_event_cb, LV_EVENT_CLICKED, ov);

    b = overlay_btn_create(bar, "音量", lv_color_hex(0x4EA35A), lv_color_hex(0x3D8346), 54, 34, 2);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 74, 0);
    lv_obj_add_event_cb(b, overlay_event_cb, LV_EVENT_CLICKED, ov);

    b = overlay_btn_create(bar, "x", lv_color_hex(0xFF6056), lv_color_hex(0xE44543), 34, 34, 3);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(b, overlay_event_cb, LV_EVENT_CLICKED, ov);

    lv_obj_t * tlabel = lv_label_create(bar);
    lv_obj_set_style_text_font(tlabel, ovl_font(20), 0);
    lv_obj_set_style_text_color(tlabel, lv_color_hex(0x555555), 0);
    lv_label_set_long_mode(tlabel, LV_LABEL_LONG_DOT);
    lv_label_set_text(tlabel, title);
    lv_obj_set_width(tlabel, bar_w - 200);
    lv_obj_align(tlabel, LV_ALIGN_CENTER, 0, 0);

    /* ---- volume bar ---- */
    lv_obj_t * vbar = lv_obj_create(root);
    lv_obj_remove_style_all(vbar);
    noScroll(vbar);
    lv_obj_set_size(vbar, OVL_VOL_BAR_W, OVL_VOL_BAR_H);
    lv_obj_set_pos(vbar, NES_SCREEN_W, OVL_VOL_BAR_Y); /* hidden off right */
    lv_obj_set_style_bg_opa(vbar, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(vbar, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_radius(vbar, 10, 0);
    ov->vol_bar = vbar;
    ov->vol_visible = false;

    lv_obj_t * slider = vol_slider_create(vbar, volume_default());
    lv_obj_add_event_cb(slider, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, ov);
    ov->vol_slider = slider;

    lv_obj_t * lab = lv_label_create(vbar);
    lv_obj_set_style_text_font(lab, ovl_font(16), 0);
    lv_label_set_text_fmt(lab, "音量 %d", volume_default());
    lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_color(lab, lv_color_hex(0x666666), 0);
    ov->vol_label = lab;

    /* swipe callback ownership: the game page owns it, the menu clears it */
    if(gesture_on) lv_nes_emu_set_swipe_cb(overlay_swipe_cb, ov);
    else lv_nes_emu_set_swipe_cb(nullptr, nullptr);

    /* free the overlay when its page root dies */
    lv_obj_add_event_cb(root, overlay_delete_cb, LV_EVENT_DELETE, ov);
    return ov;
}

/* ------------------------------------------------------------------ */
/* virtual pad (game page)                                             */
/* ------------------------------------------------------------------ */

KeyCb s_keyCb;

void padEventCb(lv_event_t * e)
{
    const int key = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if(s_keyCb) {
        if(code == LV_EVENT_PRESSED) s_keyCb(key, true);
        else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
            s_keyCb(key, false);
    }
}

lv_obj_t * mkPadButton(lv_obj_t * parent, const char * text, lv_color_t color,
                       int x, int y, int w, int h, int key)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_90, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_lighten(color, 60), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_90, LV_STATE_PRESSED);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, padEventCb, LV_EVENT_PRESSED, (void *)(intptr_t)key);
    lv_obj_add_event_cb(btn, padEventCb, LV_EVENT_RELEASED, (void *)(intptr_t)key);
    lv_obj_add_event_cb(btn, padEventCb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)key);
    return btn;
}

/* ROM selection holders (single menu instance per app). */
RomSelectedCb s_romCb;
std::vector<std::string> s_romPaths;

} /* namespace */

/* ================================================================== */
/* Page::View public API                                               */
/* ================================================================== */

void View::showMenu(const std::string & romDir, RomSelectedCb romCb, ExitCb exitCb)
{
    s_romCb = romCb;
    s_romPaths.clear();

    /* ---- fresh, non-scrollable screen ---- */
    lv_obj_t * scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    noScroll(scr);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_scr_load(scr);
    _screen = scr;
    _gameImg = nullptr;

    /* ---- ROM list (keeps its own scroll) ---- */
    lv_obj_t * list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_center(list);
    lv_obj_set_style_pad_top(list, 44, 0); /* leave room for the pinned top bar */
    lv_obj_set_style_pad_bottom(list, 12, 0);

    lv_list_add_text(list, "Select ROM");

    DIR * d = ::opendir(romDir.c_str());
    if(d) {
        struct dirent * ent;
        while((ent = ::readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if(name.size() > 4 && name.compare(name.size() - 4, 4, ".nes") == 0)
                s_romPaths.push_back(romDir + "/" + name);
        }
        ::closedir(d);
        std::sort(s_romPaths.begin(), s_romPaths.end());
    }

    if(s_romPaths.empty()) {
        lv_list_add_text(list, "No .nes files in:");
        lv_list_add_text(list, romDir.c_str());
    }

    for(size_t i = 0; i < s_romPaths.size(); i++) {
        const char * fn = s_romPaths[i].c_str();
        const char * base = strrchr(fn, '/');
        base = base ? base + 1 : fn;

        lv_obj_t * btn = lv_list_add_button(list, nullptr, base);
        lv_obj_set_user_data(btn, (void *)(i + 1)); /* 1-based index */
        lv_obj_add_event_cb(btn, [](lv_event_t * e) {
            if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            lv_obj_t * obj = (lv_obj_t *)lv_event_get_current_target(e);
            size_t idx = (size_t)lv_obj_get_user_data(obj);
            if(idx > 0 && idx <= s_romPaths.size() && s_romCb)
                s_romCb(s_romPaths[idx - 1]);
        }, LV_EVENT_CLICKED, nullptr);
    }

    /* ---- pinned top bar (退出 -> app exit), gestures OFF ---- */
    auto appExit = exitCb;
    _ov = overlay_create(scr, "NES CONSOLE", true /*top_pinned*/, false /*gesture_on*/,
                         [appExit]() { if(appExit) appExit(); });
}

void View::showGame(const std::string & romPath, KeyCb keyCb, GameExitCb gameExitCb)
{
    s_keyCb = keyCb;

    /* ---- fresh, non-scrollable screen ---- */
    lv_obj_t * scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    noScroll(scr);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_scr_load(scr);
    _screen = scr;

    /* ---- NES framebuffer image: 2x pre-scaled RGB565 512x480,
     * center-cropped onto the 480x480 panel (16px overscan per side) ---- */
    static lv_image_dsc_t s_dsc = {0};
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf    = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.flags = 0;
    s_dsc.header.w     = NesFb::kOutW;
    s_dsc.header.h     = NesFb::kOutH;
    s_dsc.header.stride = NesFb::kOutW * 2;
    s_dsc.data_size    = sizeof(uint16_t) * NesFb::kOutW * NesFb::kOutH;
    s_dsc.data         = (const uint8_t *)NesFb::fb2x;

    lv_obj_t * img = lv_image_create(scr);
    lv_image_set_src(img, &s_dsc);
    lv_obj_set_pos(img, -NesFb::kCropX, 0); /* center-crop to 480x480 */
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    _gameImg = img;

    /* ---- translucent virtual pad (bottom of the panel) ---- */
    /* D-pad: bottom-left */
    mkPadButton(scr, "UP", lv_color_hex(0x505050), 96, 356, 62, 46, BTN_UP);
    mkPadButton(scr, "DN", lv_color_hex(0x505050), 96, 422, 62, 46, BTN_DOWN);
    mkPadButton(scr, "LT", lv_color_hex(0x505050), 30, 389, 62, 46, BTN_LEFT);
    mkPadButton(scr, "RT", lv_color_hex(0x505050), 162, 389, 62, 46, BTN_RIGHT);

    /* B / A: bottom-right */
    mkPadButton(scr, "B", lv_color_hex(0x884422), 320, 389, 62, 46, BTN_B);
    mkPadButton(scr, "A", lv_color_hex(0x224488), 388, 389, 62, 46, BTN_A);

    /* START / SELECT: small translucent chips in the centre gap */
    lv_obj_t * sta = lv_button_create(scr);
    lv_obj_set_pos(sta, 238, 362);
    lv_obj_set_size(sta, 68, 28);
    lv_obj_set_style_bg_color(sta, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(sta, LV_OPA_60, 0);
    lv_obj_set_style_radius(sta, 6, 0);
    lv_obj_set_style_border_width(sta, 0, 0);
    lv_obj_add_event_cb(sta, padEventCb, LV_EVENT_PRESSED, (void *)(intptr_t)BTN_START);
    lv_obj_add_event_cb(sta, padEventCb, LV_EVENT_RELEASED, (void *)(intptr_t)BTN_START);
    lv_obj_add_event_cb(sta, padEventCb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)BTN_START);
    lv_obj_t * sta_l = lv_label_create(sta);
    lv_label_set_text(sta_l, "START");
    lv_obj_set_style_text_color(sta_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(sta_l, &lv_font_montserrat_14, 0);
    lv_obj_center(sta_l);

    lv_obj_t * sel = lv_button_create(scr);
    lv_obj_set_pos(sel, 238, 402);
    lv_obj_set_size(sel, 68, 28);
    lv_obj_set_style_bg_color(sel, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(sel, LV_OPA_60, 0);
    lv_obj_set_style_radius(sel, 6, 0);
    lv_obj_set_style_border_width(sel, 0, 0);
    lv_obj_add_event_cb(sel, padEventCb, LV_EVENT_PRESSED, (void *)(intptr_t)BTN_SELECT);
    lv_obj_add_event_cb(sel, padEventCb, LV_EVENT_RELEASED, (void *)(intptr_t)BTN_SELECT);
    lv_obj_add_event_cb(sel, padEventCb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)BTN_SELECT);
    lv_obj_t * sel_l = lv_label_create(sel);
    lv_label_set_text(sel_l, "SEL");
    lv_obj_set_style_text_color(sel_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(sel_l, &lv_font_montserrat_14, 0);
    lv_obj_center(sel_l);

    /* ---- gesture overlays: top bar hidden, volume bar hidden,
     * swipe-down/left reveal them; 退出 -> back to menu ---- */
    std::string title = romPath;
    const char * slash = strrchr(title.c_str(), '/');
    if(slash) title = slash + 1;

    auto backToMenu = gameExitCb;
    _ov = overlay_create(scr, title.c_str(), false /*top_pinned*/, true /*gesture_on*/,
                         [backToMenu]() { if(backToMenu) backToMenu(); });
}

void View::notifyFrame(void)
{
    if(_gameImg) lv_obj_invalidate(_gameImg);
}

void View::openTopBar(void)
{
    if(_ov && !_ov->top_visible) top_bar_show(_ov);
}

void View::openVolBar(void)
{
    if(_ov && !_ov->vol_visible) vol_bar_show(_ov);
}

void View::showError(const char * msg)
{
    if(!_screen) return;
    lv_obj_t * label = lv_label_create(_screen);
    lv_label_set_text(label, msg);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
}

void View::clearScreen(void)
{
    if(_screen) {
        lv_obj_delete(_screen); /* fires overlay_delete_cb -> delete _ov */
        _screen = nullptr;
        _gameImg = nullptr;
        _ov = nullptr;
    }
}

} /* namespace Page */
