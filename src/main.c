/* BlockOut port: SDL2 frontend (native and Emscripten). */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "../core/bo_core.h"
#include "audio.h"
#include "font.h"
#include "gfx.h"
#include "hof.h"
#include "store.h"
#include "touch.h"
#include "ui.h"
#include "view.h"

extern const unsigned char font_ttf[];
extern const int font_ttf_len;

/* BIOS typematic defaults: 500 ms delay, 10.9 characters per second */
#define REPEAT_DELAY 0.5
#define REPEAT_RATE  (1.0 / 10.9)
#define IDLE_DEMO    60.0 /* menus start the demo after 0x444 ticks */
#define REPO_URL     "https://github.com/rembish/blockout-revamp"

#ifdef __EMSCRIPTEN__
/* clang-format off */
EM_JS(int, js_coarse_pointer, (void), {
    return (window.matchMedia && matchMedia('(pointer: coarse)').matches) ? 1 : 0;
});
EM_JS(int, js_prompt_name, (char *out, int max), {
    var s = window.prompt('You made it into the Hall of Fame! Your name:', '') || '';
    s = s.replace(/[^ -~]/g, '').slice(0, max);
    stringToUTF8(s, out, max + 1);
    return s.length;
});
/* clang-format on */
#endif

enum scr { S_MAIN, S_CHOOSE, S_CHANGE, S_PIT, S_LEVEL, S_QUIT, S_GAME, S_HOF, S_HELP };
enum hof_mode { HOF_VIEW, HOF_ENTRY };

static const char *set_names[3] = { "FLAT", "BASIC", "EXTENDED" };
static const char *speed_names[3] = { "SLOW", "MEDIUM", "FAST" };
static const char *preset_names[4] = { "Flat Fun", "3-D Mania", "Out of Control", "Do-It-Yourself" };
static const int pacing_options[4] = { 60, 30, 20, 15 };

static struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    int w, h;
    int running;
    double now;

    int scr;
    ui_menu menu;
    double idle_since;
    char toast[80];
    double toast_until;

    /* game timing */
    double t0;
    double logic_fps;
    long ticks_done, frames_done;
    bo_game game;
    bo_setup setup;
    int practice, demo;
    float clear_flash;

    /* touch */
    int touch;
    int64_t held_finger;

    /* typematic */
    uint16_t held_key;
    SDL_Keycode held_sym;
    double next_repeat;

    /* hall of fame */
    hof_table hof;
    int hof_mode, hof_row;
    char name[HOF_NAME + 1];
    int last_game_valid;
    bo_setup hi_key; /* cache for the panel's high score */
    int32_t hi_value;
    int hi_valid;
} A;

static int32_t cached_best(const bo_setup *s)
{
    if (!A.hi_valid || memcmp(&A.hi_key, s, sizeof *s) != 0) {
        A.hi_key = *s;
        A.hi_value = hof_best(s);
        A.hi_valid = 1;
    }
    return A.hi_value;
}

/* ---- persistence ------------------------------------------------------- */

/* BLOCKOUT.SET: "05-26-89\0", a word, then the setup words (len, wid, dep, set, level,
   rotation speed, ...), 32 bytes in all. Port options live in blockout.cfg. */
static void load_setup(void)
{
    unsigned char b[32];
    A.setup = (bo_setup){ bo_default_setup[0], bo_default_setup[1], bo_default_setup[2],
                          bo_default_setup[3], bo_default_setup[4], bo_default_setup[5] };
    if (store_read("blockout.set", b, 32) >= 23 && !memcmp(b, "05-26-89", 9)) {
        int16_t v[6];
        for (int i = 0; i < 6; i++) v[i] = (int16_t)(b[11 + 2 * i] | b[12 + 2 * i] << 8);
        int ok = 1;
        for (int i = 0; i < 3; i++) ok &= v[i] >= bo_pit_min[i] && v[i] <= bo_pit_max[i];
        ok &= v[3] >= 0 && v[3] < 3 && v[4] >= 0 && v[4] < 10 && v[5] >= 0 && v[5] < 3;
        if (ok) A.setup = (bo_setup){ v[0], v[1], v[2], v[3], v[4], v[5] };
    }
    char cfg[64] = { 0 };
    int fps = 60;
    if (store_read("blockout.cfg", cfg, 63) > 0) sscanf(cfg, "fps=%d", &fps);
    A.logic_fps = fps >= 10 && fps <= 70 ? fps : 60;
}

static int save_setup(void)
{
    unsigned char b[32] = "05-26-89";
    const int16_t v[10] = { A.setup.len,
                            A.setup.wid,
                            A.setup.dep,
                            A.setup.blockset,
                            A.setup.level,
                            A.setup.rot_speed,
                            bo_rot_steps[1][A.setup.rot_speed],
                            bo_move_steps[1][A.setup.rot_speed],
                            2,
                            0 };
    b[9] = 1;
    b[10] = 0;
    for (int i = 0; i < 10; i++) {
        b[11 + 2 * i] = (unsigned char)v[i];
        b[12 + 2 * i] = (unsigned char)(v[i] >> 8);
    }
    b[31] = 1;
    char cfg[64];
    int n = snprintf(cfg, sizeof cfg, "fps=%d\n", (int)A.logic_fps);
    return store_write("blockout.set", b, 32) == 32 && store_write("blockout.cfg", cfg, n) == n;
}

static void toast(const char *msg)
{
    snprintf(A.toast, sizeof A.toast, "%s", msg);
    A.toast_until = A.now + 2.0;
}

/* ---- timing helpers ----------------------------------------------------- */

static double seconds(void)
{
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static uint32_t bios_time_of_day(void)
{
    time_t t = time(NULL);
    const struct tm *lt = localtime(&t);
    long s = lt->tm_hour * 3600L + lt->tm_min * 60L + lt->tm_sec;
    return (uint32_t)(s * BO_TICK_HZ);
}

static int preset_index(const bo_setup *s) /* 3dca */
{
    for (int i = 0; i < 3; i++)
        if (s->len == bo_presets[i][0] && s->wid == bo_presets[i][1] && s->dep == bo_presets[i][2] &&
            s->blockset == bo_presets[i][3])
            return i;
    return 3;
}

/* ---- game start / end --------------------------------------------------- */

static void start_game(int mode)
{
    static int seeded;
    if (!seeded) {
        bo_srand(&A.game, (uint16_t)time(NULL));
        seeded = 1;
    } /* once, like main() */
    for (int i = 0; i < 4; i++) A.game.sound_len[i] = (int)ceilf(audio_length(i) * (float)BO_TICK_HZ);
    A.practice = mode == BO_MODE_PRACTICE;
    A.demo = mode == BO_MODE_DEMO;
    bo_init(&A.game, &A.setup, mode, 1, bios_time_of_day());
    A.game.hiscore = cached_best(&A.setup);
    A.t0 = A.now;
    A.ticks_done = A.frames_done = 0;
    A.held_key = 0;
    touch_release_all();
    A.scr = S_GAME;
    A.last_game_valid = 1;
}

static void enter_menu(int scr)
{
    A.scr = scr;
    A.menu.sel = 0;
    A.idle_since = A.now;
    SDL_StopTextInput();
}

static void game_finished(void)
{
    if (A.demo) { /* st_demo: a finished demo starts another */
        if (A.game.aborted)
            enter_menu(S_MAIN);
        else
            start_game(BO_MODE_DEMO);
        return;
    }
    if (A.practice) {
        enter_menu(S_MAIN);
        return;
    } /* no hall of fame */
    hof_load(&A.setup, &A.hof);
    A.hof_row = hof_insert(&A.hof, A.game.score);
    if (A.hof_row >= 0) {
        A.hof_mode = HOF_ENTRY;
        A.name[0] = 0;
        audio_play(BO_SND_TUNE);
        SDL_StartTextInput();
#ifdef __EMSCRIPTEN__
        if (A.touch) { /* no physical keyboard: ask the browser */
            js_prompt_name(A.name, HOF_NAME);
            hof_set_name(&A.hof, A.hof_row, A.name);
            if (!hof_save(&A.hof)) toast("Could not save the hall of fame");
            A.hi_valid = 0;
            A.hof_mode = HOF_VIEW;
            SDL_StopTextInput();
        }
#endif
    } else {
        A.hof_mode = HOF_VIEW;
    }
    A.scr = S_HOF;
    A.menu.sel = 0;
    A.idle_since = A.now;
}

/* ---- menus ---------------------------------------------------------------- */

static int16_t *setup_dim(int axis)
{
    return axis == 0 ? &A.setup.len : axis == 1 ? &A.setup.wid : &A.setup.dep;
}

static void build_menu(void)
{
    ui_menu *m = &A.menu;
    char dims[32];
    snprintf(dims, sizeof dims, "%dx%dx%d", A.setup.len, A.setup.wid, A.setup.dep);
    switch (A.scr) {
    case S_MAIN: {
        ui_menu_clear(m, "MAIN MENU");
        int p = preset_index(&A.setup);
        ui_item *it = ui_add(m, 'S', "Start Game");
        if (p == 3)
            snprintf(it->value, sizeof it->value, "%s %s", dims, set_names[A.setup.blockset]);
        else
            snprintf(it->value, sizeof it->value, "%s", preset_names[p]);
        ui_add(m, 'C', "Choose Setup");
        ui_add(m, 'W', "Write Setup");
        ui_add(m, 'P', "Practice Mode");
        ui_add(m, 'D', "Demo");
        ui_add(m, 'H', "Help");
        ui_add(m, 'F', "Hall of Fame");
        ui_add(m, 'G', "Source Code");
#ifndef __EMSCRIPTEN__
        ui_add(m, 'Q', "Quit Game");
#endif
        break;
    }
    case S_CHOOSE:
        ui_menu_clear(m, "CHOOSE SETUP");
        for (int i = 0; i < 3; i++) {
            ui_item *it = ui_add(m, "F3O"[i], "%s", preset_names[i]);
            snprintf(it->value, sizeof it->value, "%dx%dx%d %s", bo_presets[i][0], bo_presets[i][1],
                     bo_presets[i][2], set_names[bo_presets[i][3]]);
        }
        ui_add(m, 'C', "Change Setup");
        ui_add(m, 'M', "Main Menu");
        break;
    case S_CHANGE: {
        ui_menu_clear(m, "CHANGE SETUP");
        snprintf(ui_add(m, 'P', "Pit Dimensions")->value, 40, "%s", dims);
        ui_item *it = ui_add(m, 'B', "Block Set");
        snprintf(it->value, 40, "%s", set_names[A.setup.blockset]);
        it->adjustable = 1;
        it = ui_add(m, 'R', "Rotation Speed");
        snprintf(it->value, 40, "%s", speed_names[A.setup.rot_speed]);
        it->adjustable = 1;
        it = ui_add(m, 'F', "Frame Pacing");
        snprintf(it->value, 40, "%d Hz", (int)A.logic_fps);
        it->adjustable = 1;
        ui_add(m, 'S', "Start Game");
        ui_add(m, 'W', "Write Setup");
        ui_add(m, 'M', "Main Menu");
        break;
    }
    case S_PIT: {
        ui_menu_clear(m, "PIT DIMENSIONS");
        static const char *names[3] = { "Length", "Width", "Depth" };
        for (int i = 0; i < 3; i++) {
            ui_item *it = ui_add(m, "LWD"[i], "%s", names[i]);
            snprintf(it->value, 40, "%d", *setup_dim(i));
            it->adjustable = 1;
        }
        ui_add(m, 'E', "Exit");
        break;
    }
    case S_LEVEL:
        ui_menu_clear(m, "STARTING LEVEL");
        for (int i = 0; i < 10; i++) ui_add(m, (char)('0' + i), "Level %d", i);
        break;
    case S_QUIT:
        ui_menu_clear(m, "QUIT GAME?");
        ui_add(m, 'Y', "Yes");
        ui_add(m, 'N', "No");
        break;
    case S_HOF:
        ui_menu_clear(m, "");
        ui_add(m, 'S', "Start Game");
        ui_add(m, 'C', "Change Setup");
        ui_add(m, 'M', "Main Menu");
#ifndef __EMSCRIPTEN__
        ui_add(m, 'Q', "Quit Game");
#endif
        break;
    default: ui_menu_clear(m, "");
    }
}

static void cycle(int16_t *v, int lo, int hi, int dir) /* 059b: wraps around */
{
    *v = (int16_t)(*v + dir);
    if (*v > hi) *v = (int16_t)lo;
    if (*v < lo) *v = (int16_t)hi;
}

static void open_level_picker(void)
{
    enter_menu(S_LEVEL);
    A.menu.sel = A.setup.level;
}

static void adjust(int i, int dir)
{
    switch (A.scr) {
    case S_CHANGE:
        if (i == 1)
            cycle(&A.setup.blockset, 0, 2, dir);
        else if (i == 2)
            cycle(&A.setup.rot_speed, 0, 2, dir);
        else if (i == 3) {
            int k = 0;
            while (k < 3 && pacing_options[k] != (int)A.logic_fps) k++;
            k = (k + (dir > 0 ? 1 : 3)) % 4;
            A.logic_fps = pacing_options[k];
        }
        break;
    case S_PIT:
        if (i < 3) cycle(setup_dim(i), bo_pit_min[i], bo_pit_max[i], dir);
        break;
    default: break;
    }
}

static void activate(int i)
{
    audio_click();
    switch (A.scr) {
    case S_MAIN:
        if (i == 0)
            open_level_picker();
        else if (i == 1)
            enter_menu(S_CHOOSE);
        else if (i == 2)
            toast(save_setup() ? "Setup written" : "Could not write setup");
        else if (i == 3)
            start_game(BO_MODE_PRACTICE);
        else if (i == 4)
            start_game(BO_MODE_DEMO);
        else if (i == 5)
            enter_menu(S_HELP);
        else if (i == 7) {
            if (SDL_OpenURL(REPO_URL) != 0) toast(REPO_URL);
        } else if (i == 6) {
            hof_load(&A.setup, &A.hof);
            A.hof_mode = HOF_VIEW;
            A.hof_row = -1;
            A.last_game_valid = 0;
            enter_menu(S_HOF);
        } else
            enter_menu(S_QUIT);
        break;
    case S_CHOOSE:
        if (i < 3) { /* 8907: pick a preset and play */
            A.setup.len = bo_presets[i][0];
            A.setup.wid = bo_presets[i][1];
            A.setup.dep = bo_presets[i][2];
            A.setup.blockset = bo_presets[i][3];
            open_level_picker();
        } else if (i == 3)
            enter_menu(S_CHANGE);
        else
            enter_menu(S_MAIN);
        break;
    case S_CHANGE:
        if (i == 0)
            enter_menu(S_PIT);
        else if (i <= 3)
            adjust(i, 1);
        else if (i == 4)
            open_level_picker();
        else if (i == 5)
            toast(save_setup() ? "Setup written" : "Could not write setup");
        else
            enter_menu(S_MAIN);
        break;
    case S_PIT:
        if (i < 3)
            adjust(i, 1);
        else
            enter_menu(S_CHANGE);
        break;
    case S_LEVEL:
        A.setup.level = (int16_t)i;
        start_game(BO_MODE_GAME);
        break;
    case S_QUIT:
        if (i == 0)
            A.running = 0;
        else
            enter_menu(S_MAIN);
        break;
    case S_HOF:
        if (i == 0)
            open_level_picker();
        else if (i == 1)
            enter_menu(S_CHANGE);
        else if (i == 2)
            enter_menu(S_MAIN);
        else
            enter_menu(S_QUIT);
        break;
    default: break;
    }
}

static void back(void)
{
    switch (A.scr) {
    case S_MAIN:
#ifndef __EMSCRIPTEN__
        enter_menu(S_QUIT);
#endif
        break;
    case S_PIT: enter_menu(S_CHANGE); break;
    default: enter_menu(S_MAIN); break;
    }
}

static void menu_key(const SDL_Keysym *k)
{
    ui_menu *m = &A.menu;
    A.idle_since = A.now;
    if (A.scr == S_HELP) {
        enter_menu(S_MAIN);
        return;
    }
    if (A.scr == S_HOF && A.hof_mode == HOF_ENTRY) {
        int n = (int)strlen(A.name);
        if (k->sym == SDLK_BACKSPACE && n)
            A.name[n - 1] = 0;
        else if (k->sym == SDLK_RETURN || k->sym == SDLK_KP_ENTER) {
            hof_set_name(&A.hof, A.hof_row, A.name);
            if (!hof_save(&A.hof)) toast("Could not save the hall of fame");
            A.hi_valid = 0;
            A.hof_mode = HOF_VIEW;
            SDL_StopTextInput();
        } else if (k->sym == SDLK_ESCAPE) { /* 3baf: a cancelled entry is discarded */
            hof_load(&A.setup, &A.hof);
            A.hof_row = -1;
            A.hof_mode = HOF_VIEW;
            SDL_StopTextInput();
        }
        return;
    }
    build_menu();
    switch (k->sym) {
    case SDLK_UP:
    case SDLK_KP_8: m->sel = (m->sel + m->n - 1) % m->n; break;
    case SDLK_DOWN:
    case SDLK_KP_2: m->sel = (m->sel + 1) % m->n; break;
    case SDLK_LEFT:
    case SDLK_KP_4:
        if (m->items[m->sel].adjustable) adjust(m->sel, -1);
        break;
    case SDLK_RIGHT:
    case SDLK_KP_6:
        if (m->items[m->sel].adjustable) adjust(m->sel, 1);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: activate(m->sel); break;
    case SDLK_ESCAPE: back(); break;
    default: {
        int c = (int)k->sym;
        if (c >= 'a' && c <= 'z') c -= 32;
        for (int i = 0; i < m->n; i++)
            if (m->items[i].hotkey == c) {
                m->sel = i;
                activate(i);
                break;
            }
    }
    }
}

static void text_input(const char *t)
{
    if (!(A.scr == S_HOF && A.hof_mode == HOF_ENTRY)) return;
    for (; *t; t++) {
        int n = (int)strlen(A.name);
        if (n < HOF_NAME && *t >= 32 && *t < 127) {
            A.name[n] = *t;
            A.name[n + 1] = 0;
        }
    }
}

/* ---- game input ------------------------------------------------------- */

/* SDL key -> BIOS key word as the original sees it after its translation (5176) */
static uint16_t map_key(const SDL_Keysym *k)
{
    int shift = (k->mod & KMOD_SHIFT) != 0;
    switch (k->sym) {
    case SDLK_UP:
    case SDLK_KP_8: return BO_K_UP;
    case SDLK_DOWN:
    case SDLK_KP_2: return BO_K_DOWN;
    case SDLK_LEFT:
    case SDLK_KP_4: return BO_K_LEFT;
    case SDLK_RIGHT:
    case SDLK_KP_6: return BO_K_RIGHT;
    case SDLK_HOME:
    case SDLK_KP_7: return BO_K_HOME;
    case SDLK_PAGEUP:
    case SDLK_KP_9: return BO_K_PGUP;
    case SDLK_END:
    case SDLK_KP_1: return BO_K_END;
    case SDLK_PAGEDOWN:
    case SDLK_KP_3: return BO_K_PGDN;
    case SDLK_SPACE:
    case SDLK_KP_0: return BO_K_SPACE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return BO_K_ENTER;
    case SDLK_ESCAPE: return BO_K_ESC;
    default:
        if (k->sym >= SDLK_a && k->sym <= SDLK_z) return (uint16_t)(k->sym - SDLK_a + (shift ? 'A' : 'a'));
        return 0;
    }
}

static void game_key_down(const SDL_Keysym *k)
{
    if (A.demo) {
        A.game.aborted = 1;
        A.game.state = BO_S_DONE;
        return;
    } /* any key ends it */
    uint16_t key = map_key(k);
    if (!key) return;
    bo_key(&A.game, key);
    A.held_key = key;
    A.held_sym = k->sym;
    A.next_repeat = A.now + REPEAT_DELAY;
}

/* Run the core up to the current time: ticks at 18.2 Hz, frames at logic_fps. */
static void advance_game(void)
{
    double t = A.now - A.t0;
    long frames_due = (long)(t * A.logic_fps);
    if (frames_due - A.frames_done > 30) { /* after a stall, don't fast-forward */
        long skip = frames_due - 30 - A.frames_done;
        A.t0 += skip / A.logic_fps;
        frames_due -= skip;
        A.ticks_done = (long)(A.frames_done / A.logic_fps * BO_TICK_HZ);
    }
    while (A.frames_done < frames_due && A.game.state != BO_S_DONE) {
        A.frames_done++;
        double ft = A.frames_done / A.logic_fps;
        long ticks_due = (long)(ft * BO_TICK_HZ);
        while (A.ticks_done < ticks_due) {
            bo_tick(&A.game);
            A.ticks_done++;
        }
        while (A.held_key && A.next_repeat <= A.t0 + ft) {
            bo_key(&A.game, A.held_key);
            A.next_repeat += REPEAT_RATE;
        }
        bo_frame(&A.game);
        if (A.game.ev_cleared) A.clear_flash = 1;
        if (A.game.ev_sound >= 0) audio_play(A.game.ev_sound);
    }
    if (A.game.state == BO_S_DONE) game_finished();
}

/* ---- rendering ------------------------------------------------------------- */

static void draw_hof(box pit, float t)
{
    float s = pit.w * 0.034f;
    float w = pit.w * 0.94f, h = s * 17.2f;
    box b = { pit.x + (pit.w - w) / 2, pit.y + pit.h * 0.025f, w, h };
    gfx_round_rect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, s * 0.6f, rgb_hex(0x2bd98a, 0.6f));
    gfx_round_rect(b.x, b.y, b.w, b.h, s * 0.6f, rgb_hex(0x0c1430, 1));
    float y = b.y + s * 0.7f;
    font_draw(b.x + w / 2, y, s * 1.3f, rgb_hex(0xffb347, 1), ALIGN_CENTER, "HALL OF FAME");
    y += s * 1.9f;
    int a = A.hof.len < A.hof.wid ? A.hof.len : A.hof.wid, c2 = A.hof.len < A.hof.wid ? A.hof.wid : A.hof.len;
    font_drawf(b.x + w / 2, y, s * 0.75f, rgb_hex(0x7f9cd9, 1), ALIGN_CENTER, "Pit %dx%dx%d   Block Set %s",
               a, c2, A.hof.dep, set_names[A.hof.blockset]);
    y += s * 1.6f;
    for (int i = 0; i < HOF_MAX; i++, y += s * 1.3f) {
        int editing = A.hof_mode == HOF_ENTRY && i == A.hof_row;
        rgba c = i == A.hof_row ? rgb_hex(0x5ef2b0, 1) : rgb_hex(0xc9d6f5, 1);
        font_drawf(b.x + s * 2.2f, y, s, c, ALIGN_RIGHT, "%d.", i + 1);
        if (i < A.hof.count) {
            char nm[HOF_NAME + 2];
            if (editing)
                snprintf(nm, sizeof nm, "%s%s", A.name, fmodf(t, 1.f) < 0.5f ? "_" : " ");
            else
                snprintf(nm, sizeof nm, "%s", A.hof.e[i].name);
            font_draw(b.x + s * 2.8f, y, s, c, ALIGN_LEFT, nm);
            font_drawf(b.x + w * 0.70f, y, s, c, ALIGN_RIGHT, "%ld", (long)(A.hof.e[i].score % 1000000));
            font_drawf(b.x + w - s * 0.8f, y, s * 0.8f, rgba_alpha(c, 0.7f), ALIGN_RIGHT, "%s",
                       A.hof.e[i].date);
        } else {
            font_draw(b.x + s * 2.8f, y, s, rgb_hex(0x4a5a80, 1), ALIGN_LEFT, "..........");
        }
    }
    if (A.hof_mode == HOF_ENTRY)
        font_draw(b.x + w / 2, b.y + h - s * 1.5f, s * 0.8f, rgb_hex(0x5ef2b0, 1), ALIGN_CENTER,
                  "You made it! Type your name and press ENTER");
}

static void render(void)
{
    SDL_GetRendererOutputSize(A.ren, &A.w, &A.h);
    SDL_SetRenderDrawColor(A.ren, 0, 0, 0, 255);
    SDL_RenderClear(A.ren);
    view_fx fx = { 0 };
    float t = (float)A.now;
    fx.time = t;
    box pit, gauge, panel;
    view_pit_box(A.w, A.h, &pit, &gauge, &panel);

    if (A.scr == S_GAME) {
        fx.frac = (float)((A.now - A.t0) * A.logic_fps - A.frames_done);
        if (fx.frac < 0) fx.frac = 0;
        if (fx.frac > 1) fx.frac = 1;
        fx.clear_flash = A.clear_flash;
        if (A.game.state == BO_S_PAUSED) {
            fx.message = "PAUSED";
            fx.submessage = "press P to continue";
        } else if (A.game.state == BO_S_GAME_OVER) {
            fx.message = "GAME OVER";
            fx.submessage = A.demo ? NULL : "press ENTER";
        }
        view_game(&A.game, A.w, A.h, &fx);
        if (A.demo)
            font_draw(pit.x + pit.w / 2, pit.y + pit.h * 0.92f, pit.w * 0.04f,
                      rgb_hex(0xffb347, 0.6f + 0.4f * sinf(t * 3)), ALIGN_CENTER, "DEMO  -  press any key");
        if (A.practice)
            font_draw(pit.x + pit.w / 2, pit.y + pit.h * 0.02f, pit.w * 0.03f, rgb_hex(0xffb347, 0.8f),
                      ALIGN_CENTER, "PRACTICE  -  SPACE drops, no gravity");
    } else {
        /* menus: the last game (or an empty pit of the current setup) behind a dim layer */
        static bo_game preview;
        const bo_game *bg = &preview;
        if (A.last_game_valid && A.scr == S_HOF)
            bg = &A.game;
        else {
            memset(&preview, 0, sizeof preview);
            preview.setup = A.setup;
            preview.level = A.setup.level;
            preview.state = BO_S_DONE;
            preview.hiscore = cached_best(&A.setup);
        }
        view_game(bg, A.w, A.h, &fx);
        ui_dim(pit, 0.55f);
        if (A.scr == S_HELP) {
            static const char *lines[] = {
                "Arrow keys\tmove the block",
                "Home End PgUp PgDn\tmove diagonally",
                "Q W E\trotate counter-clockwise",
                "A S D\trotate clockwise",
                "SPACE\tdrop the block",
                "P\tpause / resume",
                "O\tsound off / on",
                "ESC\tabort game",
                "F11\tfullscreen",
                "",
                "press any key",
            };
            ui_text_panel(pit, "HELP", lines, (int)(sizeof lines / sizeof *lines), t);
        } else if (A.scr == S_HOF) {
            draw_hof(pit, t);
            if (A.hof_mode == HOF_VIEW) {
                build_menu();
                box mb = { pit.x + pit.w * 0.15f, pit.y + pit.h * 0.64f, pit.w * 0.7f, pit.h * 0.34f };
                ui_menu_draw(&A.menu, mb, t);
            }
        } else {
            build_menu();
            ui_menu_draw(&A.menu, pit, t);
        }
        gfx_flush();
    }
    {
        view_layout_t L;
        view_layout(A.w, A.h, &L);
        int scr = !A.touch          ? TOUCH_NONE
                  : A.scr == S_GAME ? (A.demo ? TOUCH_NONE : TOUCH_GAME)
                                    : (A.scr == S_MAIN ? TOUCH_NONE : TOUCH_MENU);
        touch_layout(&L, scr);
        touch_draw(t);
    }
    if (A.toast_until > A.now) {
        float s = pit.w * 0.04f;
        float tw = font_width(s, A.toast) + s * 2;
        gfx_round_rect(pit.x + (pit.w - tw) / 2, pit.y + pit.h - s * 2.6f, tw, s * 1.8f, s * 0.4f,
                       rgb_hex(0x2bd98a, 0.95f));
        font_draw(pit.x + pit.w / 2, pit.y + pit.h - s * 2.2f, s, rgb_hex(0x04101a, 1), ALIGN_CENTER,
                  A.toast);
        gfx_flush();
    }
    SDL_RenderPresent(A.ren);
}

/* ---- main loop -------------------------------------------------------- */

static void set_touch(int on)
{
    A.touch = on;
    view_set_touch(on);
}

static void finger_down(float nx, float ny, int64_t finger)
{
    if (!A.touch) set_touch(1);
    audio_resume();
    uint16_t key = touch_press(nx * (float)A.w, ny * (float)A.h, finger);
    A.idle_since = A.now;
    if (A.scr == S_GAME) {
        if (A.demo) {
            A.game.aborted = 1;
            A.game.state = BO_S_DONE;
            return;
        }
        if (!key) { /* a tap anywhere answers "press ENTER" / resumes a pause */
            if (A.game.state == BO_S_GAME_OVER)
                bo_key(&A.game, BO_K_ENTER);
            else if (A.game.state == BO_S_PAUSED)
                bo_key(&A.game, 'p');
            return;
        }
        bo_key(&A.game, key);
        A.held_key = key;
        A.held_sym = SDLK_UNKNOWN;
        A.held_finger = finger;
        A.next_repeat = A.now + REPEAT_DELAY;
        return;
    }
    if (key == BO_K_ESC) {
        touch_release(finger);
        if (A.scr == S_HOF && A.hof_mode == HOF_ENTRY) return;
        back();
    }
}

static void finger_up(int64_t finger)
{
    uint16_t key = touch_release(finger);
    if (key && key == A.held_key && finger == A.held_finger) A.held_key = 0;
}

static void mouse(int x, int y, int click)
{
    if (A.scr == S_GAME) {
        if (click && A.demo) {
            A.game.aborted = 1;
            A.game.state = BO_S_DONE;
        }
        return;
    }
    if (A.scr == S_HELP) {
        if (click) enter_menu(S_MAIN);
        return;
    }
    if (A.scr == S_HOF && A.hof_mode == HOF_ENTRY) return;
    int ww, wh;
    SDL_GetWindowSize(A.win, &ww, &wh);
    float fx = (float)x * A.w / ww, fy = (float)y * A.h / wh;
    int i = ui_hit(&A.menu, fx, fy);
    if (i < 0) return;
    if (A.menu.sel != i) A.idle_since = A.now;
    A.menu.sel = i;
    if (click) {
        A.idle_since = A.now;
        activate(i);
    }
}

static void frame(void)
{
    SDL_Event e;
    A.now = seconds();
#ifdef __EMSCRIPTEN__
    { /* follow the canvas' CSS size */
        double cw, ch;
        int ww, wh;
        emscripten_get_element_css_size("#canvas", &cw, &ch);
        SDL_GetWindowSize(A.win, &ww, &wh);
        if ((int)cw != ww || (int)ch != wh) SDL_SetWindowSize(A.win, (int)cw, (int)ch);
    }
#endif
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: A.running = 0; break;
        case SDL_KEYDOWN:
            audio_resume();
            if (e.key.repeat && A.scr == S_GAME) break; /* the game uses BIOS-style repeat */
            if (e.key.keysym.sym == SDLK_F11 ||
                (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                Uint32 fs = SDL_GetWindowFlags(A.win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(A.win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
            }
            if (A.scr == S_GAME)
                game_key_down(&e.key.keysym);
            else
                menu_key(&e.key.keysym);
            break;
        case SDL_KEYUP:
            if (e.key.keysym.sym == A.held_sym && A.held_sym != SDLK_UNKNOWN) A.held_key = 0;
            break;
        case SDL_TEXTINPUT: text_input(e.text.text); break;
        case SDL_FINGERDOWN: finger_down(e.tfinger.x, e.tfinger.y, (int64_t)e.tfinger.fingerId); break;
        case SDL_FINGERUP: finger_up((int64_t)e.tfinger.fingerId); break;
        case SDL_MOUSEMOTION: mouse(e.motion.x, e.motion.y, 0); break;
        case SDL_MOUSEBUTTONDOWN:
            audio_resume();
            mouse(e.button.x, e.button.y, 1);
            break;
        default: break;
        }
    }
    if (A.scr == S_GAME)
        advance_game();
    else if (A.now - A.idle_since > IDLE_DEMO && !(A.scr == S_HOF && A.hof_mode == HOF_ENTRY))
        start_game(BO_MODE_DEMO); /* every menu has the idle flag set */
    A.clear_flash *= 0.9f;
    render();
#ifdef __EMSCRIPTEN__
    if (!A.running) emscripten_cancel_main_loop();
#endif
}

/* --shot out.bmp [--screen game|demo|main|hof|help|change] [--frames N] [--keys f:key,...]
   [--seed S] [--setup l,w,d,set,lvl,rot]: render headless, save a screenshot and exit. */
static int shot_mode(int argc, char **argv)
{
    const char *out = NULL, *keys = "", *screen = "game", *record = NULL;
    long frames = 600, every = 3;
    unsigned seed = 1;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--shot"))
            out = argv[++i];
        else if (!strcmp(argv[i], "--frames"))
            frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--keys"))
            keys = argv[++i];
        else if (!strcmp(argv[i], "--seed"))
            seed = (unsigned)atol(argv[++i]);
        else if (!strcmp(argv[i], "--screen"))
            screen = argv[++i];
        else if (!strcmp(argv[i], "--record"))
            record = argv[++i]; /* dir for frame BMPs */
        else if (!strcmp(argv[i], "--every"))
            every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--setup"))
            sscanf(argv[++i], "%hd,%hd,%hd,%hd,%hd,%hd", &A.setup.len, &A.setup.wid, &A.setup.dep,
                   &A.setup.blockset, &A.setup.level, &A.setup.rot_speed);
    }
    if (!out) return 0;
    A.now = 1.0;
    if (!strcmp(screen, "game") || !strcmp(screen, "demo")) {
        A.demo = !strcmp(screen, "demo");
        bo_srand(&A.game, (uint16_t)seed);
        bo_init(&A.game, &A.setup, A.demo ? BO_MODE_DEMO : BO_MODE_GAME, 1, seed * 7919u);
        A.scr = S_GAME;
        const char *k = keys;
        for (long f = 1; f <= frames && A.game.state != BO_S_DONE; f++) {
            long ticks_due = (long)(f / A.logic_fps * BO_TICK_HZ);
            while (A.ticks_done < ticks_due) {
                bo_tick(&A.game);
                A.ticks_done++;
            }
            while (*k) {
                long kf;
                unsigned kv;
                int n;
                if (sscanf(k, "%ld:%x%n", &kf, &kv, &n) != 2 || kf != f) break;
                bo_key(&A.game, (uint16_t)kv);
                k += n;
                if (*k == ',') k++;
            }
            bo_frame(&A.game);
            if (record && f % every == 0) {
                static long n;
                char fn[512];
                A.frames_done = f;
                A.now = 1.0 + f / A.logic_fps;
                A.t0 = 1.0;
                if (A.game.ev_cleared) A.clear_flash = 1;
                A.clear_flash *= 0.75f;
                render();
                SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, A.w, A.h, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(A.ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
                snprintf(fn, sizeof fn, "%s/f%06ld.bmp", record, n++);
                SDL_SaveBMP(sf, fn);
                SDL_FreeSurface(sf);
            }
        }
        if (record) {
            printf("frames=%d\n", A.game.frame);
            return 1;
        }
        A.frames_done = frames;
        A.t0 = A.now - frames / A.logic_fps;
        printf("state=%d score=%ld cubes=%ld level=%d\n", A.game.state, (long)A.game.score,
               (long)A.game.cubes_played, A.game.level);
    } else if (!strcmp(screen, "hof")) {
        hof_load(&A.setup, &A.hof);
        A.hof_row = hof_insert(&A.hof, 12345);
        hof_set_name(&A.hof, A.hof_row, "sjk");
        A.hof_mode = HOF_VIEW;
        A.scr = S_HOF;
    } else if (!strcmp(screen, "help"))
        A.scr = S_HELP;
    else if (!strcmp(screen, "change"))
        A.scr = S_CHANGE;
    else
        A.scr = S_MAIN;
    render();
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, A.w, A.h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(A.ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, out);
    return 1;
}

int main(int argc, char **argv)
{
    int w = 1280, h = 800;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--size")) sscanf(argv[i + 1], "%dx%d", &w, &h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    A.win = SDL_CreateWindow("BlockOut", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!A.win) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        A.win = SDL_CreateWindow("BlockOut", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    }
    A.ren = SDL_CreateRenderer(A.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!A.ren) A.ren = SDL_CreateRenderer(A.win, -1, 0);
    if (!A.win || !A.ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(A.ren, SDL_BLENDMODE_BLEND);
    gfx_init(A.ren);
    if (!font_init(A.ren, font_ttf, font_ttf_len)) {
        fprintf(stderr, "font initialisation failed\n");
        return 1;
    }
    audio_init();
    store_init();
    load_setup();
    SDL_StopTextInput();

#ifdef __EMSCRIPTEN__
    set_touch(js_coarse_pointer());
#endif
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--touch")) set_touch(1);
    if (shot_mode(argc, argv)) return 0;
    A.now = seconds();
    enter_menu(S_MAIN);
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
