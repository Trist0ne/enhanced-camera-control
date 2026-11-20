/* gate: SYNC's own camera program refuses a front view the driver did not ask for, before anything is drawn.
 *
 * When the camera module reports a front view, SS_RVC (SYNC 3.4; its code is the same in builds 22110 to 26057) decides
 * at 0x12765c whether to show it:
 *     0x127660  bl  0x10921c
 *     0x127664  cmp r0, #0
 *     0x127668  bne 0x127448        ; refused: nothing is shown (the path its own entry speed limit takes)
 * The gate turns that "bne" into "b" (one byte, 0x1a -> 0xea at 0x12766b) while the driver does not want the camera,
 * and back the moment a press says otherwise (the module reports the view 0.29 s after the button's echo). Only
 * front views (7 to 9) reach this place; a rear view never does.
 *
 * The same way as rvcpatch (see there): the code page is found in RAM by its content and written through a physical
 * mapping, in memory only. The mapping is kept, so a change is one byte stored and the instruction cache cleared.
 * Safety: the eight bytes there must be this build's (or ours) before every write; the page must be found once or
 * twice; a write that does not show in the running program is put back and the gate stays open until SYNC restarts.
 * A search that never finished three times in a row is not tried again (gate.state; an install removes it).
 * Stopped, or SYNC restarted: stock. */
#ifndef GATE_H
#define GATE_H

/* Starts looking for SYNC's camera program (its own thread, below the rule's priority). */
void gate_start(const char *dir);
void gate_set(int refuse);            /* what is wanted now; cheap, call on every decision */
int gate_refusing(void);              /* 1 while SYNC's camera program is refusing front views because of us */
void gate_stop(void);                 /* stock again */
/* One shot, for the installers: finds the program, sets it and returns (0 done or nothing to do). */
int gate_once(int refuse);
/* SYNC's camera screen as SS_RVC last set it: 2 on, 3 off; -1 not known (the program not found yet). Read only. */
int gate_screen(void);

#endif
