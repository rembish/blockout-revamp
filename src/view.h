/* Game screen rendering. */
#ifndef VIEW_H
#define VIEW_H

#include "gfx.h"
#include "../core/bo_core.h"

typedef struct {
    float x, y, w, h;
} box;

typedef struct {
    float time;          /* seconds, for subtle effects */
    float frac;          /* 0..1 progress towards the next logic frame */
    float clear_flash;   /* 1 -> 0 after a layer clear */
    const char *message; /* big overlay text or NULL */
    const char *submessage;
} view_fx;

extern const rgba layer_colors[7];

void view_background(int w, int h);
void view_game(const bo_game *g, int w, int h, const view_fx *fx);
void view_pit_box(int w, int h, box *pit, box *gauge, box *panel);

#endif
