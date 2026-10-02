/* Replay a scripted input file through the core and print a per-frame state log in the
   same format as re/emu/difftest.py prints for the original code. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/bo_core.h"

static int ticks_at(long f, int fps) { return (int)((double)f * BO_TICK_HZ / fps); }

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: replay script.txt\n");
        return 2;
    }
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror(argv[1]);
        return 1;
    }
    bo_setup s;
    int mode, fps, fast;
    unsigned seed, bios;
    int nfill = 0;
    static int fill[3 * BO_MAX_LEN * BO_MAX_WID * BO_MAX_DEP];
    static long kf[100000];
    static unsigned kk[100000];
    int nk = 0, ki = 0;
    if (fscanf(fp, "%hd %hd %hd %hd %hd %hd %d %d %d %u %u", &s.len, &s.wid, &s.dep, &s.blockset, &s.level,
               &s.rot_speed, &mode, &fps, &fast, &seed, &bios) != 11)
        goto bad;
    if (fps <= 0 || fps > 1000) goto bad;
    if (fscanf(fp, "%d", &nfill) != 1 || nfill < 0 || nfill > BO_MAX_LEN * BO_MAX_WID * BO_MAX_DEP) goto bad;
    for (int i = 0; i < 3 * nfill; i++)
        if (fscanf(fp, "%d", &fill[i]) != 1) goto bad;
    while (nk < 100000 && fscanf(fp, "%ld %x", &kf[nk], &kk[nk]) == 2) nk++;
    fclose(fp);

    static bo_game g;
    bo_srand(&g, (uint16_t)seed);
    bo_init(&g, &s, mode, fast, bios);
    for (int i = 0; i < nfill; i++) bo_fill_cell(&g, fill[3 * i], fill[3 * i + 1], fill[3 * i + 2]);
    long f = 0, max_frames = argc > 2 ? atol(argv[2]) : 2000000;
    while (g.state == BO_S_PLAY || g.state == BO_S_DROP_WAIT || g.state == BO_S_LAND_WAIT ||
           g.state == BO_S_DEMO) {
        if (++f > max_frames) break;
        for (int t = ticks_at(f - 1, fps); t < ticks_at(f, fps); t++) bo_tick(&g);
        while (ki < nk && kf[ki] == f) bo_key(&g, (uint16_t)kk[ki++]);
        printf("%ld %d", f, g.piece);
        for (int i = 0; i < 3; i++) printf(" %d %d %d", g.pose.a[i].axis, g.pose.a[i].sign, g.pose.a[i].pos);
        printf(" %d %ld %ld %d\n", g.countdown, (long)g.score, (long)g.cubes_played, g.level);
        bo_frame(&g);
        if (g.ev_cleared) fprintf(stderr, "clear %d at %ld\n", g.ev_cleared, f);
    }
    printf("end %d %ld %ld %d\n", g.state, (long)g.score, (long)g.cubes_played, g.level);
    return 0;
bad:
    fprintf(stderr, "%s: malformed script\n", argv[1]);
    fclose(fp);
    return 1;
}
