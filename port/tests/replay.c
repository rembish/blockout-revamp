/* Replay a scripted input file through the core and print a per-frame state log in the
   same format as re/emu/difftest.py prints for the original code. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/bo_core.h"

static int ticks_at(long f, int fps) { return (int)(f * BO_TICK_HZ / fps); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: replay script.txt\n"); return 2; }
    FILE *fp = fopen(argv[1], "r");
    if (!fp) { perror(argv[1]); return 1; }
    bo_setup s; int mode, fps, fast; unsigned seed, bios;
    if (fscanf(fp, "%hd %hd %hd %hd %hd %hd %d %d %d %u %u", &s.len, &s.wid, &s.dep,
               &s.blockset, &s.level, &s.rot_speed, &mode, &fps, &fast, &seed, &bios) != 11) return 1;
    static long kf[100000]; static unsigned kk[100000]; int nk = 0, ki = 0;
    while (nk < 100000 && fscanf(fp, "%ld %x", &kf[nk], &kk[nk]) == 2) nk++;

    static bo_game g;
    bo_srand(&g, (uint16_t)seed);
    bo_init(&g, &s, mode, fast, bios);
    long f = 0;
    while (g.state == BO_S_PLAY || g.state == BO_S_DROP_WAIT || g.state == BO_S_LAND_WAIT) {
        f++;
        for (int t = ticks_at(f - 1, fps); t < ticks_at(f, fps); t++) bo_tick(&g);
        while (ki < nk && kf[ki] == f) bo_key(&g, (uint16_t)kk[ki++]);
        printf("%ld %d", f, g.piece);
        for (int i = 0; i < 3; i++) printf(" %d %d %d", g.pose.a[i].axis, g.pose.a[i].sign, g.pose.a[i].pos);
        printf(" %d %ld %ld %d\n", g.countdown, (long)g.score, (long)g.cubes_played, g.level);
        bo_frame(&g);
        if (f > 2000000) break;
    }
    printf("end %d %ld %ld %d\n", g.state, (long)g.score, (long)g.cubes_played, g.level);
    return 0;
}
