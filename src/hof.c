/*
 * BLSCORE.IDX: "05-26-89\0" + N x {int16 len, wid, dep, set; int32 offset into DAT}, kept
 *              sorted by (depth, min(len,wid), max(len,wid), set) -- see 7a10.
 * BLSCORE.DAT: 262-byte tables: int16 count; 10 x {char name[11]; int32 score; char date[11]}.
 * Length and width are interchangeable keys. All little-endian.
 */
#include "hof.h"
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VERSION    "05-26-89"
#define IDX_HDR    9
#define KEY_SIZE   12
#define TABLE_SIZE 262
#define ENTRY_SIZE 26
#define MAX_TABLES 2000

static int16_t rd16(const unsigned char *p) { return (int16_t)(p[0] | p[1] << 8); }
static int32_t rd32(const unsigned char *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}
static void wr16(unsigned char *p, int v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
static void wr32(unsigned char *p, int32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)((uint32_t)v >> (8 * i));
}

static int mn(int a, int b) { return a < b ? a : b; }
static int mx(int a, int b) { return a > b ? a : b; }

/* 7a10: <0 key before setup, 0 same table, >0 after */
static int key_cmp(const unsigned char *k, const bo_setup *s)
{
    int kl = rd16(k), kw = rd16(k + 2), kd = rd16(k + 4), ks = rd16(k + 6);
    if (kd != s->dep) return kd - s->dep;
    if (mn(kl, kw) != mn(s->len, s->wid)) return mn(kl, kw) - mn(s->len, s->wid);
    if (mx(kl, kw) != mx(s->len, s->wid)) return mx(kl, kw) - mx(s->len, s->wid);
    return ks - s->blockset;
}

static unsigned char *load_file(const char *name, int *len)
{
    int cap = name[8] == 'i' ? IDX_HDR + KEY_SIZE * MAX_TABLES : TABLE_SIZE * MAX_TABLES;
    unsigned char *b = malloc((size_t)cap);
    *len = store_read(name, b, cap);
    return b;
}

static void decode_table(const unsigned char *p, hof_table *t)
{
    t->count = rd16(p);
    if (t->count < 0 || t->count > HOF_MAX) t->count = 0;
    for (int i = 0; i < t->count; i++) {
        const unsigned char *e = p + 2 + i * ENTRY_SIZE;
        memcpy(t->e[i].name, e, 11);
        t->e[i].name[HOF_NAME] = 0;
        t->e[i].score = rd32(e + 11);
        memcpy(t->e[i].date, e + 15, 11);
        t->e[i].date[10] = 0;
    }
}

static void encode_table(const hof_table *t, unsigned char *p)
{
    memset(p, 0, TABLE_SIZE);
    wr16(p, t->count);
    for (int i = 0; i < t->count; i++) {
        unsigned char *e = p + 2 + i * ENTRY_SIZE;
        memcpy(e, t->e[i].name, 11);
        wr32(e + 11, t->e[i].score);
        memcpy(e + 15, t->e[i].date, 11);
    }
}

void hof_load(const bo_setup *s, hof_table *t)
{
    memset(t, 0, sizeof *t);
    t->len = s->len;
    t->wid = s->wid;
    t->dep = s->dep;
    t->blockset = s->blockset;
    int il, dl;
    unsigned char *idx = load_file("blscore.idx", &il), *dat = load_file("blscore.dat", &dl);
    if (il >= IDX_HDR && !memcmp(idx, VERSION, 9))
        for (int k = IDX_HDR; k + KEY_SIZE <= il; k += KEY_SIZE)
            if (key_cmp(idx + k, s) == 0) {
                int32_t off = rd32(idx + k + 8);
                if (off >= 0 && off + TABLE_SIZE <= dl) decode_table(dat + off, t);
                break;
            }
    free(idx);
    free(dat);
}

int32_t hof_best(const bo_setup *s)
{
    hof_table t;
    hof_load(s, &t);
    return t.count ? t.e[0].score : 0;
}

/* 3b90 / 3a88 */
int hof_qualifies(const hof_table *t, int32_t score)
{
    if (score <= 0) return 0;
    return t->count < HOF_MAX || score > t->e[t->count - 1].score;
}

int hof_insert(hof_table *t, int32_t score)
{
    if (!hof_qualifies(t, score)) return -1;
    int row = 0;
    while (row < t->count && !(t->e[row].score < score)) row++;
    if (t->count < HOF_MAX) t->count++;
    for (int i = t->count - 1; i > row; i--) t->e[i] = t->e[i - 1];
    memset(&t->e[row], 0, sizeof t->e[row]);
    memset(t->e[row].name, '.', HOF_NAME);
    t->e[row].score = score;
    time_t now = time(NULL);
    const struct tm *lt = localtime(&now);
    snprintf(t->e[row].date, sizeof t->e[row].date, "%02u-%02u-%04u", (unsigned)(lt->tm_mon + 1) % 100u,
             (unsigned)lt->tm_mday % 100u, (unsigned)(lt->tm_year + 1900) % 10000u);
    return row;
}

void hof_set_name(hof_table *t, int row, const char *name)
{
    int i = 0;
    for (; i < HOF_NAME && name[i]; i++) t->e[row].name[i] = name[i];
    for (; i < HOF_NAME; i++) t->e[row].name[i] = '.';
    t->e[row].name[HOF_NAME] = 0;
}

int hof_save(const hof_table *t)
{
    bo_setup s = { t->len, t->wid, t->dep, t->blockset, 0, 0 };
    int il, dl;
    unsigned char *idx = load_file("blscore.idx", &il), *dat = load_file("blscore.dat", &dl);
    if (il < IDX_HDR || memcmp(idx, VERSION, 9) != 0) {
        memcpy(idx, VERSION, 9);
        il = IDX_HDR;
        dl = 0;
    }
    if (dl < 0) dl = 0;
    int pos = IDX_HDR, found = 0;
    for (; pos + KEY_SIZE <= il; pos += KEY_SIZE) {
        int c = key_cmp(idx + pos, &s);
        if (c == 0) {
            found = 1;
            break;
        }
        if (c > 0) break;
    }
    int32_t off = found ? rd32(idx + pos + 8) : -1;
    if (found && (off < 0 || off > TABLE_SIZE * (MAX_TABLES - 1))) {
        /* a damaged or foreign index: drop the bad key and store the table anew */
        memmove(idx + pos, idx + pos + KEY_SIZE, (size_t)(il - pos - KEY_SIZE));
        il -= KEY_SIZE;
        found = 0;
    }
    if (found) {
        if (off + TABLE_SIZE > dl) { /* DAT shorter than the index says: extend */
            memset(dat + dl, 0, (size_t)(off + TABLE_SIZE - dl));
            dl = off + TABLE_SIZE;
        }
    } else {
        if (il + KEY_SIZE > IDX_HDR + KEY_SIZE * MAX_TABLES) {
            free(idx);
            free(dat);
            return 0;
        }
        off = dl;
        dl += TABLE_SIZE;
        memmove(idx + pos + KEY_SIZE, idx + pos, (size_t)(il - pos));
        il += KEY_SIZE;
        wr16(idx + pos, t->len);
        wr16(idx + pos + 2, t->wid);
        wr16(idx + pos + 4, t->dep);
        wr16(idx + pos + 6, t->blockset);
        wr32(idx + pos + 8, off);
    }
    encode_table(t, dat + off);
    int ok = store_write("blscore.dat", dat, dl) == dl && store_write("blscore.idx", idx, il) == il;
    free(idx);
    free(dat);
    return ok;
}
