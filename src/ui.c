#include "ui.h"
#include "font.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define C_BOX    rgb_hex(0x0c1430, 1)
#define C_BORDER rgb_hex(0x2bd98a, 1)
#define C_TITLE  rgb_hex(0xffb347, 1)
#define C_ITEM   rgb_hex(0xc9d6f5, 1)
#define C_SEL    rgb_hex(0x5ef2b0, 1)
#define C_DIS    rgb_hex(0x4a5a80, 1)

void ui_menu_clear(ui_menu *m, const char *title)
{
    int sel = m->sel;
    memset(m, 0, sizeof *m);
    m->title = title;
    m->sel = sel;
}

ui_item *ui_add(ui_menu *m, char hotkey, const char *fmt, ...)
{
    ui_item *it = &m->items[m->n++];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it->label, sizeof it->label, fmt, ap);
    va_end(ap);
    it->hotkey = hotkey;
    return it;
}

void ui_dim(box area, float alpha) { gfx_rect(area.x, area.y, area.w, area.h, rgb_hex(0x02040c, alpha)); }

static void frame_box(box b, float t)
{
    gfx_round_rect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, b.w * 0.03f + 2,
                   rgba_alpha(C_BORDER, 0.55f + 0.15f * sinf(t * 2)));
    gfx_round_rect(b.x, b.y, b.w, b.h, b.w * 0.03f, C_BOX);
}

void ui_menu_draw(ui_menu *m, box area, float t)
{
    if (m->sel >= m->n) m->sel = m->n - 1;
    if (m->sel < 0) m->sel = 0;
    float s = area.w * 0.052f; /* item text size */
    float row = s * 1.55f;
    float w = area.w * 0.80f;
    int titled = m->title && m->title[0];
    float h = (titled ? s * 2.6f : s * 0.9f) + m->n * row + s * 0.8f + (m->subtitle ? s : 0);
    box b = { area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h };
    frame_box(b, t);
    float y = b.y + s * 0.6f;
    if (titled) {
        font_draw(b.x + w / 2, y, s * 1.05f, C_TITLE, ALIGN_CENTER, m->title);
        y += s * 1.5f;
    } else
        y -= s * 0.6f;
    if (m->subtitle) {
        font_draw(b.x + w / 2, y, s * 0.6f, C_DIS, ALIGN_CENTER, m->subtitle);
        y += s;
    }
    y += s * 0.5f;
    for (int i = 0; i < m->n; i++) {
        ui_item *it = &m->items[i];
        box r = { b.x + s * 0.5f, y - row * 0.18f, w - s, row };
        m->rects[i] = r;
        rgba c = it->disabled ? C_DIS : C_ITEM;
        if (i == m->sel) {
            gfx_round_rect(r.x, r.y, r.w, r.h, row * 0.25f, rgba_alpha(C_SEL, 0.14f));
            gfx_round_rect(r.x, r.y + row * 0.15f, s * 0.12f, r.h - row * 0.3f, s * 0.06f, C_SEL);
            c = C_SEL;
        }
        float x = r.x + s * 0.6f;
        font_draw(x, y, s, c, ALIGN_LEFT, it->label);
        if (it->value[0]) {
            float vx = r.x + r.w - s * 0.6f;
            if (it->adjustable) {
                font_draw(vx, y, s, rgba_alpha(c, 0.7f), ALIGN_RIGHT, ">");
                vx -= s * 0.9f;
            }
            float vw = font_draw(vx, y, s, C_TITLE, ALIGN_RIGHT, it->value);
            if (it->adjustable) font_draw(vx - vw - s * 0.35f, y, s, rgba_alpha(c, 0.7f), ALIGN_RIGHT, "<");
        }
        y += row;
    }
}

int ui_hit(const ui_menu *m, float x, float y)
{
    for (int i = 0; i < m->n; i++) {
        const box *r = &m->rects[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) return i;
    }
    return -1;
}

void ui_text_panel(box area, const char *title, const char *const *lines, int n, float t)
{
    float s = area.w * 0.040f;
    float w = area.w * 0.9f, h = s * 3.2f + n * s * 1.45f;
    box b = { area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h };
    frame_box(b, t);
    float y = b.y + s * 0.8f;
    font_draw(b.x + w / 2, y, s * 1.3f, C_TITLE, ALIGN_CENTER, title);
    y += s * 2.2f;
    for (int i = 0; i < n; i++, y += s * 1.45f) {
        const char *l = lines[i];
        const char *tab = strchr(l, '\t');
        if (tab) {
            char k[64];
            snprintf(k, sizeof k, "%.*s", (int)(tab - l), l);
            font_draw(b.x + w * 0.42f, y, s, C_SEL, ALIGN_RIGHT, k);
            font_draw(b.x + w * 0.47f, y, s, C_ITEM, ALIGN_LEFT, tab + 1);
        } else {
            font_draw(b.x + w / 2, y, s, C_ITEM, ALIGN_CENTER, l);
        }
    }
}
