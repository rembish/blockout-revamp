/* BlockOut port: SDL2 frontend (native and Emscripten). */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "../core/bo_core.h"
#include "gfx.h"
#include "font.h"
#include "view.h"

extern const unsigned char font_ttf[];
extern const int font_ttf_len;

/* BIOS typematic defaults: 500 ms delay, 10.9 characters per second */
#define REPEAT_DELAY 0.5
#define REPEAT_RATE  (1.0 / 10.9)

static struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    int w, h;
    int running;

    double t0, now;             /* seconds */
    double logic_fps;
    long ticks_done, frames_done;

    bo_game game;
    bo_setup setup;

    /* typematic */
    uint16_t held_key;
    SDL_Keycode held_sym;
    double next_repeat;

    float clear_flash;
} A;

static double seconds(void)
{
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static uint32_t bios_time_of_day(void)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    long s = lt->tm_hour * 3600L + lt->tm_min * 60L + lt->tm_sec;
    return (uint32_t)(s * BO_TICK_HZ);
}

static void start_game(void)
{
    bo_srand(&A.game, (uint16_t)time(NULL));
    bo_init(&A.game, &A.setup, BO_MODE_GAME, 1, bios_time_of_day());
    A.t0 = seconds();
    A.ticks_done = A.frames_done = 0;
}

/* SDL key -> BIOS key word as the original sees it after its translation (5176) */
static uint16_t map_key(const SDL_Keysym *k)
{
    int shift = (k->mod & KMOD_SHIFT) != 0;
    switch (k->sym) {
    case SDLK_UP: case SDLK_KP_8: return BO_K_UP;
    case SDLK_DOWN: case SDLK_KP_2: return BO_K_DOWN;
    case SDLK_LEFT: case SDLK_KP_4: return BO_K_LEFT;
    case SDLK_RIGHT: case SDLK_KP_6: return BO_K_RIGHT;
    case SDLK_HOME: case SDLK_KP_7: return BO_K_HOME;
    case SDLK_PAGEUP: case SDLK_KP_9: return BO_K_PGUP;
    case SDLK_END: case SDLK_KP_1: return BO_K_END;
    case SDLK_PAGEDOWN: case SDLK_KP_3: return BO_K_PGDN;
    case SDLK_SPACE: case SDLK_KP_0: return BO_K_SPACE;
    case SDLK_RETURN: case SDLK_KP_ENTER: return BO_K_ENTER;
    case SDLK_ESCAPE: return BO_K_ESC;
    default:
        if (k->sym >= SDLK_a && k->sym <= SDLK_z) return (uint16_t)(k->sym - SDLK_a + (shift ? 'A' : 'a'));
        return 0;
    }
}

static void key_down(const SDL_Keysym *k)
{
    uint16_t key = map_key(k);
    if (!key) return;
    if (A.game.state == BO_S_DONE) { start_game(); return; }
    bo_key(&A.game, key);
    A.held_key = key;
    A.held_sym = k->sym;
    A.next_repeat = A.now + REPEAT_DELAY;
}

static void key_up(const SDL_Keysym *k)
{
    if (k->sym == A.held_sym) A.held_key = 0;
}

/* Run the core up to the current time: ticks at 18.2 Hz, frames at logic_fps. */
static void advance(void)
{
    double t = A.now - A.t0;
    long frames_due = (long)(t * A.logic_fps);
    if (frames_due - A.frames_done > 30) A.frames_done = frames_due - 30;   /* after a stall */
    while (A.frames_done < frames_due) {
        A.frames_done++;
        double ft = A.frames_done / A.logic_fps;
        long ticks_due = (long)(ft * BO_TICK_HZ);
        while (A.ticks_done < ticks_due) { bo_tick(&A.game); A.ticks_done++; }
        while (A.held_key && A.next_repeat <= A.t0 + ft) {
            bo_key(&A.game, A.held_key);
            A.next_repeat += REPEAT_RATE;
        }
        bo_frame(&A.game);
        if (A.game.ev_cleared) A.clear_flash = 1;
    }
}

static void render(void)
{
    SDL_GetRendererOutputSize(A.ren, &A.w, &A.h);
    SDL_SetRenderDrawColor(A.ren, 0, 0, 0, 255);
    SDL_RenderClear(A.ren);
    view_fx fx = {0};
    fx.time = (float)(A.now - A.t0);
    fx.frac = (float)((A.now - A.t0) * A.logic_fps - A.frames_done);
    if (fx.frac < 0) fx.frac = 0;
    if (fx.frac > 1) fx.frac = 1;
    fx.clear_flash = A.clear_flash;
    switch (A.game.state) {
    case BO_S_PAUSED: fx.message = "PAUSED"; fx.submessage = "press P to continue"; break;
    case BO_S_GAME_OVER: fx.message = "GAME OVER"; fx.submessage = "press ENTER"; break;
    case BO_S_DONE: fx.message = A.game.aborted ? "GAME ABORTED" : "GAME OVER"; fx.submessage = "press any key for a new game"; break;
    }
    view_game(&A.game, A.w, A.h, &fx);
    SDL_RenderPresent(A.ren);
}

static void frame(void)
{
    SDL_Event e;
    A.now = seconds();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: A.running = 0; break;
        case SDL_KEYDOWN:
            if (e.key.repeat) break;             /* we do our own BIOS-style repeat */
            if (e.key.keysym.sym == SDLK_F11 || (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                Uint32 fs = SDL_GetWindowFlags(A.win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(A.win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
            }
            key_down(&e.key.keysym);
            break;
        case SDL_KEYUP: key_up(&e.key.keysym); break;
        }
    }
    advance();
    A.clear_flash *= 0.9f;
    render();
#ifdef __EMSCRIPTEN__
    if (!A.running) emscripten_cancel_main_loop();
#endif
}

/* --shot out.bmp [--frames N] [--keys f:key,...] [--seed S] [--size WxH]: run the game
   headless for N logic frames with scripted keys, save a screenshot and exit. */
static int shot_mode(int argc, char **argv)
{
    const char *out = NULL, *keys = "";
    long frames = 600;
    unsigned seed = 1;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--shot")) out = argv[++i];
        else if (!strcmp(argv[i], "--frames")) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--keys")) keys = argv[++i];
        else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atol(argv[++i]);
        else if (!strcmp(argv[i], "--size")) sscanf(argv[++i], "%dx%d", &A.w, &A.h);
        else if (!strcmp(argv[i], "--setup")) sscanf(argv[++i], "%hd,%hd,%hd,%hd,%hd,%hd", &A.setup.len,
                    &A.setup.wid, &A.setup.dep, &A.setup.blockset, &A.setup.level, &A.setup.rot_speed);
    }
    if (!out) return 0;
    bo_srand(&A.game, (uint16_t)seed);
    bo_init(&A.game, &A.setup, BO_MODE_GAME, 1, seed * 7919u);
    const char *k = keys;
    for (long f = 1; f <= frames; f++) {
        long ticks_due = (long)(f / A.logic_fps * BO_TICK_HZ);
        while (A.ticks_done < ticks_due) { bo_tick(&A.game); A.ticks_done++; }
        while (*k) {
            long kf; unsigned kv; int n;
            if (sscanf(k, "%ld:%x%n", &kf, &kv, &n) != 2 || kf != f) break;
            bo_key(&A.game, (uint16_t)kv);
            k += n; if (*k == ',') k++;
        }
        bo_frame(&A.game);
    }
    A.frames_done = frames;
    A.now = A.t0 = 0;
    render();
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, A.w, A.h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(A.ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, out);
    printf("state=%d score=%ld cubes=%ld level=%d\n", A.game.state, (long)A.game.score,
           (long)A.game.cubes_played, A.game.level);
    return 1;
}

int main(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--size")) sscanf(argv[i + 1], "%dx%d", &A.w, &A.h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    A.win = SDL_CreateWindow("BlockOut", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, A.w ? A.w : 1280, A.h ? A.h : 800,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!A.win) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        A.win = SDL_CreateWindow("BlockOut", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, A.w ? A.w : 1280, A.h ? A.h : 800,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    A.ren = SDL_CreateRenderer(A.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!A.ren) A.ren = SDL_CreateRenderer(A.win, -1, 0);
    if (!A.win || !A.ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(A.ren, SDL_BLENDMODE_BLEND);
    gfx_init(A.ren);
    font_init(A.ren, font_ttf, font_ttf_len);

    A.logic_fps = 60;
    A.setup = (bo_setup){bo_default_setup[0], bo_default_setup[1], bo_default_setup[2],
                         bo_default_setup[3], bo_default_setup[4], bo_default_setup[5]};
    if (shot_mode(argc, argv)) return 0;
    start_game();
    A.running = 1;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (A.running) frame();
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
#endif
    return 0;
}
