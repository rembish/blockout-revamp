# BL2.OVL reverse-engineering notes

Addresses: code = offset in segment `1000` (load-relative 0), data = `ds:XXXX` (DGROUP at
load-relative paragraph `0x0f02`, image offset `0xf020`). Symbol names live in
`ghidra/names.txt`. Items marked **[verify]** still need a check in DOSBox-X.

## Binary layout

- Turbo C 2.0, small model. Startup `0000`, game code `0121–b324`, Turbo C RTL `b325–f012`.
- `main` (`03b2`) requires one argument from `BL.EXE`: 42 letters `a`–`p` encoding 21 bytes in
  nibbles: `[0]` BIOS video mode → `0040:0049`, `[2..3]` → `ds:44a6`, `[4..19]` → `ds:4e4e`
  (probably EGA palette), `[20]` → `ds:4e5e`.
- Top level is a setjmp/longjmp state machine: `setjmp(ds:3748)` result indexes the jump
  table at `ds:0264`; `goto_state(n)` (`0369`) longjmps.

| state | addr | meaning |
|---|---|---|
| 1 | 065a | change setup menu |
| 2 | 16f5 | demo / attract mode (AI player: `130e`, `162d`) |
| 3 | 4ec1 | controls help screen (spinning sample block) |
| 4 | 2cd1 | quit prompt |
| 5 | 58e5 | main menu |
| 6 | 6258 | play |
| 7 | 8907 | choose preset (Flat Fun / 3-D Mania / Out of Control) |
| 8 | 8c28 | game_mode=0 → 6 |
| 9 | 91e1 | game_mode=3 (practice) → 6 |

`game_mode` (`ds:0da4`): 0 game, 1 demo, 3 practice, 4 help-screen sample.

## Timing

- Only time source: BIOS INT 1Ch at the stock 18.2065 Hz (PIT not reprogrammed).
  ISR `919e`: `if (countdown>0) countdown--; ticks++; if (speaker_on) toggle port 61h bit 1`.
- Everything else is per-frame: the game loop runs as fast as rendering allows. Animation
  step counts are chosen by a CPU-speed probe (`5b5a`: tight-loop iterations per tick
  `>> 10`; class > 2 → "fast" table, i.e. anything faster than a 4.77 MHz XT).

### Fall delay per level (`ds:0f4e`, 11 levels, count at `ds:0f4c`), in ticks

| level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ticks | 100 | 63 | 40 | 25 | 16 | 10 | 6 | 4 | 3 | 2 | 1 |

### Animation steps (frames), indexed by rotation-speed setting SLOW/MEDIUM/FAST

| | slow CPU | fast CPU |
|---|---|---|
| rotation (`ds:117c`) | 10 / 3 / 2 | 14 / 7 / 3 |
| move (`ds:1188`) | 7 / 2 / 1 | 10 / 5 / 1 |

## Random pieces (`new_piece`, `3e94`)

- `srand(time(NULL))` once at startup (`6e3e`). Turbo C `rand()`:
  `seed = seed*0x015A4E35 + 1; return (seed>>16) & 0x7fff`.
- First candidate: `idx = set_list[set][ biostime_low_word % set_count ]` (unsigned).
- If the piece's bounding box doesn't fit the pit (largest extent > smallest pit dimension
  among the three), reroll with `idx = rand() % set_count` **used directly as a global piece
  index** (no set-list lookup), an original bug, kept.
- Block sets (`ds:2968` = {count, list ptr}):
  FLAT = {0,1,2,3,5,6,7,11}, BASIC = {2,5,6,7,8,9,10}, EXTENDED = 0..40.
- Piece table `ds:288a`: 41 × {ncubes, ptr to ncubes × (x,y,z) words}.

## Play loop (`st_play` 6258 → `55a7` per piece)

Per piece:
1. Level up: `thr = (int16)((len+wid) * (earned+1) * 15)`; if `cubes_played >= thr` and
   `earned+1 < 11` then `earned++`, `level = max(level, earned)`.
   `earned` (`ds:40ab`) starts at 0 regardless of the chosen starting level.
2. `countdown = fall_delay[level]` (practice: 0 = no gravity). `drop_height = 0`.
3. Frame loop:
   - advance move animation one step, advance rotation animation one step, redraw if changed;
   - drain **all** pending keys:
     - arrows / numpad (incl. diagonals Home/PgUp/End/PgDn) → `move(dx,dy,0)`
     - `Q W E A S D` (either case) → `rotate(0..5)`
     - Space → `drop_height = z`; hard drop; wait for animations; first time only
       `countdown = 4` (lock delay; piece can still be slid)
     - `P` pause (game mode 0 only), `O` sound toggle, Esc abort
   - if countdown still > 0: next frame;
   - else `countdown = fall_delay[level]` and, unless already dropped, `h--`; if any cube is
     still above `h` (spawn grace), skip; otherwise `move(0,0,-1)`; if blocked → land.
4. Land (`37cb`): `cubes_played += ncubes`; mark cells; per-layer counters; clear full layers
   (`52ae`); sounds; score (`8266`).

### Movement / rotation rules

- Orientation is a signed permutation (`ds:0de4`: 3 × {src axis, sign, offset}).
- `move()` (`5c24`) tests collision at the new position; on success starts a move
  animation of `move_steps` frames.
- `rotate(k)` (`78b2`) queues into a 3-slot ring (`ds:4e48` head, `ds:4e4c` tail):
  **a rotation is ignored if 2 are already pending**. The new orientation is tested with
  wall-kick (`2af2` with kick: out-of-bounds cubes push the piece back inside); fails only
  when blocked by settled cubes.
- Spawning uses the same kick test; if it fails → game over.

### Layer clear (`52ae`)

For z = 0..depth-1: if `layer_count[z] >= len*wid` → `n++`, shift everything above down,
re-check z. `n` in `ds:4e60`.

### Score (`8266`), only in modes 0/1/3

```
L = (level+1)*(level+17)
A = (n*n*X*L + 150*n) * depthK[depth] * setK[set] / 200
B = pit_empty ? setK*L*depthK*X / 62 : 0
C = ((pieceK[class[piece]] * L * (12*drop_height + depth)) << 3) / (depth*depth) / 40
score = (score + ((A+B+C) >> 1) + 1) % 1000000      (32-bit signed, Turbo C LDIV/LMOD)
```

- `setK` (`ds:2b04`, long): FLAT 1, BASIC 2, EXTENDED 4.
- `depthK` (`ds:2b56 + 2*depth`): 6→135, 7→125, 8→110, 9→105, 10→100, 11→95, 12→90,
  13→90, 14→85, 15→85, 16→80, 17→80, 18→75.
- `class` (`ds:2b10`, 41 words): 0,0,1,1,1,1,2,2,3,3,3,0,3,4,5,5,5,2,5,8,4,5,5,7,9,9,2,5,7,5,
  9,8,6,7,6,9,7,7,9,8,8. `pieceK[c] = 5*(c+1)` (`ds:2b7c`).
- `X` = long at `ds:4e62` = `len + wid`, set together with `score = 0` by `824a` at game
  start. (An early linear-sweep scan missed this write; use Ghidra xrefs, not linear
  disassembly, to find writers.)
- `drop_height` = piece z (`ds:0df4`) when Space was pressed, 0 if it just fell.
- `pit_empty` = bottom layer empty after clearing (`5276`).

## Demo / attract mode (`st_demo` 16f5)

Started from the menu (Demo) or after 0x444 ticks (60 s) idle in a menu. `game_init` in
mode 1, weights `w[z] = dep+1-z` (`0e24`), then per piece: `spawn_piece`, plan (`130e`),
perform (`162d`). No gravity, no `play_piece` (so no level-ups, no key flush, and
`drop_height` keeps whatever the last real game left in it).

- Plan: for each of 24 rotation sequences (`ds:0478`, `{n, rot×n}`), simulate the
  rotations with wall kicks, slide the piece to the low x/y corner (teleport if it is
  entirely above the stack), then sweep every x/y position, drop it and score it with
  `0ec3`: `-60` per completed layer, `+16·z` per cube, `+w[z]` per empty side neighbour,
  `+16·w[z]` if the cell below is empty; `w/4 - 7` on nearly full layers. A pruning
  threshold (starts at -4, set to the last drop depth, `-4` per sequence) rejects shallow
  drops. Lowest score wins.
- Perform: each rotation, then one-cell steps towards the target (the last step is a
  zero move, which still plays a full move animation), hard drop, land, each followed by
  `wait_animations`.
- Game over: wait 0x5b ticks or a key; with no key, a new demo starts.

## Hall of fame (`3c33`)

After every game in mode 0, including Esc aborts; not in practice or demo.
One table per (min(len,wid), max(len,wid), depth, block set), 10 entries, inserted
before the first strictly lower score (ties go below). Name up to 10 chars padded with
`.`, date `MM-DD-YYYY`. The tune (sound 3) plays during name entry; Esc discards it.
`BLSCORE.IDX` = `"05-26-89\0"` + sorted 12-byte keys `{len,wid,dep,set,int32 offset}`,
`BLSCORE.DAT` = 262-byte tables `{int16 n; 10×{name[11], int32 score, date[11]}}`.

## Sounds (`8be5`)

All blocking. Frequencies in Hz (`PIT divisor = 1193180/f`), durations in calibrated delay
units (~7.6 µs: the only value that puts the tune on B4/A4/F#4/E4).

| # | when | routine |
|---|---|---|
| 0 | layers cleared and pit empty | 5 warbles around 6000…3600 Hz |
| 1 | layers cleared | sweep 2700→50 Hz, step 220 |
| 2 | level up | fast warble 700↔5300 Hz ×2 |
| 3 | hall of fame entry | 8 PWM notes (periods 265/300/360/410 units) |

`8ffd` (a fifth sound) and the ISR speaker toggle are never used.
