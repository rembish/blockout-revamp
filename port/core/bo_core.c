/*
 * BlockOut game logic, reconstructed from the original BL2.OVL (1989).
 * Comments name the original routines (segment 1000 offsets); see re/NOTES.md.
 */
#include "bo_core.h"
#include <string.h>

/* ---- Turbo C rand() ---------------------------------------------------- */

void bo_srand(bo_game *g, uint16_t seed) { g->rand_seed = seed; }

int bo_rand(bo_game *g)
{
    g->rand_seed = g->rand_seed * 0x015A4E35u + 1;
    return (int)((g->rand_seed >> 16) & 0x7fff);
}

/* ---- keyboard buffer (BIOS type-ahead) --------------------------------- */

void bo_key(bo_game *g, uint16_t key)
{
    if (g->kb_count == BO_KEYBUF) return;   /* full: BIOS beeps and drops it */
    g->keybuf[(g->kb_head + g->kb_count++) % BO_KEYBUF] = key;
}

/* poll_key (51ab): next key or -1; any key resets the idle tick counter */
static int pop_key(bo_game *g)
{
    if (!g->kb_count) return -1;
    int k = g->keybuf[g->kb_head];
    g->kb_head = (g->kb_head + 1) % BO_KEYBUF;
    g->kb_count--;
    g->ticks = 0;
    return k;
}

/* isr_timer_1c (919e) */
void bo_tick(bo_game *g)
{
    if (g->countdown > 0) g->countdown--;
    if (g->sound_ticks > 0) g->sound_ticks--;
    g->ticks++;
    g->bios_ticks++;
}

/* ---- geometry ----------------------------------------------------------- */

static const int8_t *cube(const bo_game *g, int i)
{
    return &bo_piece_cubes[3 * (bo_piece_first[g->piece] + i)];
}

int bo_ncubes(const bo_game *g) { return bo_piece_ncubes[g->piece]; }

/* transform_cube (91f1) */
static void transform(const bo_pose *p, const int8_t *c, int16_t out[3])
{
    for (int i = 0; i < 3; i++) {
        int v = c[p->a[i].axis];
        out[i] = (int16_t)((p->a[i].sign ? v : -v) + p->a[i].pos);
    }
}

void bo_piece_cells(const bo_game *g, const bo_pose *p, int16_t out[][3])
{
    for (int i = 0; i < bo_ncubes(g); i++) transform(p, cube(g, i), out[i]);
}

/* compose_pose (2ea7): apply rotation r in the piece's own frame */
static void compose(const bo_pose *p, const bo_pose *r, bo_pose *out)
{
    for (int i = 0; i < 3; i++) {
        const bo_axis *q = &r->a[p->a[i].axis];
        out->a[i].pos  = (int16_t)((p->a[i].sign ? q->pos : -q->pos) + p->a[i].pos);
        out->a[i].axis = q->axis;
        out->a[i].sign = (int16_t)(p->a[i].sign ? q->sign : 1 - q->sign);
    }
}

static const int16_t *pit_dims(const bo_game *g) { return &g->setup.len; }

/* cube_check (2991): 0 = free, 1 = outside (push = way back in), 2 = occupied.
   An outside cube is clamped first, and its clamped cell is still tested. */
static int cube_check(const bo_game *g, const int8_t *c, const bo_pose *p, int16_t push[3])
{
    int16_t v[3];
    int r = 0;
    transform(p, c, v);
    for (int i = 0; i < 3; i++) {
        int max = pit_dims(g)[i] - 1;
        if (v[i] < 0) { r = 1; push[i] = (int16_t)-v[i]; v[i] = 0; }
        else if (v[i] > max) { r = 1; push[i] = (int16_t)(max - v[i]); v[i] = (int16_t)max; }
        else push[i] = 0;
    }
    if (g->cell[v[0]][v[1]][v[2]]) r = 2;
    return r;
}

/* piece_fits (2af2): with kick, cubes outside the pit push the piece back in. */
static int piece_fits(const bo_game *g, const bo_pose *pose, int kick, int16_t kickvec[3])
{
    bo_pose p = *pose;
    int16_t push[3];
    if (kick) kickvec[0] = kickvec[1] = kickvec[2] = 0;
restart:
    for (int i = 0; i < bo_ncubes(g); i++) {
        int r = cube_check(g, cube(g, i), &p, push);
        if (r == 2) return 0;
        if (r == 1) {
            if (!kick) return 0;
            for (int k = 0; k < 3; k++) { p.a[k].pos += push[k]; kickvec[k] += push[k]; }
            goto restart;
        }
    }
    return 1;
}

/* ---- animations (frame based, visual only, but they gate drop/landing) - */

int bo_rot_busy(const bo_game *g)   /* rot_anim_busy (784a) */
{
    return g->rot_left != 0 || g->rotq_head != g->rotq_tail;
}

static int anims_busy(const bo_game *g) { return g->move_left != 0 || bo_rot_busy(g); }

static void anim_move_step(bo_game *g)   /* 5d3a */
{
    if (!g->move_left) return;
    for (int i = 0; i < 3; i++) g->move_rem[i] -= g->move_rem[i] / g->move_left;
    g->move_left--;
}

static void anim_rot_step(bo_game *g)    /* 776f */
{
    if (g->rot_left == 0) {
        if (g->rotq_head == g->rotq_tail) return;
        g->rot_left = g->rot_steps;
        g->rotq_head = g->rotq_head == 2 ? 0 : g->rotq_head + 1;
    }
    g->rot_left--;
}

/* ---- moves ------------------------------------------------------------- */

/* move_piece (5c24): 0 = moved, 1 = blocked */
static int move_piece(bo_game *g, int dx, int dy, int dz)
{
    bo_pose np = g->pose;
    np.a[0].pos += dx; np.a[1].pos += dy; np.a[2].pos += dz;
    if (!piece_fits(g, &np, 0, NULL)) return 1;
    g->pose = np;
    g->move_rem[0] += dx; g->move_rem[1] += dy; g->move_rem[2] += dz;
    g->move_left = g->move_steps;
    return 0;
}

/* hard_drop (5de7): returns the distance fallen */
static int hard_drop(bo_game *g)
{
    bo_pose p = g->pose;
    int dz = 0;
    do { dz--; p.a[2].pos--; } while (piece_fits(g, &p, 0, NULL));
    dz++;
    if (dz) move_piece(g, 0, 0, dz);
    return -dz;
}

/* rotation_axis (7862): key 0..5 (QWE ASD) -> rotation index for the current pose */
static int rotation_index(const bo_pose *p, int k)
{
    int flip = k > 2;
    if (flip) k -= 3;
    return ((p->a[k].axis << 1) | (p->a[k].sign == 0)) ^ flip;
}

/* rotate_piece (78b2): queued; ignored while two rotations are still pending */
static void rotate_piece(bo_game *g, int k)
{
    int next = g->rotq_tail == 2 ? 0 : g->rotq_tail + 1;
    if (next == g->rotq_head) return;
    bo_rotq_entry *e = &g->rotq[g->rotq_tail];
    int r = rotation_index(&g->pose, k);
    bo_pose np;
    compose(&g->pose, &bo_rotations[r], &np);
    if (!piece_fits(g, &np, 1, e->kick)) return;
    e->rot = (int16_t)r;
    e->before = g->pose;
    for (int i = 0; i < 3; i++) np.a[i].pos += e->kick[i];
    e->after = np;
    g->pose = np;
    g->rotq_tail = (int16_t)next;
}

/* ---- pieces ------------------------------------------------------------ */

static int max_extent(int piece)
{
    int ext[3] = {0, 0, 0};
    for (int i = 0; i < bo_piece_ncubes[piece]; i++)
        for (int k = 0; k < 3; k++) {
            int v = bo_piece_cubes[3 * (bo_piece_first[piece] + i) + k] + 1;
            if (v > ext[k]) ext[k] = v;
        }
    int m = ext[0] > ext[1] ? ext[0] : ext[1];
    return m > ext[2] ? m : ext[2];
}

/* new_piece (3e94) */
static void new_piece(bo_game *g)
{
    const bo_setup *s = &g->setup;
    int count = bo_set_count[s->blockset];
    int idx = bo_set_list[s->blockset][(uint16_t)g->bios_ticks % count];
    int min = s->wid < s->dep ? s->wid : s->dep;
    if (s->len < min) min = s->len;
    for (;;) {
        g->piece = (int16_t)idx;
        /* the reroll is drawn even when the piece fits, and used as a *global*
           piece index without the block-set lookup: original behaviour */
        idx = bo_rand(g) % count;
        if (max_extent(g->piece) <= min) break;
    }
    /* identity orientation, origin at the top corner of the pit */
    for (int i = 0; i < 3; i++) {
        g->pose.a[i].axis = (int16_t)i;
        g->pose.a[i].sign = 1;
        g->pose.a[i].pos = 0;
    }
    g->pose.a[2].pos = (int16_t)(s->dep - 1);
}

/* spawn_piece (550a): 1 = game over */
static int spawn_piece(bo_game *g)
{
    int16_t kick[3];
    new_piece(g);
    if (!piece_fits(g, &g->pose, 1, kick)) return 1;
    for (int i = 0; i < 3; i++) g->pose.a[i].pos += kick[i];
    g->ev_spawned = 1;
    return 0;
}

/* ---- landing & scoring ------------------------------------------------- */

/* clear_layers (52ae) */
static int clear_layers(bo_game *g)
{
    const bo_setup *s = &g->setup;
    int any = 0;
    g->layers_cleared = 0;
    for (int z = 0; z < s->dep; z++) {
        if (g->layer_count[z] < s->len * s->wid) continue;
        any = 1;
        g->layers_cleared++;
        for (int zz = z; zz < s->dep - 1; zz++) {
            for (int x = 0; x < s->len; x++)
                for (int y = 0; y < s->wid; y++) g->cell[x][y][zz] = g->cell[x][y][zz + 1];
            g->layer_count[zz] = g->layer_count[zz + 1];
        }
        for (int x = 0; x < s->len; x++)
            for (int y = 0; y < s->wid; y++) g->cell[x][y][s->dep - 1] = 0;
        g->layer_count[s->dep - 1] = 0;
        z--;
    }
    return any;
}

/* pit_bottom_empty (5276) */
static int pit_empty(const bo_game *g)
{
    for (int x = 0; x < g->setup.len; x++)
        for (int y = 0; y < g->setup.wid; y++)
            if (g->cell[x][y][0]) return 0;
    return 1;
}

/* add_score (8266). X (ds:4e62) is set to len+wid at game start (824a). */
static void add_score(bo_game *g)
{
    const bo_setup *s = &g->setup;
    const int32_t X = s->len + s->wid;
    int32_t L = (int16_t)((g->level + 1) * (g->level + 17));
    int32_t n = g->layers_cleared;
    int32_t dk = bo_depth_factor[s->dep], sk = bo_set_factor[s->blockset];
    int32_t a = (n * X * n * L + n * 150) * dk * sk / 200;
    int32_t b = pit_empty(g) ? sk * L * dk * X / 62 : 0;
    int32_t c = bo_class_factor[bo_piece_class[g->piece]] * L
                * (int16_t)(g->drop_height * 12 + s->dep);
    c = (int32_t)((uint32_t)c << 3) / (int16_t)(s->dep * s->dep) / 40;
    g->score = (g->score + ((a + b + c) >> 1) + 1) % 1000000;
}

static void play_sound(bo_game *g, int snd)
{
    if (!g->sound_on) return;
    g->ev_sound = snd;
    g->sound_ticks = g->sound_len[snd];
}

/* land_piece (37cb), part 1: lock the piece and clear layers */
static void land_lock(bo_game *g)
{
    int16_t v[3];
    g->cubes_played += bo_ncubes(g);
    for (int i = bo_ncubes(g) - 1; i >= 0; i--) {
        transform(&g->pose, cube(g, i), v);
        g->cell[v[0]][v[1]][v[2]] = 1;
        g->layer_count[v[2]]++;
    }
    g->ev_landed = 1;
    if (clear_layers(g)) {
        g->ev_cleared = g->layers_cleared;
        play_sound(g, pit_empty(g) ? BO_SND_PIT_CLEAR : BO_SND_LAYER);
    }
}

/* flush_keys (5237): discard the BIOS type-ahead buffer */
static void flush_keys(bo_game *g) { g->kb_count = 0; }

/* play_piece (55a7) prologue: key flush, level-up check and fall timer */
static void piece_prologue(bo_game *g)
{
    const bo_setup *s = &g->setup;
    flush_keys(g);
    g->h = s->dep;
    g->delay = bo_fall_delay[g->level];
    g->dropped = 0;
    uint16_t thr = (uint16_t)((s->len + s->wid) * (g->earned_level + 1) * 15);
    if ((int32_t)(int16_t)thr <= g->cubes_played && g->earned_level + 1 < BO_NLEVELS) {
        g->earned_level++;
        if (g->level < g->earned_level) {
            g->level = g->earned_level;
            play_sound(g, BO_SND_LEVEL);
        }
        g->delay = bo_fall_delay[g->level];
    }
    g->countdown = g->mode == BO_MODE_PRACTICE ? 0 : g->delay;
    g->drop_height = 0;
}

/* ---- demo AI --------------------------------------------------------- */

/* Rotation sequences tried by the planner: bo_demo_seqs (ds:0478), {n, rot x n}... */
#define demo_seqs bo_demo_seqs
#define DEMO_SEQ_BYTES 0x46

typedef struct { int16_t x[5], y[5], z[5]; } cubes5;

static int pit_free(const bo_game *g, int x, int y, int z)
{
    /* the original reads past the pit edge here; those reads never change the outcome */
    if (x < 0 || y < 0 || z < 0 || x >= g->setup.len || y >= g->setup.wid || z >= g->setup.dep) return 0;
    return !g->cell[x][y][z];
}

/* 10b6 / 111a / 117e: shift all cubes along one axis, undo if any lands on a cube */
static int demo_shift(const bo_game *g, cubes5 *c, int axis, int d)
{
    int16_t *v = axis == 0 ? c->x : axis == 1 ? c->y : c->z;
    int i;
    for (i = 0; i < bo_ncubes(g); i++) {
        v[i] += d;
        if (!pit_free(g, c->x[i], c->y[i], c->z[i])) break;
    }
    if (i == bo_ncubes(g)) return 1;
    for (; i >= 0; i--) v[i] -= d;
    return 0;
}

/* 0ec3: lower is better */
static int demo_eval(bo_game *g, const cubes5 *c)
{
    const bo_setup *s = &g->setup;
    int cells = s->len * s->wid, score = 0, n = bo_ncubes(g);
    for (int i = n - 1; i >= 0; i--) {
        g->cell[c->x[i]][c->y[i]][c->z[i]] = 1;
        if (++g->layer_count[c->z[i]] == cells) score -= 60;
    }
    for (int i = n - 1; i >= 0; i--) {
        int x = c->x[i], y = c->y[i], z = c->z[i], w = g->demo_weight[z];
        score += z * 16;
        if (g->layer_count[z] >= cells - 1) { score -= 7; w /= 4; }
        if (x > 0 && !g->cell[x - 1][y][z]) score += w;
        if (x + 1 < s->len && !g->cell[x + 1][y][z]) score += w;
        if (y > 0 && !g->cell[x][y - 1][z]) score += w;
        if (y + 1 < s->wid && !g->cell[x][y + 1][z]) score += w;
        if (z > 0 && !g->cell[x][y][z - 1]) score += w * 16;
    }
    for (int i = n - 1; i >= 0; i--) {
        g->cell[c->x[i]][c->y[i]][c->z[i]] = 0;
        g->layer_count[c->z[i]]--;
    }
    return score;
}

static void demo_cubes(const bo_game *g, const bo_pose *p, cubes5 *c)   /* 0e6d */
{
    int16_t v[3];
    for (int i = bo_ncubes(g) - 1; i >= 0; i--) {
        transform(p, cube(g, i), v);
        c->x[i] = v[0]; c->y[i] = v[1]; c->z[i] = v[2];
    }
}

/* 130e */
static void demo_plan(bo_game *g)
{
    const bo_setup *s = &g->setup;
    int n = bo_ncubes(g), best = 0x7fff, thresh = -4;
    g->demo_seq = 0; g->demo_dx = g->demo_dy = 0;
    int height = s->dep;
    while (height > 0 && g->layer_count[height - 1] == 0) height--;
    for (int off = 0; off < DEMO_SEQ_BYTES; off += demo_seqs[off] + 1) {
        bo_pose p = g->pose;
        for (int r = 0; r < demo_seqs[off]; r++) {                   /* 11e2 */
            bo_pose np;
            int16_t kick[3];
            compose(&p, &bo_rotations[rotation_index(&p, demo_seqs[off + 1 + r])], &np);
            if (piece_fits(g, &np, 1, kick)) {
                p = np;
                for (int k = 0; k < 3; k++) p.a[k].pos += kick[k];
            }
        }
        cubes5 c = {{0}, {0}, {0}}, orig, row, saved;
        demo_cubes(g, &p, &c);
        orig = c;
        int maxx = 0, minx = s->len, maxy = 0, miny = s->wid, minz = s->dep;
        for (int i = n - 1; i >= 0; i--) {
            if (maxx < c.x[i]) maxx = c.x[i];
            if (c.x[i] < minx) minx = c.x[i];
            if (maxy < c.y[i]) maxy = c.y[i];
            if (c.y[i] < miny) miny = c.y[i];
            if (c.z[i] < minz) minz = c.z[i];
        }
        if (height - 1 < minz) {               /* above the stack: jump to the corner */
            demo_shift(g, &c, 0, -minx); maxx -= minx;
            demo_shift(g, &c, 1, -miny); maxy -= miny;
            demo_shift(g, &c, 2, height - minz);
            minz = height;
        } else {
            while (minx != 0 && demo_shift(g, &c, 0, -1)) { maxx--; minx--; }
            while (miny != 0 && demo_shift(g, &c, 1, -1)) { maxy--; miny--; }
        }
        int ok;
        do {
            row = c;
            int y = maxy;
            do {
                saved = c;                                           /* 126a */
                int drop = 0;
                for (int k = minz; k > 0 && demo_shift(g, &c, 2, -1); k--) drop++;
                int v;
                if (drop < thresh) v = 0x7fff;
                else { v = demo_eval(g, &c); thresh = (int16_t)drop; }
                c = saved;                                           /* 12ce */
                if (v < best) {
                    best = v;
                    g->demo_seq = (int16_t)off;
                    g->demo_dx = (int16_t)(c.x[0] - orig.x[0]);
                    g->demo_dy = (int16_t)(c.y[0] - orig.y[0]);
                }
                ok = demo_shift(g, &c, 1, 1);
            } while (ok && ++y < s->wid);
            c = row;
            ok = demo_shift(g, &c, 0, 1);
        } while (ok && ++maxx < s->len);
        thresh -= 4;
    }
    g->demo_phase = 0;
    g->demo_ri = 0;
}

static void demo_weights(bo_game *g)   /* 0e24 */
{
    g->demo_weight[0] = (int16_t)(g->setup.dep + 1);
    for (int z = 1; z < g->setup.dep; z++) {
        g->demo_weight[z] = (int16_t)(g->demo_weight[z - 1] - 1);
        if (g->demo_weight[z] < 0) g->demo_weight[z] = g->demo_weight[z - 1];
    }
}

/* ---- the frame loop ---------------------------------------------------- */

enum { R_NONE, R_AFTER_DROP, R_LAND, R_AFTER_LAND_SOUND, R_AFTER_LEVEL_SOUND, R_AFTER_PAUSE,
       R_DEMO_SPAWN, R_DEMO_ACT };

void bo_init(bo_game *g, const bo_setup *s, int mode, int fast_cpu, uint32_t bios_ticks)
{
    uint32_t seed = g->rand_seed;
    int16_t drop_height = g->drop_height;
    int sound_len[4];
    memcpy(sound_len, g->sound_len, sizeof sound_len);
    memset(g, 0, sizeof *g);
    g->rand_seed = seed;
    g->drop_height = drop_height;   /* only play_piece resets it; the demo inherits it */
    memcpy(g->sound_len, sound_len, sizeof sound_len);
    g->setup = *s;
    g->mode = mode;
    g->fast_cpu = fast_cpu;
    g->bios_ticks = bios_ticks;
    g->level = s->level;
    g->sound_on = 1;
    g->ev_sound = -1;
    g->move_steps = bo_move_steps[fast_cpu][s->rot_speed];   /* init_move_anim (5be3) */
    if (g->move_steps < 1) g->move_steps = 1;
    g->rot_steps = bo_rot_steps[fast_cpu][s->rot_speed];      /* init_rot_anim (756c) */
    if (g->rot_steps < 1) g->rot_steps = 1;
    if (g->rot_steps > 15) g->rot_steps = 15;
    g->resume_at = R_NONE;
    if (mode == BO_MODE_DEMO) {
        demo_weights(g);
        g->state = BO_S_DEMO;
        g->resume_at = R_DEMO_SPAWN;
        bo_frame(g);            /* run up to the first animation wait */
        g->frame = 0;
        return;
    }
    if (spawn_piece(g)) { g->state = BO_S_GAME_OVER; return; }
    piece_prologue(g);
    g->state = BO_S_PLAY;
}

void bo_fill_cell(bo_game *g, int x, int y, int z)
{
    if (!g->cell[x][y][z]) { g->cell[x][y][z] = 1; g->layer_count[z]++; }
}

int bo_frame(bo_game *g)
{
    int k;
    g->ev_sound = -1;
    g->ev_landed = g->ev_spawned = g->ev_cleared = 0;

    switch (g->state) {
    case BO_S_PLAY:      goto frame_start;
    case BO_S_DROP_WAIT:
    case BO_S_LAND_WAIT: goto wait_frame;
    case BO_S_SOUND:     goto sound;
    case BO_S_PAUSED:    goto paused;
    case BO_S_GAME_OVER: goto game_over;
    case BO_S_DEMO:      goto demo;
    default:             return g->state;
    }

frame_start:                                        /* LAB_563e */
    g->frame++;
    anim_move_step(g);
    anim_rot_step(g);

key_loop:                                           /* LAB_5831 */
    while ((k = pop_key(g)) != -1) {
        switch (k) {
        case 'o': case 'O': g->sound_on = !g->sound_on; break;
        case 'p': case 'P':
            if (g->mode == BO_MODE_GAME) {           /* 6106 */
                g->paused_countdown = g->countdown;
                g->state = BO_S_PAUSED;
                return g->state;
            }
            break;
        case BO_K_ESC: g->aborted = 1; g->state = BO_S_DONE; return g->state;
        case BO_K_SPACE:
            g->drop_height = g->pose.a[2].pos;
            g->h -= (int16_t)hard_drop(g);
            g->resume_at = R_AFTER_DROP;
            if (anims_busy(g)) { g->state = BO_S_DROP_WAIT; return g->state; }
        after_drop:
            if (!g->dropped) { g->countdown = 4; g->dropped = 1; }
            break;
        case 'q': case 'Q': rotate_piece(g, 0); break;
        case 'w': case 'W': rotate_piece(g, 1); break;
        case 'e': case 'E': rotate_piece(g, 2); break;
        case 'a': case 'A': rotate_piece(g, 3); break;
        case 's': case 'S': rotate_piece(g, 4); break;
        case 'd': case 'D': rotate_piece(g, 5); break;
        case BO_K_RIGHT: move_piece(g, 1, 0, 0); break;
        case BO_K_LEFT:  move_piece(g, -1, 0, 0); break;
        case BO_K_UP:    move_piece(g, 0, 1, 0); break;
        case BO_K_DOWN:  move_piece(g, 0, -1, 0); break;
        case BO_K_HOME:  move_piece(g, -1, 0, 0); move_piece(g, 0, 1, 0); break;
        case BO_K_END:   move_piece(g, -1, 0, 0); move_piece(g, 0, -1, 0); break;
        case BO_K_PGUP:  move_piece(g, 1, 0, 0); move_piece(g, 0, 1, 0); break;
        case BO_K_PGDN:  move_piece(g, 1, 0, 0); move_piece(g, 0, -1, 0); break;
        }
    }

    /* gravity */
    if (g->mode == BO_MODE_PRACTICE) {
        if (!g->dropped || g->countdown != 0) return g->state;
    } else {
        if (g->countdown != 0) return g->state;
        g->countdown = g->delay;
        if (!g->dropped) {
            int16_t v[3];
            g->h--;
            for (int i = 0; i < bo_ncubes(g); i++) {
                transform(&g->pose, cube(g, i), v);
                if (v[2] < g->h) return g->state;  /* still entering the pit */
            }
        }
    }
    if (move_piece(g, 0, 0, -1) == 0) { g->dropped = 0; return g->state; }
    g->resume_at = R_LAND;
    if (anims_busy(g)) { g->state = BO_S_LAND_WAIT; return g->state; }
    goto land;

wait_frame:                                         /* wait_animations (5dd6) */
    g->frame++;
    anim_move_step(g);
    anim_rot_step(g);
    if (anims_busy(g)) return g->state;
    g->state = BO_S_PLAY;
    if (g->resume_at == R_AFTER_DROP) goto after_drop;
    goto land;

land:
    land_lock(g);
    if (g->sound_ticks) { g->resume_at = R_AFTER_LAND_SOUND; g->state = BO_S_SOUND; return g->state; }
after_land_sound:
    add_score(g);
    if (g->mode == BO_MODE_DEMO) goto demo_spawn;
    if (spawn_piece(g)) { flush_keys(g); g->state = BO_S_GAME_OVER; g->ticks = 0; return g->state; }
    piece_prologue(g);
    if (g->sound_ticks) { g->resume_at = R_AFTER_LEVEL_SOUND; g->state = BO_S_SOUND; return g->state; }
    g->state = BO_S_PLAY;
    return g->state;

sound:                  /* the original blocks in the speaker routine; ticks keep running */
    if (g->sound_ticks > 0) return g->state;
    g->state = g->mode == BO_MODE_DEMO ? BO_S_DEMO : BO_S_PLAY;
    if (g->resume_at == R_AFTER_LAND_SOUND) goto after_land_sound;
    if (g->resume_at == R_AFTER_LEVEL_SOUND) { g->countdown = g->mode == BO_MODE_PRACTICE ? 0 : g->delay; return g->state; }
    return g->state;

paused:                                             /* 6106 */
    while ((k = pop_key(g)) != -1)
        if (k == 'p' || k == 'P') {
            g->countdown = g->paused_countdown;
            g->state = BO_S_PLAY;
            goto key_loop;
        }
    return g->state;

demo:                   /* 162d: each action is followed by wait_animations */
    if (g->resume_at == R_DEMO_SPAWN) goto demo_spawn;
    g->frame++;
    anim_move_step(g);
    anim_rot_step(g);
    if (anims_busy(g)) return g->state;
demo_act:
    for (;;) {
        if (g->demo_phase == 0) {                       /* rotations */
            if (g->demo_ri < demo_seqs[g->demo_seq]) {
                rotate_piece(g, demo_seqs[g->demo_seq + 1 + g->demo_ri++]);
            } else { g->demo_phase = 1; continue; }
        } else if (g->demo_phase == 1) {                /* step towards the target */
            int sx = g->demo_dx > 0 ? 1 : g->demo_dx < 0 ? -1 : 0;
            int sy = g->demo_dy > 0 ? 1 : g->demo_dy < 0 ? -1 : 0;
            g->demo_dx -= sx; g->demo_dy -= sy;
            move_piece(g, sx, sy, 0);
            if (!sx && !sy) g->demo_phase = 2;
        } else if (g->demo_phase == 2) {
            hard_drop(g);
            g->demo_phase = 3;
        } else {
            land_lock(g);
            if (g->sound_ticks) { g->resume_at = R_AFTER_LAND_SOUND; g->state = BO_S_SOUND; return g->state; }
            add_score(g);
        demo_spawn:
            g->resume_at = R_DEMO_ACT;
            g->state = BO_S_DEMO;
            if (spawn_piece(g)) { g->state = BO_S_GAME_OVER; g->ticks = 0; return g->state; }
            demo_plan(g);
            continue;
        }
        if (anims_busy(g)) return g->state;
    }
    goto demo_act;

game_over:                                          /* game_over (3d2d) */
    if (g->mode == BO_MODE_DEMO) {
        if (pop_key(g) != -1 || g->ticks >= 0x5b) g->state = BO_S_DONE;
        return g->state;
    }
    while ((k = pop_key(g)) != -1)
        if (k == BO_K_ENTER || k == BO_K_ESC) { g->state = BO_S_DONE; break; }
    return g->state;
}
