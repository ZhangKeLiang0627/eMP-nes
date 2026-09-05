/*
 * AudioPlayer stand-in for the SimpleNES core.
 *
 * The core APU (include/APU/APU.h) binds:
 *   - player.output_sample_rate  (sample timer period)
 *   - player.audio_queue         (spsc ring it pushes mixed samples into)
 * The real implementation uses miniaudio + a device callback. For the
 * embedded build we keep the same two members but never start an audio
 * device: samples land in the spsc ring and are simply dropped when it
 * fills (spsc::push is a non-blocking tryPush). A dedicated ALSA sink can
 * be attached to audio_queue later without touching the core.
 *
 * This header SHADOWS the submodule's include/AudioPlayer.h because this
 * directory is listed before libs/simplenes/include on the include path.
 */
#pragma once

#include "APU/spsc.hpp"

namespace sn
{
    class AudioPlayer
    {
    public:
        int output_sample_rate;
        spsc::RingBuffer<float> audio_queue;

        explicit AudioPlayer(int /* input_rate */)
            : output_sample_rate(44100)
            , audio_queue(4 * 44100)
        {
        }

        bool start() { return true; }
        void mute() {}
    };
}
