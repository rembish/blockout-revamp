#include "view.h"
#include "font.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EGA layer colours of the original (palette 1..7), modernised. */
const rgba layer_colors[7] = {
    {0.18f, 0.42f, 1.00f, 1}, /* blue    */
    {0.20f, 0.83f, 0.42f, 1}, /* green   */
    {0.13f, 0.83f, 0.93f, 1}, /* cyan    */
    {1.00f, 0.30f, 0.37f, 1}, /* red     */
    {0.84f, 0.36f, 1.00f, 1}, /* magenta */
    {1.00f, 0.62f, 0.11f, 1}, /* brown -> amber */
    {0.79f, 0.82f, 0.89f, 1}, /* light grey */
};

static const char *set_names[3] = {"FLAT", "BASIC", "EXTENDED"};

#define C_BG0   rgb_hex(0x05070f, 1)
#define C_BG1   rgb_hex(0x0b1226, 1)
#define C_GRID  rgb_hex(0x2bd98a, 1)
#define C_PANEL rgb_hex(0x0c1430, 1)
#define C_EDGE  rgb_hex(0x2a3a6e, 1)
#define C_LABEL rgb_hex(0x7f9cd9, 1)
#define C_VALUE rgb_hex(0xe9f1ff, 1)
#define C_ACCENT rgb_hex(0x5ef2b0, 1)

/* ---- projection ---------------------------------------------------------- */

static struct {
    float cx, cy, scale, eye, top;
    float hl, hw;
} P;

static void proj_setup(const bo_game *g, box pit)
{
    int n = g->setup.len > g->setup.wid ? g->setup.len : g->setup.wid;
    P.cx = pit.x + pit.w / 2;
    P.cy = pit.y + pit.h / 2;
    P.scale = pit.w / n;
    P.eye = 0.85f * n;                    /* eye height above the rim, in cubes */
    P.top = g->setup.dep;
    P.hl = g->setup.len / 2.f;
    P.hw = g->setup.wid / 2.f;
}

static void proj(float x, float y, float z, float *sx, float *sy)
{
    float d = P.top + P.eye - z;
    if (d < 0.05f) d = 0.05f;
    float k = P.scale * P.eye / d;
    *sx = P.cx + (x - P.hl) * k;
    *sy = P.cy - (y - P.hw) * k;
}

static float depth_shade(const bo_game *g, float z)
{
    return 0.45f + 0.55f * (z + 1) / (g->setup.dep + 1);
}

/* ---- pit ------------------------------------------------------------------ */

static void quad3(const float (*p)[3], rgba c)
{
    float q[8];
    for (int i = 0; i < 4; i++) proj(p[i][0], p[i][1], p[i][2], &q[2 * i], &q[2 * i + 1]);
    gfx_quad(q, c);
}

static void line3(float x0, float y0, float z0, float x1, float y1, float z1, float w, rgba c)
{
    float a, b, d, e;
    proj(x0, y0, z0, &a, &b);
    proj(x1, y1, z1, &d, &e);
    gfx_line(a, b, d, e, w, c);
}

static void draw_pit(const bo_game *g, float lw)
{
    float L = g->setup.len, W = g->setup.wid, D = g->setup.dep;
    /* walls and floor: one quad per layer band for a depth gradient */
    for (int z = 0; z < D; z++) {
        float s0 = 0.10f + 0.22f * z / D, s1 = 0.10f + 0.22f * (z + 1) / D;
        rgba c0 = rgba_mix(C_BG0, C_BG1, s0 * 2), c1 = rgba_mix(C_BG0, C_BG1, s1 * 2);
        (void)c1;
        float zl = z, zh = z + 1;
        const float wall[4][4][3] = {
            {{0, 0, zl}, {L, 0, zl}, {L, 0, zh}, {0, 0, zh}},
            {{L, 0, zl}, {L, W, zl}, {L, W, zh}, {L, 0, zh}},
            {{L, W, zl}, {0, W, zl}, {0, W, zh}, {L, W, zh}},
            {{0, W, zl}, {0, 0, zl}, {0, 0, zh}, {0, W, zh}},
        };
        for (int k = 0; k < 4; k++) quad3(wall[k], rgba_scale(c0, k & 1 ? 0.85f : 1.0f));
    }
    const float floor_[4][3] = {{0, 0, 0}, {L, 0, 0}, {L, W, 0}, {0, W, 0}};
    quad3(floor_, rgba_mix(C_BG0, C_BG1, 0.15f));

    rgba gr = rgba_alpha(C_GRID, 0.30f), gr2 = rgba_alpha(C_GRID, 0.16f);
    for (int z = 0; z <= D; z++) {
        rgba c = z == D ? rgba_alpha(C_GRID, 0.85f) : rgba_alpha(C_GRID, 0.12f + 0.25f * z / D);
        float w = z == D ? lw * 1.6f : lw;
        line3(0, 0, z, L, 0, z, w, c); line3(L, 0, z, L, W, z, w, c);
        line3(L, W, z, 0, W, z, w, c); line3(0, W, z, 0, 0, z, w, c);
    }
    for (int x = 0; x <= L; x++) {
        line3(x, 0, 0, x, 0, D, lw, gr2); line3(x, W, 0, x, W, D, lw, gr2);
        line3(x, 0, 0, x, W, 0, lw, gr);
    }
    for (int y = 0; y <= W; y++) {
        line3(0, y, 0, 0, y, D, lw, gr2); line3(L, y, 0, L, y, D, lw, gr2);
        line3(0, y, 0, L, y, 0, lw, gr);
    }
}

/* ---- settled cubes ----------------------------------------------------- */

typedef struct { int x, y; float d; } cell_ref;

static int occ(const bo_game *g, int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= g->setup.len || y >= g->setup.wid || z >= g->setup.dep) return 0;
    return g->cell[x][y][z];
}

static void face(const float (*p)[3], rgba fill, rgba edge, float lw)
{
    float q[8];
    for (int i = 0; i < 4; i++) proj(p[i][0], p[i][1], p[i][2], &q[2 * i], &q[2 * i + 1]);
    gfx_quad(q, fill);
    for (int i = 0; i < 4; i++) gfx_line(q[2 * i], q[2 * i + 1], q[(2 * i + 2) % 8], q[(2 * i + 3) % 8], lw, edge);
}

static void draw_cube(const bo_game *g, int x, int y, int z, rgba base, float lw)
{
    float x0 = x, x1 = x + 1, y0 = y, y1 = y + 1, z0 = z, z1 = z + 1;
    float sh = depth_shade(g, z);
    rgba edge = rgba_alpha(rgba_scale(base, 0.25f), 0.9f);
    /* side faces that look towards the pit axis */
    if (x1 <= P.hl && !occ(g, x + 1, y, z)) {
        const float f[4][3] = {{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}};
        face(f, rgba_scale(base, 0.62f * sh), edge, lw);
    }
    if (x0 >= P.hl && !occ(g, x - 1, y, z)) {
        const float f[4][3] = {{x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}};
        face(f, rgba_scale(base, 0.62f * sh), edge, lw);
    }
    if (y1 <= P.hw && !occ(g, x, y + 1, z)) {
        const float f[4][3] = {{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
        face(f, rgba_scale(base, 0.48f * sh), edge, lw);
    }
    if (y0 >= P.hw && !occ(g, x, y - 1, z)) {
        const float f[4][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}};
        face(f, rgba_scale(base, 0.48f * sh), edge, lw);
    }
    if (!occ(g, x, y, z + 1)) {
        const float f[4][3] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
        face(f, rgba_scale(base, sh), edge, lw);
    }
}

static int cmp_cell(const void *a, const void *b)
{
    float d = ((const cell_ref *)b)->d - ((const cell_ref *)a)->d;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static void draw_settled(const bo_game *g, float lw, float flash)
{
    cell_ref order[BO_MAX_LEN * BO_MAX_WID];
    int n = 0;
    for (int x = 0; x < g->setup.len; x++)
        for (int y = 0; y < g->setup.wid; y++) {
            float dx = x + .5f - P.hl, dy = y + .5f - P.hw;
            order[n++] = (cell_ref){x, y, dx * dx + dy * dy};
        }
    qsort(order, n, sizeof *order, cmp_cell);
    for (int z = 0; z < g->setup.dep; z++) {
        if (!g->layer_count[z]) continue;
        rgba base = layer_colors[z % 7];
        if (flash > 0) base = rgba_mix(base, rgb_hex(0xffffff, 1), flash * 0.6f);
        for (int i = 0; i < n; i++)
            if (g->cell[order[i].x][order[i].y][z]) draw_cube(g, order[i].x, order[i].y, z, base, lw);
    }
}

/* ---- falling piece: logical pose with pending animation undone ---------- */

typedef struct { float m[3][3], t[3]; } affine;   /* x' = m x + t */

static affine pose_affine(const bo_pose *p)
{
    affine a;
    memset(&a, 0, sizeof a);
    for (int i = 0; i < 3; i++) {
        a.m[i][p->a[i].axis] = p->a[i].sign ? 1.f : -1.f;
        a.t[i] = p->a[i].pos + (p->a[i].sign ? 0.f : 1.f);   /* cell -> continuous */
    }
    return a;
}

static affine aff_mul(const affine *a, const affine *b)   /* a after b */
{
    affine r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = 0;
            for (int k = 0; k < 3; k++) r.m[i][j] += a->m[i][k] * b->m[k][j];
        }
        r.t[i] = a->t[i];
        for (int k = 0; k < 3; k++) r.t[i] += a->m[i][k] * b->t[k];
    }
    return r;
}

static void aff_apply(const affine *a, const float in[3], float out[3])
{
    for (int i = 0; i < 3; i++)
        out[i] = a->m[i][0] * in[0] + a->m[i][1] * in[1] + a->m[i][2] * in[2] + a->t[i];
}

/* Fraction q (0..1) of a 90-degree local rotation r, as a rotation about its pivot line.
   q < 0 gives the inverse direction. */
static affine partial_rot(const bo_pose *rp, float q)
{
    affine r = pose_affine(rp), out;
    int u = 0;
    for (int i = 0; i < 3; i++) if (r.m[i][i] == 1.f) u = i;          /* fixed axis */
    int a = (u + 1) % 3, b = (u + 2) % 3;
    /* in the (a,b) plane r maps (xa, xb) -> (m_aa xa + m_ab xb + ta, ...); a 90 degree turn */
    float s = r.m[b][a];                                   /* sin of the full turn: +1 or -1 */
    /* pivot: solve (I - R) c = t in the plane */
    float A = 1 - r.m[a][a], B = -r.m[a][b], C = -r.m[b][a], D = 1 - r.m[b][b];
    float det = A * D - B * C;
    float ca = (r.t[a] * D - B * r.t[b]) / det, cb = (A * r.t[b] - C * r.t[a]) / det;
    float th = q * (float)M_PI / 2 * s, cs = cosf(th), sn = sinf(th);
    memset(&out, 0, sizeof out);
    out.m[u][u] = 1;
    out.m[a][a] = cs; out.m[a][b] = -sn;
    out.m[b][a] = sn; out.m[b][b] = cs;
    out.t[a] = ca - (cs * ca - sn * cb);
    out.t[b] = cb - (sn * ca + cs * cb);
    return out;
}

static affine piece_visual(const bo_game *g, float frac)
{
    affine v = pose_affine(&g->pose);
    float shift[3] = {0, 0, 0};
    /* entries still in the queue, newest first: undo fully */
    int busy_entry = -1;
    float p = 1;
    if (g->rot_left > 0) {
        busy_entry = (g->rotq_head + 2) % 3;
        float left = g->rot_left - frac;
        if (left < 0) left = 0;
        p = 1 - left / g->rot_steps;
    }
    int k = g->rotq_tail;
    while (k != g->rotq_head) {
        k = (k + 2) % 3;
        const bo_rotq_entry *e = &g->rotq[k];
        affine inv = partial_rot(&bo_rotations[e->rot], -1);
        v = aff_mul(&v, &inv);
        for (int i = 0; i < 3; i++) shift[i] += e->kick[i];
    }
    if (busy_entry >= 0) {
        const bo_rotq_entry *e = &g->rotq[busy_entry];
        affine inv = partial_rot(&bo_rotations[e->rot], -(1 - p));
        v = aff_mul(&v, &inv);
        for (int i = 0; i < 3; i++) shift[i] += e->kick[i] * (1 - p);
    }
    float mk = 1;
    if (g->move_left > 0) mk = 1 - frac / g->move_left;
    for (int i = 0; i < 3; i++) v.t[i] -= shift[i] + g->move_rem[i] * mk;
    return v;
}

typedef struct { float q[8]; float depth; float shade; } pface;

static int cmp_face(const void *a, const void *b)
{
    float d = ((const pface *)b)->depth - ((const pface *)a)->depth;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static void draw_piece(const bo_game *g, float lw, float frac, float t)
{
    affine v = piece_visual(g, frac);
    static const float corners[8][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    static const int faces[6][4] = {{0,1,2,3},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
    pface pf[5 * 6];
    int npf = 0;
    float eye[3] = {P.hl, P.hw, P.top + P.eye};
    for (int c = 0; c < bo_ncubes(g); c++) {
        const int8_t *cc = &bo_piece_cubes[3 * (bo_piece_first[g->piece] + c)];
        float w[8][3];
        for (int i = 0; i < 8; i++) {
            float l[3] = {cc[0] + corners[i][0], cc[1] + corners[i][1], cc[2] + corners[i][2]};
            aff_apply(&v, l, w[i]);
        }
        for (int f = 0; f < 6; f++) {
            pface *p = &pf[npf++];
            float m[3] = {0, 0, 0};
            for (int i = 0; i < 4; i++) {
                const float *pt = w[faces[f][i]];
                proj(pt[0], pt[1], pt[2], &p->q[2 * i], &p->q[2 * i + 1]);
                for (int k = 0; k < 3; k++) m[k] += pt[k] / 4;
            }
            float dx = m[0] - eye[0], dy = m[1] - eye[1], dz = m[2] - eye[2];
            p->depth = dx * dx + dy * dy + dz * dz;
            p->shade = f == 1 ? 1.f : f == 0 ? 0.5f : 0.75f;
        }
    }
    qsort(pf, npf, sizeof *pf, cmp_face);
    float pulse = 0.85f + 0.15f * sinf(t * 4.f);
    rgba fill = rgb_hex(0xbfe6ff, 0.10f), edge = rgb_hex(0xffffff, 0.95f * pulse);
    rgba glow = rgb_hex(0x7fd4ff, 0.18f);
    for (int i = 0; i < npf; i++) gfx_quad(pf[i].q, rgba_alpha(fill, fill.a * pf[i].shade + 0.04f));
    for (int i = 0; i < npf; i++)
        for (int k = 0; k < 4; k++) {
            float *q = pf[i].q;
            gfx_line(q[2 * k], q[2 * k + 1], q[(2 * k + 2) % 8], q[(2 * k + 3) % 8], lw * 4.f, glow);
        }
    for (int i = 0; i < npf; i++)
        for (int k = 0; k < 4; k++) {
            float *q = pf[i].q;
            gfx_line(q[2 * k], q[2 * k + 1], q[(2 * k + 2) % 8], q[(2 * k + 3) % 8], lw * 1.4f, edge);
        }
}

/* ---- layout, gauge and panel ------------------------------------------- */

void view_pit_box(int w, int h, box *pit, box *gauge, box *panel)
{
    float m = h * 0.04f;
    float S = h - 2 * m;
    float gw = S * 0.075f, pw = S * 0.46f, gap = S * 0.03f;
    float total = gw + gap + S + gap + pw;
    if (total > w - 2 * m) {
        float k = (w - 2 * m) / total;
        S *= k; gw *= k; pw *= k; gap *= k; total = w - 2 * m;
    }
    float x = (w - total) / 2, y = (h - S) / 2;
    *gauge = (box){x, y, gw, S};
    *pit = (box){x + gw + gap, y, S, S};
    *panel = (box){x + gw + gap + S + gap, y, pw, S};
}

static void draw_gauge(const bo_game *g, box b, float s)
{
    gfx_round_rect(b.x, b.y, b.w, b.h, b.w * 0.18f, C_PANEL);
    float top = b.y + b.w * 1.25f, bot = b.y + b.h - b.w * 0.2f;
    font_draw(b.x + b.w / 2, b.y + b.w * 0.18f, b.w * 0.30f, C_LABEL, ALIGN_CENTER, "LEVEL");
    font_drawf(b.x + b.w / 2, b.y + b.w * 0.55f, b.w * 0.55f, C_VALUE, ALIGN_CENTER, "%d", g->level);
    int D = g->setup.dep;
    if (D < 10) D = 10;
    float ch = (bot - top) / D, x0 = b.x + b.w * 0.2f, cw = b.w * 0.6f;
    for (int z = 0; z < g->setup.dep; z++) {
        float y = bot - (z + 1) * ch;
        rgba c = g->layer_count[z] ? layer_colors[z % 7] : rgba_alpha(C_EDGE, 0.35f);
        gfx_round_rect(x0, y + ch * 0.1f, cw, ch * 0.8f, ch * 0.18f, c);
    }
    (void)s;
}

static void stat(box b, float y, float s, const char *label, const char *value, rgba vc)
{
    font_draw(b.x + b.w / 2, y, s * 0.42f, C_LABEL, ALIGN_CENTER, label);
    gfx_round_rect(b.x + b.w * 0.08f, y + s * 0.6f, b.w * 0.84f, s * 1.05f, s * 0.2f, rgb_hex(0x060a18, 1));
    font_draw(b.x + b.w * 0.88f, y + s * 0.72f, s * 0.78f, vc, ALIGN_RIGHT, value);
}

static void draw_logo(box b, float y, float s)
{
    /* the original's red/blue "BLOCK OUT" title, as a two-tone wordmark */
    float x = b.x + b.w / 2;
    font_draw(x + s * 0.04f, y + s * 0.04f, s, rgb_hex(0x1b3cff, 1), ALIGN_CENTER, "BLOCK");
    font_draw(x, y, s, rgb_hex(0xff3b4f, 1), ALIGN_CENTER, "BLOCK");
    font_draw(x + s * 0.03f, y + s * 0.84f, s * 0.75f, rgb_hex(0xff3b4f, 1), ALIGN_CENTER, "OUT");
    font_draw(x, y + s * 0.81f, s * 0.75f, rgb_hex(0x4f7bff, 1), ALIGN_CENTER, "OUT");
}

static void draw_panel(const bo_game *g, box b)
{
    gfx_round_rect(b.x, b.y, b.w, b.h, b.w * 0.05f, C_PANEL);
    float s = b.h * 0.052f;
    char buf[64];
    draw_logo(b, b.y + s * 0.6f, s * 1.6f);
    float y = b.y + s * 3.9f;
    snprintf(buf, sizeof buf, "%ld", (long)g->score);
    stat(b, y, s, "SCORE", buf, C_ACCENT); y += s * 2.2f;
    snprintf(buf, sizeof buf, "%ld", (long)g->cubes_played);
    stat(b, y, s, "CUBES PLAYED", buf, C_VALUE); y += s * 2.2f;
    snprintf(buf, sizeof buf, "%ld", (long)g->hiscore);
    stat(b, y, s, "HIGH SCORE", buf, C_VALUE); y += s * 2.2f;
    snprintf(buf, sizeof buf, "%dx%dx%d", g->setup.len, g->setup.wid, g->setup.dep);
    stat(b, y, s, "PIT", buf, rgb_hex(0xffb347, 1)); y += s * 2.2f;
    stat(b, y, s, "BLOCK SET", set_names[g->setup.blockset], rgb_hex(0xffb347, 1)); y += s * 2.4f;
    float ks = s * 0.36f;
    rgba kc = rgba_alpha(C_LABEL, 0.75f);
    font_draw(b.x + b.w / 2, y, ks, kc, ALIGN_CENTER, "ARROWS move   SPACE drop"); y += ks * 1.5f;
    font_draw(b.x + b.w / 2, y, ks, kc, ALIGN_CENTER, "Q W E / A S D rotate"); y += ks * 1.5f;
    font_draw(b.x + b.w / 2, y, ks, kc, ALIGN_CENTER, "P pause   O sound   ESC quit");
}

void view_background(int w, int h)
{
    gfx_rect_v(0, 0, w, h, rgb_hex(0x0a1024, 1), rgb_hex(0x02030a, 1));
}

void view_game(const bo_game *g, int w, int h, const view_fx *fx)
{
    box pit, gauge, panel;
    view_pit_box(w, h, &pit, &gauge, &panel);
    float lw = pit.w / 600.f;
    if (lw < 1) lw = 1;
    proj_setup(g, pit);
    view_background(w, h);
    draw_gauge(g, gauge, lw);
    draw_panel(g, panel);
    draw_pit(g, lw);
    draw_settled(g, lw, fx->clear_flash);
    if (g->state != BO_S_GAME_OVER && g->state != BO_S_DONE) draw_piece(g, lw, fx->frac, fx->time);
    if (fx->message) {
        float s = pit.w * 0.09f;
        gfx_rect(pit.x, pit.y + pit.h / 2 - s * 1.1f, pit.w, s * (fx->submessage ? 2.6f : 2.0f), rgb_hex(0x000000, 0.6f));
        font_draw(pit.x + pit.w / 2, pit.y + pit.h / 2 - s * 0.8f, s, C_VALUE, ALIGN_CENTER, fx->message);
        if (fx->submessage)
            font_draw(pit.x + pit.w / 2, pit.y + pit.h / 2 + s * 0.45f, s * 0.4f, C_ACCENT, ALIGN_CENTER, fx->submessage);
    }
    gfx_flush();
}
