/*
 * Multi-touch input for eMP-nes (Allwinner T113-S3, Goodix gt9xx touchscreen).
 *
 * Why a custom driver instead of lv_evdev_create?
 *   LVGL v9's built-in evdev driver opens ONE fd and its pointer read callback
 *   always reports touch_data[0] (slot 0) only -- it can never surface a second
 *   touch point. Per LVGL's own docs, multi-touch is done by creating several
 *   POINTER indevs, each reporting a different touch slot. This module does
 *   exactly that: it opens /dev/input/event1 once per indev, parses the kernel
 *   MT event stream, and hands slot N to indev N. The on-screen virtual GBA
 *   buttons (lv_btn) are then pressable by independent fingers, so e.g.
 *   "Up + A" or "L + R" can be held simultaneously.
 *
 * The Goodix device on this board advertises ABS_MT_POSITION_X/Y and
 * ABS_MT_TRACKING_ID but NOT ABS_MT_SLOT (no MT slot bit in the abs bitmap):
 * it speaks **MT protocol A**, where contacts are delimited by SYN_MT_REPORT
 * and a contact absent from a frame is considered lifted. Captured on-board
 * event order per contact is: POSITION_X, POSITION_Y, TOUCH_MAJOR,
 * WIDTH_MAJOR, TRACKING_ID, then SYN_MT_REPORT (coordinates arrive BEFORE the
 * tracking id, and the tracking id of each contact is the contact index).
 *
 * The parser is protocol-agnostic:
 *   - If ABS_MT_SLOT is seen (protocol B), coordinates/tracking-id update the
 *     slot selected by ABS_MT_SLOT directly (release on TRACKING_ID == -1).
 *   - Otherwise (protocol A) a contact is accumulated between SYN_MT_REPORT
 *     markers and committed to a slot at SYN_MT_REPORT: slot = tracking id if
 *     a sane one is present, else the contact's frame order. At SYN_REPORT any
 *     slot that was not seen in this frame is released (absence == lifted).
 * Legacy single-touch (ABS_X/ABS_Y + BTN_TOUCH) is handled as slot 0 until
 * the first MT frame is seen.
 *
 * NOTE (why slot identity matters): each POINTER indev reports one slot, and
 * LVGL treats a pointer that suddenly jumps to another object as a DRAG (it
 * sends PRESS_LOST to the old object and does NOT send PRESSED to the new one
 * when the pointer was already pressed). So contacts MUST stay on stable slots
 * or two-finger presses get eaten. The old tid-mapping parser mapped both
 * contacts to slot 0 (both tids were reused per frame and x/y arrived before
 * tid), which swapped contacts between indevs and broke every second press.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

extern "C" {
#include "lvgl/lvgl.h"
}

/* Number of simultaneous touch points exposed to LVGL (one POINTER indev
 * each). Bump this if the panel and use-case need more (the GBA pad rarely
 * needs more than 2-3 held at once, but 2 satisfies "at least two points"). */
#ifndef NES_INPUT_TOUCH_POINTS
#define NES_INPUT_TOUCH_POINTS 2
#endif

#define MT_MAX_SLOTS 10

typedef struct {
    int fd;                 /* own open fd on the evdev device */
    int slot;               /* which contact (slot) this indev reports */
    int min_x, min_y;       /* calibration input range */
    int max_x, max_y;
    bool saw_slot;          /* protocol B (ABS_MT_SLOT seen)? */
    bool saw_mt;            /* at least one MT frame (SYN_MT_REPORT) seen? */

    /* Parsed state for every contact, refreshed from the event stream. */
    int cur_slot;                       /* protocol B: slot in progress */
    int slot_x[MT_MAX_SLOTS];
    int slot_y[MT_MAX_SLOTS];
    lv_indev_state_t slot_state[MT_MAX_SLOTS];

    /* protocol A: pending contact accumulated between SYN_MT_REPORT markers. */
    int pending_x, pending_y;           /* -1 = not set */
    int pending_tid;                    /* -1 = none */
    int contact_idx;                    /* contacts committed so far in this frame */
    bool slot_seen[MT_MAX_SLOTS];       /* slots seen in the current frame */

    lv_point_t last;        /* last reported point (used on release) */
    int last_active;        /* last printed active-contact count (diag) */

} mt_indev_ctx_t;

static int mt_calib(int v, int in_min, int in_max, int out_min, int out_max)
{
    if(in_max > in_min)
        v = (v - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    if(v < out_min) v = out_min;
    if(v > out_max) v = out_max;
    return v;
}

/* Protocol A: a SYN_MT_REPORT ended one contact's event group. Commit the
 * accumulated (x, y, tid) to a slot: prefer the tracking id as the slot index
 * (stable across frames on this panel: tid == contact index), fall back to the
 * contact's order in the frame. */
static void mt_commit_contact(mt_indev_ctx_t * c)
{
    if(c->pending_x < 0 && c->pending_y < 0) return;

    /* Prefer the tracking id as the slot index (stable across frames on this
     * panel: tid == contact index). If that slot already holds a contact this
     * frame (duplicate tid), fall back to the contact's frame order. */
    int s = (c->pending_tid >= 0 && c->pending_tid < MT_MAX_SLOTS)
                ? c->pending_tid : c->contact_idx;
    if(c->slot_seen[s]) s = c->contact_idx;
    if(s >= MT_MAX_SLOTS) s = MT_MAX_SLOTS - 1;

    c->slot_x[s] = (c->pending_x >= 0) ? c->pending_x : 0;
    c->slot_y[s] = (c->pending_y >= 0) ? c->pending_y : 0;
    c->slot_state[s] = LV_INDEV_STATE_PRESSED;
    c->slot_seen[s] = true;
    c->contact_idx++;

    c->pending_x = c->pending_y = c->pending_tid = -1;
}

static void mt_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    mt_indev_ctx_t * c = (mt_indev_ctx_t *)lv_indev_get_driver_data(indev);
    if(c == NULL) return;

    struct input_event in;
    ssize_t br;
    while((br = read(c->fd, &in, sizeof(in))) > 0) {
        if(in.type == EV_ABS) {
            switch(in.code) {
            case ABS_MT_SLOT: /* protocol B */
                c->saw_slot = true;
                if(in.value >= 0 && in.value < MT_MAX_SLOTS)
                    c->cur_slot = in.value;
                else
                    c->cur_slot = MT_MAX_SLOTS - 1;
                break;

            case ABS_MT_TRACKING_ID:
                if(c->saw_slot) {
                    /* protocol B: press/release the active slot */
                    if(in.value < 0)
                        c->slot_state[c->cur_slot] = LV_INDEV_STATE_RELEASED;
                    else
                        c->slot_state[c->cur_slot] = LV_INDEV_STATE_PRESSED;
                }
                else {
                    /* protocol A: remember the tid of the pending contact */
                    c->pending_tid = in.value;
                }
                break;

            case ABS_MT_POSITION_X:
                if(c->saw_slot) {
                    c->slot_x[c->cur_slot] = in.value;
                }
                else {
                    c->pending_x = in.value;   /* coords come BEFORE tid on this panel */
                }
                break;
            case ABS_MT_POSITION_Y:
                if(c->saw_slot) {
                    c->slot_y[c->cur_slot] = in.value;
                }
                else {
                    c->pending_y = in.value;
                }
                break;

            case ABS_X: /* legacy single-touch mirror -> slot 0 */
                c->slot_x[0] = in.value;
                break;
            case ABS_Y:
                c->slot_y[0] = in.value;
                break;

            default:
                break;
            }
        }
        else if(in.type == EV_KEY && in.code == BTN_TOUCH) {
            /* Single-touch emulation: only drive slot 0 while no MT frame has
             * been seen. Once MT is active, press state is derived from frame
             * membership (this panel also toggles BTN_TOUCH on multi-finger
             * transitions, which would otherwise mis-release slot 0). */
            if(!c->saw_mt) {
                c->slot_state[0] = (in.value == 0)
                                     ? LV_INDEV_STATE_RELEASED
                                     : LV_INDEV_STATE_PRESSED;
            }
        }
        else if(in.type == EV_SYN && in.code == SYN_MT_REPORT) {
            if(!c->saw_slot) {
                c->saw_mt = true;
                mt_commit_contact(c);
            }
        }
        else if(in.type == EV_SYN && in.code == SYN_REPORT) {
            if(!c->saw_slot) {
                /* Protocol A: a contact that was not in this frame is lifted. */
                for(int s = 0; s < MT_MAX_SLOTS; s++) {
                    if(!c->slot_seen[s])
                        c->slot_state[s] = LV_INDEV_STATE_RELEASED;
                    c->slot_seen[s] = false;
                }
                c->contact_idx = 0;
            }
        }
    }

    /* Report the contact assigned to this indev. */
    int s = c->slot;
    if(s >= MT_MAX_SLOTS) s = MT_MAX_SLOTS - 1;

    data->state = c->slot_state[s];
    if(data->state == LV_INDEV_STATE_PRESSED) {
        lv_display_t * disp = lv_indev_get_display(indev);
        int off_x = disp ? (int)lv_display_get_offset_x(disp) : 0;
        int off_y = disp ? (int)lv_display_get_offset_y(disp) : 0;
        int w = disp ? (int)lv_display_get_horizontal_resolution(disp) : 480;
        int h = disp ? (int)lv_display_get_vertical_resolution(disp) : 480;
        c->last.x = mt_calib(c->slot_x[s], c->min_x, c->max_x, off_x, off_x + w - 1);
        c->last.y = mt_calib(c->slot_y[s], c->min_y, c->max_y, off_y, off_y + h - 1);
    }
    data->point = c->last;
    /* Diagnostic: indev #0 also reports how many contacts are currently
     * active (across all slots) so a two-finger press is visible in the log. */
    if(c->slot == 0) {
        int n = 0;
        for(int i = 0; i < MT_MAX_SLOTS; i++)
            if(c->slot_state[i] == LV_INDEV_STATE_PRESSED) n++;
        if(n != c->last_active) {
            fprintf(stderr, "[MT] active contacts = %d\n", n);
            c->last_active = n;
        }

    }
}

namespace HAL {

void InitMultiTouchInput(void)
{
    const char * dev = "/dev/input/event1";

    for(int i = 0; i < NES_INPUT_TOUCH_POINTS; i++) {
        int fd = open(dev, O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if(fd < 0) {
            LV_LOG_WARN("MT touch: open %s failed: %s", dev, strerror(errno));
            continue;
        }

        mt_indev_ctx_t * c = (mt_indev_ctx_t *)lv_malloc_zeroed(sizeof(*c));
        if(c == NULL) {
            LV_LOG_WARN("MT touch: alloc failed for indev #%d", i);
            close(fd);
            continue;
        }
        c->fd = fd;
        c->slot = i;
        c->cur_slot = 0;
        c->pending_x = c->pending_y = c->pending_tid = -1;
        c->contact_idx = 0;
        c->last.x = 0;
        c->last.y = 0;
        c->last_active = -1;
        for(int s = 0; s < MT_MAX_SLOTS; s++) {
            c->slot_state[s] = LV_INDEV_STATE_RELEASED;
            c->slot_seen[s] = false;
        }

        /* Calibration range: prefer the MT position axes (this panel only
         * advertises ABS_MT_POSITION_*, not ABS_X/ABS_Y), fall back to the
         * legacy axes, then to the display size. */
        struct input_absinfo ai;
        if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) {
            c->min_x = ai.minimum; c->max_x = ai.maximum;
        }
        else if(ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0) {
            c->min_x = ai.minimum; c->max_x = ai.maximum;
        }
        else {
            c->min_x = 0; c->max_x = 480;
        }
        if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) {
            c->min_y = ai.minimum; c->max_y = ai.maximum;
        }
        else if(ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0) {
            c->min_y = ai.minimum; c->max_y = ai.maximum;
        }
        else {
            c->min_y = 0; c->max_y = 480;
        }

        lv_indev_t * indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, mt_read_cb);
        lv_indev_set_driver_data(indev, c);

        LV_LOG_USER("MT touch: indev #%d (contact %d) fd=%d cal x[%d,%d] y[%d,%d]",
                    i, i, fd, c->min_x, c->max_x, c->min_y, c->max_y);
        fprintf(stderr, "[MT] indev #%d (contact %d) fd=%d cal x[%d,%d] y[%d,%d]\n",
                i, i, fd, c->min_x, c->max_x, c->min_y, c->max_y);
    }
}

} /* namespace HAL */
