/* Menu panels drawn over the pit. */
#ifndef UI_H
#define UI_H

#include "view.h"

#define UI_MAX_ITEMS 12

typedef struct {
    char label[80];
    char value[40]; /* shown right-aligned with < > when adjustable */
    int adjustable;
    int disabled;
    char hotkey;
} ui_item;

typedef struct {
    const char *title;
    const char *subtitle;
    ui_item items[UI_MAX_ITEMS];
    int n, sel;
    /* filled by ui_menu_draw for mouse hit-testing */
    box rects[UI_MAX_ITEMS];
} ui_menu;

void ui_menu_clear(ui_menu *m, const char *title);
ui_item *ui_add(ui_menu *m, char hotkey, const char *fmt, ...);
void ui_menu_draw(ui_menu *m, box area, float t);
int ui_hit(const ui_menu *m, float x, float y); /* item under point or -1 */

void ui_dim(box area, float alpha);
void ui_text_panel(box area, const char *title, const char *const *lines, int n, float t);

#endif
