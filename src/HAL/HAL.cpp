/*
 * HAL for eMP-gba (Allwinner T113-S3, 480x480 RGB565 framebuffer).
 *
 * Display + input are driven by LVGL's built-in Linux fbdev + evdev drivers
 * (validated on this board), so no sunxifb/lv_drivers dependency is needed.
 */
#include "HAL.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

extern "C" {
#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/display/fb/lv_linux_fbdev.h"
/* Original single-touch evdev driver (kept for reference / fallback):
 * #include "lvgl/src/drivers/evdev/lv_evdev.h" */
void lv_fs_posix_init(void);
}

void signalExitCallback(int signal, siginfo_t* info, void* uctx);
void install_signal_handler(void);
uint32_t custom_tick_get(void);

void HAL::Init(void)
{
    /* LittlevGL init */
    lv_init();

    /* Register the custom POSIX file system driver (letter '/') */
    lv_fs_posix_init();

    /* Linux frame buffer device init (480x480, 32bpp XRGB8888).
     * force_refresh=false: LVGL flushes only dirty rectangles instead of
     * memcpy-ing the whole fb every frame. */
    lv_display_t * disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
    lv_linux_fbdev_set_force_refresh(disp, false);

    /* Touchscreen via evdev -- multi-touch: one POINTER indev per contact
     * so two fingers can press two virtual keys at once. */
    /* Original single-touch init (kept for reference / fallback):
     * lv_indev_t * indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event1"); */
    HAL::InitMultiTouchInput();

    /* Register exit signal handler */
    install_signal_handler();
}

/**
 * @brief System exit callback.
 *        An atomic_flag guarantees the teardown runs only once, since a signal
 *        can be delivered to any thread and concurrent exit(0) calls would
 *        double-free glibc atexit handlers (random crash).
 */
void signalExitCallback(int signal, siginfo_t* info, void* uctx)
{
    static std::atomic_flag exitFlag = ATOMIC_FLAG_INIT;
    if (exitFlag.test_and_set())
        return;

    /* fprintf (not LV_LOG_USER): visible even at LV_LOG_LEVEL_WARN */
    fprintf(stderr, "[HAL] Got signal %d (si_addr=%p), exiting ...\n", signal,
            info ? info->si_addr : NULL);

    /* Crashing signals: dump pc/lr/fp and walk the ARM frame-pointer chain so
     * the fault can be resolved offline with addr2line (musl has no
     * backtrace(); needs -fno-omit-frame-pointer at build time). */
    if (signal == SIGSEGV || signal == SIGBUS || signal == SIGILL ||
        signal == SIGFPE || signal == SIGTRAP) {
        ucontext_t* uc = (ucontext_t*)uctx;
        if (uc) {
            unsigned long pc = uc->uc_mcontext.arm_pc;
            unsigned long lr = uc->uc_mcontext.arm_lr;
            unsigned long fp = uc->uc_mcontext.arm_fp;
            fprintf(stderr, "[HAL] pc=0x%lx lr=0x%lx fp=0x%lx\n", pc, lr, fp);
            for (int i = 0; i < 20 && fp; i++) {
                unsigned long ret = ((unsigned long*)fp)[1];
                fprintf(stderr, "[HAL]   #%02d ret=0x%lx\n", i, ret);
                fp = ((unsigned long*)fp)[0];
            }
        }
    }

    _exit(1);
}

void install_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signalExitCallback;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGIOT,  &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
}

/* Set in lv_conf.h as LV_TICK_CUSTOM_SYS_TIME_EXPR */
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = ((uint64_t)tv_start.tv_sec * 1000000 + (uint64_t)tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms = ((uint64_t)tv_now.tv_sec * 1000000 + (uint64_t)tv_now.tv_usec) / 1000;

    return (uint32_t)(now_ms - start_ms);
}
