/*
 * View for eMP-nes: NES framebuffer lv_image + on-screen virtual pad.
 *
 * Layout (480x480):
 *   - game image: NES 256x240 zoomed 150% -> 384x360 at (48, 0)
 *   - D-pad      : bottom-left diamond (U/D/L/R)
 *   - B / A      : bottom-right
 *   - START/SELECT : small translucent chips over the image top-left
 */
#include "View.h"
#include "nes_fb.h"

#include <cstdint>

namespace Page
{
/* NES button indices (== sn::Controller::Buttons == sf-shim key order). */
enum : int { BTN_A = 0, BTN_B, BTN_SELECT, BTN_START, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT };



/* Static bridge state (single View instance per app). */
static KeyCb s_keyCb;

static void padEventCb(lv_event_t * e)
{
    const int key = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (s_keyCb) {
        if (code == LV_EVENT_PRESSED)
            s_keyCb(key, true);
        else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
            s_keyCb(key, false);
    }
}

static lv_obj_t * mkPadButton(lv_obj_t * parent, const char * text, lv_color_t color,
                              int x, int y, int w, int h, int key)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    stylePad(btn, color);
    addLabel(btn, text);
    lv_obj_add_event_cb(btn, padEventCb, LV_EVENT_PRESSED, (void *)(intptr_t)key);
    lv_obj_add_event_cb(btn, padEventCb, LV_EVENT_RELEASED, (void *)(intptr_t)key);
    lv_obj_add_event_cb(btn, padEventCb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)key);
    return btn;
}

void View::createGame(KeyCb keyCb)
{
    s_keyCb = keyCb;

    /* Screen */
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_scr_load(scr);
    _screen = scr;

    /* NES framebuffer image (RGB565 256x240, zoom 150% = 384x360). */
    static lv_image_dsc_t s_dsc = {0};
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf    = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.flags = 0;
    s_dsc.header.w     = NesFb::kWidth;
    s_dsc.header.h     = NesFb::kHeight;
    s_dsc.header.stride = NesFb::kWidth * 2;
    s_dsc.data_size    = sizeof(uint16_t) * NesFb::kWidth * NesFb::kHeight;
    s_dsc.data         = (const uint8_t *)NesFb::buffer;

    lv_obj_t * img = lv_image_create(scr);
    lv_image_set_src(img, &s_dsc);
    lv_image_set_scale(img, 150); /* 1.5x -> 384x360 */
    lv_obj_set_pos(img, (480 - 384) / 2, 0);
    lv_obj_set_style_image_recolor_opa(img, 0, 0);
    _gameImg = img;

    /* START / SELECT chips (top-left, translucent overlay). */
    lv_obj_t * sta = lv_button_create(scr);
    lv_obj_set_pos(sta, 8, 6);
    lv_obj_set_size(sta, 74, 30);
    lv_obj_set_style_bg_color(sta, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(sta, LV_OPA_60, 0);
    lv_obj_set_style_radius(sta, 6, 0);
    lv_obj_set_style_border_width(sta, 0, 0);
    lv_obj_add_event_cb(sta, padEventCb, LV_EVENT_PRESSED, (void *)(intptr_t)BTN_START);
    lv_obj_add_event_cb(sta, padEventCb, LV_EVENT_RELEASED, (void *)(intptr_t)BTN_START);
    lv_obj_add_event_cb(sta, padEventCb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)BTN_START);
    addLabel(sta, "START");

    lv_obj_t * sel = lv_button_create(scr);
    lv_obj_set_pos(sel, 8, 40);
    lv_obj_set_size(sel, 74, 30);
    lv_obj_set_style_bg_color(sel, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(sel, LV_OPA_60, 0);
    lv_obj_set_style_radius(sel, 6, 0);
    lv_obj_set_style_border_width(sel, 0, 0);
    lv_obj_add_event_cb(sel, padEventCb, LV_EVENT_PRESSED, (void *)(intptr_t)BTN_SELECT);
    lv_obj_add_event_cb(sel, padEventCb, LV_EVENT_RELEASED, (void *)(intptr_t)BTN_SELECT);
    lv_obj_add_event_cb(sel, padEventCb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)BTN_SELECT);
    addLabel(sel, "SEL");

    /* D-pad diamond (bottom-left) */
    mkPadButton(scr, "UP",    lv_color_hex(0x505050), 96, 362, 66, 48, BTN_UP);
    mkPadButton(scr, "DN",    lv_color_hex(0x505050), 96, 428, 66, 48, BTN_DOWN);
    mkPadButton(scr, "LT",    lv_color_hex(0x505050), 26, 395, 66, 48, BTN_LEFT);
    mkPadButton(scr, "RT",    lv_color_hex(0x505050), 166, 395, 66, 48, BTN_RIGHT);

    /* B / A (bottom-right) */
    mkPadButton(scr, "B", lv_color_hex(0x884422), 322, 395, 62, 48, BTN_B);
    mkPadButton(scr, "A", lv_color_hex(0x224488), 388, 395, 62, 48, BTN_A);
}

void View::notifyFrame(void)
{
    if (_gameImg)
        lv_obj_invalidate(_gameImg);
}

void View::showError(const char * msg)
{
    if (!_screen)
        return;
    lv_obj_t * label = lv_label_create(_screen);
    lv_label_set_text(label, msg);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
}

void View::clearScreen(void)
{
    if (_screen) {
        lv_obj_delete(_screen);
        _screen = nullptr;
        _gameImg = nullptr;
    }
}

} /* namespace Page */
