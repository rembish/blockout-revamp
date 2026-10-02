/*
 * The original drives the PC speaker directly (8be5 and friends). Its delays are busy
 * loops in calibrated units; the hall-of-fame tune only comes out in tune (B4 A4 F#4 E4)
 * with a unit of ~7.6 us, so that is used throughout. Frequencies are taken verbatim.
 */
#include "audio.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE             44100
#define UNIT             7.58e-6 /* seconds per delay unit */
#define SETFREQ_OVERHEAD 40e-6   /* cost of reprogramming the PIT (a long division) */
#define MAXS             (RATE * 3)

static SDL_AudioDeviceID dev;
static float volume = 0.22f;

static float buf[MAXS];
static int n;
static double phase; /* 0..1 of the current square wave */

static void emit_tone(double freq, double secs)
{
    int k = (int)(secs * RATE);
    for (int i = 0; i < k && n < MAXS; i++) {
        phase += freq / RATE;
        phase -= floor(phase);
        buf[n++] = phase < 0.5 ? 1.f : -1.f;
    }
}

static void emit_level(int on, double secs)
{
    int k = (int)(secs * RATE + 0.5);
    for (int i = 0; i < k && n < MAXS; i++) buf[n++] = on ? 1.f : -1.f;
}

static void tone(double f, int units) { emit_tone(f, units * UNIT + SETFREQ_OVERHEAD); }

/* 8d72: warble around a centre frequency */
static void warble(int centre, int range, int step, int dur, int reps)
{
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i * step < range * 2; i++) tone(i * step + (centre - range), dur);
        for (int i = 0; i * step < range * 2; i++) tone((centre + range) - i * step, dur);
    }
}

/* 8d47: falling sweep */
static void sweep(int start, int end, int step, int dur)
{
    for (int f = start; f >= end; f -= step) tone(f, dur);
}

/* 8ee4: a note made by toggling the speaker with a duty cycle sweeping 1..97 and back */
static void pwm_note(int period, int reps)
{
    int list[32], k = 0;
    for (int d = 1; d <= 100; d += 4) list[k++] = d;
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < k; i++) {
            emit_level(1, list[i] * UNIT);
            emit_level(0, (period - list[i]) * UNIT);
        }
        for (int i = k - 1; i >= 0; i--) {
            emit_level(1, list[i] * UNIT);
            emit_level(0, (period - list[i]) * UNIT);
        }
    }
}

static void synth(int snd)
{
    n = 0;
    switch (snd) {
    case 0: /* 8f90: pit cleared */
        for (int i = 0; i < 5; i++) warble(6000 - 600 * i, 800, 800, 3000, 1);
        break;
    case 1: sweep(0xa8c, 0x32, 0xdc, 600); break;   /* 8fc3: layer cleared */
    case 2: warble(3000, 0x8fc, 0x1e, 9, 2); break; /* 8f47: level up */
    case 3: {                                       /* 9037: hall of fame tune */
        static const int notes[8][2] = { { 0x109, 1 }, { 300, 1 },   { 0x168, 1 }, { 300, 1 },
                                         { 0x168, 1 }, { 0x19a, 2 }, { 0x168, 1 }, { 300, 4 } };
        for (int i = 0; i < 8; i++) pwm_note(notes[i][0], notes[i][1]);
        break;
    }
    case 100: /* menu click */ emit_tone(1800, 0.012); break;
    default: break;
    }
}

/* soften the raw square wave a little: one-pole low-pass + fade edges */
static void finish(void)
{
    if (n <= 0) return;
    float y = 0, a = 1.f - expf(-2.f * (float)M_PI * 5000.f / RATE);
    for (int i = 0; i < n; i++) {
        y += a * (buf[i] - y);
        buf[i] = y * volume;
    }
    int f = n < 200 ? n / 2 : 100;
    for (int i = 0; i < f; i++) {
        buf[i] *= (float)i / f;
        buf[n - 1 - i] *= (float)i / f;
    }
}

void audio_init(void)
{
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

void audio_resume(void)
{
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

void audio_set_volume(float v) { volume = v; }

static void play(int snd)
{
    if (!dev) return;
    synth(snd);
    finish();
    SDL_ClearQueuedAudio(dev); /* the speaker plays one thing at a time */
    SDL_QueueAudio(dev, buf, (Uint32)(n * sizeof(float)));
}

void audio_play(int snd) { play(snd); }
void audio_click(void)
{
    if (dev && SDL_GetQueuedAudioSize(dev) == 0) play(100);
}

float audio_length(int snd)
{
    synth(snd);
    return (float)n / RATE;
}
