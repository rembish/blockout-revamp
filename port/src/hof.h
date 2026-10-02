/* Hall of fame, stored in the original BLSCORE.IDX / BLSCORE.DAT format, so score files
   from the DOS version can be dropped in. */
#ifndef HOF_H
#define HOF_H

#include <stdint.h>
#include "../core/bo_core.h"

#define HOF_MAX 10
#define HOF_NAME 10

typedef struct {
    char name[HOF_NAME + 1];     /* padded with '.' like the original */
    int32_t score;
    char date[11];               /* MM-DD-YYYY */
} hof_entry;

typedef struct {
    int16_t len, wid, dep, blockset;
    int count;
    hof_entry e[HOF_MAX];
} hof_table;

void hof_load(const bo_setup *s, hof_table *t);
int  hof_qualifies(const hof_table *t, int32_t score);
int  hof_insert(hof_table *t, int32_t score);              /* returns row, -1 if none */
void hof_set_name(hof_table *t, int row, const char *name);
int  hof_save(const hof_table *t);
int32_t hof_best(const bo_setup *s);

#endif
