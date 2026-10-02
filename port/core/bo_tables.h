/* Game tables extracted from the original BL2.OVL (see re/tools/gen_tables.py). */
#ifndef BO_TABLES_H
#define BO_TABLES_H

#include <stdint.h>

#define BO_NPIECES 41
#define BO_NLEVELS 11
#define BO_MAX_DEP 18

/* One axis of a piece orientation: out[i] = (sign ? +1 : -1) * cube[axis] + pos. */
typedef struct { int16_t axis, sign, pos; } bo_axis;
typedef struct { bo_axis a[3]; } bo_pose;

extern const int8_t  bo_piece_cubes[];              /* (x,y,z) triples */
extern const uint8_t bo_piece_ncubes[BO_NPIECES];
extern const uint8_t bo_piece_first[BO_NPIECES];    /* index of first cube triple */
extern const uint8_t bo_set_count[3];               /* FLAT, BASIC, EXTENDED */
extern const uint8_t bo_set_list[3][BO_NPIECES];
extern const bo_pose bo_rotations[6];               /* indexed by axis*2 + dir */
extern const int16_t bo_fall_delay[BO_NLEVELS];     /* BIOS ticks per row */
extern const int16_t bo_rot_steps[2][3];            /* [fast cpu][rotation speed] frames */
extern const int16_t bo_move_steps[2][3];
extern const int32_t bo_set_factor[3];
extern const int16_t bo_depth_factor[BO_MAX_DEP + 1];
extern const int16_t bo_piece_class[BO_NPIECES];
extern const int32_t bo_class_factor[10];
extern const int16_t bo_pit_max[3];
extern const int16_t bo_pit_min[3];
extern const int16_t bo_presets[3][4];              /* len, wid, dep, block set */
extern const int16_t bo_default_setup[6];           /* len, wid, dep, set, level, rot speed */

#endif
