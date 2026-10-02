/* Generate a key script that plays well (clears layers) by searching placements on copies
   of the core. Output is a replay.c script; it exercises scoring paths random keys miss.

   usage: botgen LEN WID DEP SET LEVEL ROT MODE FPS FAST SEED BIOS PIECES [fill-seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/bo_core.h"

static int fps;

static unsigned lcg_state;
static int lcg(void) /* deterministic fill pattern, independent of the C library */
{
    lcg_state = lcg_state * 1103515245u + 12345u;
    return (int)((lcg_state >> 16) & 0x7fff);
}
static int ticks_at(long f) { return (int)((double)f * BO_TICK_HZ / fps); }

typedef struct {
    long f;
    uint16_t k;
} key_ev;
static key_ev script[200000];
static int nscript;

/* advance one frame with ticks; keys for this frame already pushed */
static void step(bo_game *g, long *f)
{
    (*f)++;
    for (int t = ticks_at(*f - 1); t < ticks_at(*f); t++) bo_tick(g);
}

static int live(const bo_game *g)
{
    return g->state == BO_S_PLAY || g->state == BO_S_DROP_WAIT || g->state == BO_S_LAND_WAIT;
}

/* keys for a plan, 3 per frame */
static int plan_keys(int r1, int r2, int dx, int dy, uint16_t *out)
{
    static const uint16_t rk[6] = { 'q', 'w', 'e', 'a', 's', 'd' };
    int n = 0;
    if (r1 >= 0) out[n++] = rk[r1];
    if (r2 >= 0) out[n++] = rk[r2];
    for (int i = 0; i < abs(dx); i++) out[n++] = dx > 0 ? BO_K_RIGHT : BO_K_LEFT;
    for (int i = 0; i < abs(dy); i++) out[n++] = dy > 0 ? BO_K_UP : BO_K_DOWN;
    out[n++] = BO_K_SPACE;
    return n;
}

/* run g from frame f applying keys; stop after landing. Returns frames used. */
static long run_plan(bo_game *g, long f, const uint16_t *keys, int nk, int record)
{
    long start = f;
    int ki = 0;
    while (live(g)) {
        step(g, &f);
        for (int j = 0; j < 3 && ki < nk; j++, ki++) {
            bo_key(g, keys[ki]);
            if (record) script[nscript++] = (key_ev){ f, keys[ki] };
        }
        bo_frame(g);
        if (g->ev_landed && ki >= nk) break;
        if (f - start > 20000) break;
    }
    return f;
}

static int height(const bo_game *g)
{
    int h = 0;
    for (int z = 0; z < g->setup.dep; z++)
        if (g->layer_count[z]) h = z + 1;
    return h;
}

int main(int argc, char **argv)
{
    if (argc < 13) {
        fprintf(stderr, "usage: see source\n");
        return 2;
    }
    bo_setup s = { (int16_t)atoi(argv[1]), (int16_t)atoi(argv[2]), (int16_t)atoi(argv[3]),
                   (int16_t)atoi(argv[4]), (int16_t)atoi(argv[5]), (int16_t)atoi(argv[6]) };
    int mode = atoi(argv[7]), fast = atoi(argv[9]), pieces = atoi(argv[12]);
    unsigned seed = (unsigned)atol(argv[10]), bios = (unsigned)atol(argv[11]);
    fps = atoi(argv[8]);
    int fill_seed = argc > 13 ? atoi(argv[13]) : 0;

    static bo_game g;
    bo_srand(&g, (uint16_t)seed);
    bo_init(&g, &s, mode, fast, bios);
    int nfill = 0, fill[3 * 7 * 7 * 18];
    if (fill_seed) {
        lcg_state = (unsigned)fill_seed;
        int layers = 1 + lcg() % 3;
        for (int z = 0; z < layers && z < s.dep - 4; z++) {
            int hx = lcg() % s.len, hy = lcg() % s.wid;
            for (int x = 0; x < s.len; x++)
                for (int y = 0; y < s.wid; y++)
                    if (!(x == hx && y == hy) && lcg() % 9) {
                        fill[3 * nfill] = x;
                        fill[3 * nfill + 1] = y;
                        fill[3 * nfill + 2] = z;
                        nfill++;
                        bo_fill_cell(&g, x, y, z);
                    }
        }
    }

    long f = 0;
    for (int p = 0; p < pieces && live(&g); p++) {
        int best = -(1 << 30), br1 = -1, br2 = -1, bdx = 0, bdy = 0;
        for (int r1 = -1; r1 < 6; r1++)
            for (int r2 = -1; r2 < (r1 < 0 ? 0 : 6); r2++)
                for (int dx = -s.len; dx <= s.len; dx++)
                    for (int dy = -s.wid; dy <= s.wid; dy++) {
                        static bo_game c;
                        uint16_t keys[32];
                        int nk = plan_keys(r1, r2, dx, dy, keys);
                        if (nk > 15) continue;
                        c = g;
                        run_plan(&c, f, keys, nk, 0);
                        int v = c.layers_cleared * 1000 - height(&c) * 10 - nk;
                        if (!live(&c) && c.state != BO_S_DONE) v -= 100000;
                        if (v > best) {
                            best = v;
                            br1 = r1;
                            br2 = r2;
                            bdx = dx;
                            bdy = dy;
                        }
                    }
        uint16_t keys[32];
        int nk = plan_keys(br1, br2, bdx, bdy, keys);
        f = run_plan(&g, f, keys, nk, 1);
    }

    printf("%d %d %d %d %d %d %d %d %d %u %u\n", s.len, s.wid, s.dep, s.blockset, s.level, s.rot_speed, mode,
           fps, fast, seed, bios);
    printf("%d", nfill);
    for (int i = 0; i < 3 * nfill; i++) printf(" %d", fill[i]);
    printf("\n");
    for (int i = 0; i < nscript; i++) printf("%ld %x\n", script[i].f, script[i].k);
    fprintf(stderr, "frames=%ld score=%ld cubes=%ld level=%d\n", f, (long)g.score, (long)g.cubes_played,
            g.level);
    return 0;
}
