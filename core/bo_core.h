/*
 * BlockOut game logic, reconstructed from the original BL2.OVL (1989).
 *
 * The core knows nothing about graphics, audio or real time. The frontend drives it:
 *
 *   bo_tick(g)        once per BIOS timer tick (18.2065 Hz) -- the original INT 1Ch handler
 *   bo_key(g, code)   whenever a key arrives (BIOS key word: ascii, or scan<<8 for
 *                     extended keys; see BO_K_*). Key repeat is the frontend's job.
 *   bo_frame(g)       once per rendered frame -- one iteration of the original game loop
 *
 * Function names in comments refer to the original code (see re/NOTES.md).
 */
#ifndef BO_CORE_H
#define BO_CORE_H

#include <stdint.h>
#include "bo_tables.h"

#define BO_TICK_HZ (1193182.0 / 65536.0)

#define BO_MAX_LEN 7
#define BO_MAX_WID 7
#define BO_KEYBUF  15 /* BIOS type-ahead buffer capacity */

/* Key words as delivered by the original's key translation (5176). */
enum {
    BO_K_ESC = 0x1b,
    BO_K_ENTER = 0x0d,
    BO_K_SPACE = 0x20,
    BO_K_HOME = 0x4700,
    BO_K_UP = 0x4800,
    BO_K_PGUP = 0x4900,
    BO_K_LEFT = 0x4b00,
    BO_K_RIGHT = 0x4d00,
    BO_K_END = 0x4f00,
    BO_K_DOWN = 0x5000,
    BO_K_PGDN = 0x5100,
};

enum bo_mode { BO_MODE_GAME = 0, BO_MODE_DEMO = 1, BO_MODE_PRACTICE = 3 };

enum bo_state {
    BO_S_PLAY,      /* at the top of the per-piece frame loop */
    BO_S_DROP_WAIT, /* hard drop: waiting for animations, then resumes key handling */
    BO_S_LAND_WAIT, /* piece blocked: waiting for animations, then lands */
    BO_S_SOUND,     /* a blocking PC-speaker effect is playing */
    BO_S_PAUSED,
    BO_S_GAME_OVER, /* "game over" shown; waiting for Enter/Esc */
    BO_S_DEMO,      /* demo: the AI is waiting for an animation to finish */
    BO_S_DONE,      /* game finished; see `aborted`. Mode 0 goes to the hall of fame
                         either way, practice mode returns to the menu */
};

enum bo_sound { BO_SND_PIT_CLEAR = 0, BO_SND_LAYER = 1, BO_SND_LEVEL = 2, BO_SND_TUNE = 3 };

typedef struct {
    int16_t len, wid, dep; /* pit size: x, y, z (z = depth, 0 = bottom) */
    int16_t blockset;      /* 0 FLAT, 1 BASIC, 2 EXTENDED */
    int16_t level;         /* starting level 0..9 */
    int16_t rot_speed;     /* 0 SLOW, 1 MEDIUM, 2 FAST */
} bo_setup;

typedef struct {
    int16_t rot;           /* index into bo_rotations */
    int16_t kick[3];       /* wall-kick offset applied with it */
    bo_pose before, after; /* logical poses around this rotation (for rendering) */
} bo_rotq_entry;

typedef struct bo_game {
    bo_setup setup;
    int mode;
    int fast_cpu; /* original cpu_speed_class() > 2 */

    /* pit */
    uint8_t cell[BO_MAX_LEN][BO_MAX_WID][BO_MAX_DEP];
    int16_t layer_count[BO_MAX_DEP];

    /* current piece */
    int16_t piece;
    bo_pose pose;

    /* counters */
    int16_t level, earned_level;
    int32_t cubes_played, score, hiscore;
    int16_t layers_cleared, drop_height;
    int16_t countdown;   /* decremented by bo_tick while > 0 */
    uint16_t ticks;      /* free-running, reset by key presses */
    uint32_t bios_ticks; /* time-of-day tick count (piece selection) */
    uint32_t rand_seed;

    /* play_piece locals */
    int16_t delay, h;
    uint8_t dropped;

    /* animations (frame based) */
    int16_t move_steps, move_left;
    float move_rem[3]; /* visual offset still to travel, in cubes */
    int16_t rot_steps, rot_left, rotq_head, rotq_tail;
    bo_rotq_entry rotq[3];

    /* keyboard */
    uint16_t keybuf[BO_KEYBUF];
    int kb_head, kb_count;

    /* demo AI (st_demo 16f5: plan 130e, perform 162d) */
    int16_t demo_seq, demo_dx, demo_dy; /* chosen rotation sequence and offset */
    int16_t demo_phase, demo_ri;
    int16_t demo_weight[BO_MAX_DEP]; /* 0e24 */

    int state;
    int aborted;   /* finished by Esc rather than game over */
    int resume_at; /* where a wait/sound/pause continues */
    int16_t paused_countdown;
    int sound_on;
    int sound_ticks;  /* remaining blocking time of the current sound */
    int sound_len[4]; /* blocking length of each effect in ticks (0 = none) */
    int frame;        /* frames run so far */

    /* events for the frontend, cleared at the start of each bo_frame */
    int ev_sound; /* -1 or enum bo_sound */
    int ev_landed, ev_spawned, ev_cleared;
} bo_game;

/* RNG / clock seeding: the original does srand(time(NULL)) once at startup and reads the
   BIOS tick count for each new piece. bo_init keeps the RNG state, so seed before it. */
void bo_srand(bo_game *g, uint16_t seed);
int bo_rand(bo_game *g);

void bo_init(bo_game *g, const bo_setup *s, int mode, int fast_cpu, uint32_t bios_ticks);
void bo_tick(bo_game *g);
void bo_key(bo_game *g, uint16_t key);
int bo_frame(bo_game *g); /* returns g->state */

/* testing: occupy a pit cell (call right after bo_init) */
void bo_fill_cell(bo_game *g, int x, int y, int z);

/* helpers for frontends */
void bo_piece_cells(const bo_game *g, const bo_pose *p, int16_t out[][3]);
int bo_ncubes(const bo_game *g);
int bo_rot_busy(const bo_game *g);

#endif
