#include "touch.h"
#include "font.h"
#include <math.h>
#include <string.h>
#include "../core/bo_core.h"

#define MAXB 24

enum kind { K_ARROW, K_ROT, K_DROP, K_SMALL };

typedef struct {
    box r;
    uint16_t key;
    int kind;
    float dir_x, dir_y; /* arrows: direction; rotations: axis (0..2) and sense (+1 cw) */
    const char *label;
    int64_t finger;
    int held;
} tbtn;

static tbtn btn[MAXB];
static int nb;

static void add(box r, uint16_t key, int kind, float a, float b, const char *label)
{
    if (nb == MAXB) return;
    btn[nb++] = (tbtn){ r, key, kind, a, b, label, 0, 0 };
}

static box inset(box b, float k)
{
    return (box){ b.x + b.w * k, b.y + b.h * k, b.w * (1 - 2 * k), b.h * (1 - 2 * k) };
}

/* 8-way pad; diagonals are the numpad's Home/PgUp/End/PgDn */
static void dpad(box a)
{
    float s = fminf(a.w, a.h) / 3.f, x0 = a.x + (a.w - 3 * s) / 2, y0 = a.y + (a.h - 3 * s) / 2;
    static const uint16_t keys[3][3] = {
        { BO_K_HOME, BO_K_UP, BO_K_PGUP },
        { BO_K_LEFT, 0, BO_K_RIGHT },
        { BO_K_END, BO_K_DOWN, BO_K_PGDN },
    };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (keys[r][c])
                add(inset((box){ x0 + c * s, y0 + r * s, s, s }, 0.05f), keys[r][c], K_ARROW, (float)(c - 1),
                    (float)(1 - r), NULL);
}

/* Q W E over A S D, then DROP */
static void rotations(box a)
{
    static const char keys[2][3] = { { 'q', 'w', 'e' }, { 'a', 's', 'd' } };
    float drop_h = a.h * 0.26f;
    float s = fminf(a.w / 3.f, (a.h - drop_h) / 2.f);
    float x0 = a.x + (a.w - 3 * s) / 2, y0 = a.y + (a.h - drop_h - 2 * s) / 2;
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++)
            add(inset((box){ x0 + c * s, y0 + r * s, s, s }, 0.05f), (uint16_t)keys[r][c], K_ROT, (float)c,
                r ? 1.f : -1.f, NULL);
    add(inset((box){ x0, y0 + 2 * s, 3 * s, drop_h }, 0.03f), BO_K_SPACE, K_DROP, 0, 0, "DROP");
}

static void small_row(box a, int menu)
{
    float w = fminf(a.h * 2.4f, a.w / 3.3f), h = fminf(a.h, w * 0.5f);
    if (menu) {
        add((box){ a.x, a.y, w, h }, BO_K_ESC, K_SMALL, 0, 0, "BACK");
        return;
    }
    add((box){ a.x, a.y, w, h }, BO_K_ESC, K_SMALL, 0, 0, "ESC");
    add((box){ a.x + w * 1.15f, a.y, w, h }, 'p', K_SMALL, 0, 0, "PAUSE");
    add((box){ a.x + w * 2.3f, a.y, w, h }, 'o', K_SMALL, 0, 0, "SOUND");
}

void touch_layout(const view_layout_t *L, int screen)
{
    int64_t keep[MAXB];
    int keep_n = nb;
    for (int i = 0; i < nb; i++) keep[i] = btn[i].held ? btn[i].finger : -1;
    nb = 0;
    if (screen == TOUCH_NONE || L->left.w <= 0) return;
    float row = L->portrait ? L->left.h * 0.13f : L->left.w * 0.16f;
    box top = L->portrait ? (box){ L->left.x, L->left.y, L->right.x + L->right.w - L->left.x, row }
                          : (box){ L->left.x, L->left.y, L->left.w, row };
    small_row(top, screen == TOUCH_MENU);
    if (screen == TOUCH_GAME) {
        float off = row * 1.3f;
        dpad((box){ L->left.x, L->left.y + off, L->left.w, L->left.h - off });
        float roff = L->portrait ? off : 0;
        rotations((box){ L->right.x, L->right.y + roff, L->right.w, L->right.h - roff });
    }
    /* layouts are rebuilt every frame; keep held state for buttons still under a finger */
    for (int i = 0; i < nb && i < keep_n; i++)
        if (keep[i] >= 0) {
            btn[i].held = 1;
            btn[i].finger = keep[i];
        }
}

static void arc_icon(float cx, float cy, float r, float sense, rgba c, float lw)
{
    const int seg = 14;
    float a0 = -2.4f, a1 = 2.4f;
    float px = 0, py = 0;
    for (int i = 0; i <= seg; i++) {
        float a = a0 + (a1 - a0) * i / seg, x = cx + sinf(a) * r, y = cy - cosf(a) * r * 0.55f;
        if (i) gfx_line(px, py, x, y, lw, c);
        px = x;
        py = y;
    }
    /* arrow head at the end the rotation runs towards */
    float a = sense > 0 ? a1 : a0, ex = cx + sinf(a) * r, ey = cy - cosf(a) * r * 0.55f;
    float tx = cosf(a) * (sense > 0 ? 1 : -1), ty = sinf(a) * 0.55f * (sense > 0 ? 1 : -1);
    float n = sqrtf(tx * tx + ty * ty);
    tx /= n;
    ty /= n;
    float hs = r * 0.45f;
    gfx_tri(ex + tx * hs * 0.6f, ey + ty * hs * 0.6f, ex - tx * hs * 0.4f - ty * hs * 0.5f,
            ey - ty * hs * 0.4f + tx * hs * 0.5f, ex - tx * hs * 0.4f + ty * hs * 0.5f,
            ey - ty * hs * 0.4f - tx * hs * 0.5f, c);
}

void touch_draw(float t)
{
    (void)t;
    for (int i = 0; i < nb; i++) {
        tbtn *b = &btn[i];
        float rad = fminf(b->r.w, b->r.h) * 0.22f;
        rgba face = b->held ? rgb_hex(0x2bd98a, 0.45f) : rgb_hex(0x16224a, 0.72f);
        rgba ink = b->held ? rgb_hex(0xffffff, 1) : rgb_hex(0xc9d6f5, 0.9f);
        gfx_round_rect(b->r.x, b->r.y, b->r.w, b->r.h, rad, face);
        float cx = b->r.x + b->r.w / 2, cy = b->r.y + b->r.h / 2, s = fminf(b->r.w, b->r.h);
        switch (b->kind) {
        case K_ARROW: {
            float n = sqrtf(b->dir_x * b->dir_x + b->dir_y * b->dir_y), dx = b->dir_x / n, dy = -b->dir_y / n;
            float k = s * (n > 1.2f ? 0.16f : 0.22f);
            float bw = k * 0.75f; /* half width of the arrow's base */
            gfx_tri(cx + dx * k, cy + dy * k, cx - dx * k * 0.6f - dy * bw, cy - dy * k * 0.6f + dx * bw,
                    cx - dx * k * 0.6f + dy * bw, cy - dy * k * 0.6f - dx * bw, ink);
            break;
        }
        case K_ROT: {
            static const char *axis[3] = { "X", "Y", "Z" };
            arc_icon(cx, cy - s * 0.05f, s * 0.28f, b->dir_y, ink, fmaxf(1.5f, s * 0.04f));
            font_draw(cx, cy + s * 0.06f, s * 0.24f, ink, ALIGN_CENTER, axis[(int)b->dir_x]);
            const char k[2] = { (char)(b->key - 32), 0 };
            font_draw(b->r.x + s * 0.1f, b->r.y + s * 0.06f, s * 0.16f, rgba_alpha(ink, 0.55f), ALIGN_LEFT,
                      k);
            break;
        }
        default:
            font_draw(cx, cy - s * 0.17f, s * (b->kind == K_DROP ? 0.34f : 0.36f), ink, ALIGN_CENTER,
                      b->label);
        }
    }
    gfx_flush();
}

uint16_t touch_press(float x, float y, int64_t finger)
{
    for (int i = 0; i < nb; i++) {
        tbtn *b = &btn[i];
        if (x >= b->r.x && x < b->r.x + b->r.w && y >= b->r.y && y < b->r.y + b->r.h) {
            b->held = 1;
            b->finger = finger;
            return b->key;
        }
    }
    return 0;
}

uint16_t touch_release(int64_t finger)
{
    uint16_t k = 0;
    for (int i = 0; i < nb; i++)
        if (btn[i].held && btn[i].finger == finger) {
            btn[i].held = 0;
            k = btn[i].key;
        }
    return k;
}

void touch_release_all(void)
{
    for (int i = 0; i < nb; i++) btn[i].held = 0;
}
