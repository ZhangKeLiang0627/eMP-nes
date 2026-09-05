#pragma once

#include "common_inc.h"

namespace HAL
{
    /**
     * @brief Hardware abstraction layer init for T113-S3 (TinaLinux).
     *        Initializes LVGL, the custom POSIX FS driver, the Linux
     *        framebuffer display (/dev/fb0) and the multi-touch evdev
     *        touchscreen (/dev/input/event1), plus the signal handler.
     */
    void Init(void);

    /**
     * @brief Create one POINTER indev per touch contact on /dev/input/event1
     *        (default 2, see GBA_INPUT_TOUCH_POINTS). Each indev reports a
     *        different MT slot so the on-screen virtual GBA buttons can be
     *        pressed by independent fingers simultaneously.
     */
    void InitMultiTouchInput(void);
}
