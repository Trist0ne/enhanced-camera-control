/* latch: the front camera is on or off because the driver said so, and for no other reason
 *.
 * Pure logic, no I/O: the host (camctl.c) feeds it what the truck reports and sends the requests it asks for.
 *
 * One thing is remembered: what the driver wants (on or off). Only these change it:
 *   - a press of the front camera button: the opposite of what was showing;
 *   - the first report after the ignition comes on (or after we start): what is showing is taken as wanted;
 *   - only with the option on: an off-road mode opening the camera (and that mode ending, if no press came between).
 * Whenever the camera module shows something else for longer than a moment, it is asked to show what is wanted.
 * Never more than 8 requests a minute without a press in between; past that it stands aside for two minutes.
 * A rear view is Ford's while Ford's own conditions for one hold: the gear is Reverse, or the truck has left Reverse
 * and is still under the 10 km/h at which SYNC ends it. Nothing is sent then; what was wanted comes back after it.
 * A rear view outside those conditions has no owner (SYNC lost track of it: it restarted on the move, say) and the
 * module ignores the button in it, so it is ended with the "off" SYNC itself sends, and a press during it counts.
 * With SYNC's speed limits lifted, the camera is closed just under the camera module's own limit (20 mph), and it
 * stays off (latch.c, MODULE_OFF); with them in force, SYNC closes it at its own, and it stays off too. Above the
 * limit a press opens nothing. Nothing ever comes back by itself after a limit.
 * A shift into Park ends it: wanted is off from then on, as SYNC itself
 * closes a front view on that shift. A press in Park opens it as anywhere else.
 *
 * It decides from two signals seen working on the truck: the module's view report and the button's
 * echo, and for Park the gear state SYNC's own camera program goes by (the notification CANShadow/GearStateEvent:
 * 0 Park, 1 Reverse, 2 Neutral, 3 Drive). The selector message is not used (it did not follow Reverse on the truck).
 * The echo can arrive a few
 * milliseconds after the view change it caused, so a change is judged only after a short wait. */
#ifndef LATCH_H
#define LATCH_H
#include <stdint.h>

enum { LT_ON = 1, LT_AUTO_OPEN = 2, LT_LAST_VIEW = 4 };   /* option bits (settings) */

struct latch {
    int opts;
    int run;                  /* ignition run/start (1), off (0), not known yet (-1) */
    int view, prev_view;      /* the module's view (0 off, 5 rear, 8 front, 9 front split), -1 unknown */
    uint64_t view_t;          /* when it last changed */
    /* the last time the camera went from off to a front view or back (normal <-> split is not such a change) */
    uint64_t sw_t;            /* when */
    uint64_t sw_prev_since;   /* when the state before it had begun */
    int sw_from_rear;         /* it was a rear view ending, not an on/off change */
    int view_by_us;           /* that change was the module doing what we had just asked */
    int view_unexpected;      /* that change was not what was wanted at the time (nothing had explained it yet) */
    int view_after_press;     /* that change came within the module's response time after a press (it was that press's) */
    int req_open;             /* a request of ours is out and not yet answered */
    int req_front;            /* ... for a front view (1) or for off (0) */
    int want;                 /* -1 not known yet, 0 off, 1 on */
    int target;               /* the front view wanted when on: 8 or 9 */
    int mode_owned;           /* on because the off-road mode opened it (option), untouched since */
    int mode_fresh;           /* the flag came on and nothing has happened since: its first opening is the mode's */
    int flag;                 /* the off-road camera flag */
    int gear;                 /* SYNC's gear state: -1 not known yet, 0 Park, 1 Reverse, 2 Neutral, 3 Drive */
    uint64_t flag_on_t, press_t, req_t;
    uint64_t hold_until;      /* nothing is sent before this */
    int tries;
    uint64_t sent_t[8];
    int sent_i;
    uint64_t pause_until;
    double speed;             /* km/h, -1 unknown */
    int lifted;               /* SYNC's own speed limits are lifted */
    int limit_done;           /* closed for the module's limit this time over it (once: not again at every report) */
    int limit_close;          /* the close that is out is the limit's: not counted as a fight */
    uint64_t bounce_t;        /* the host asked the module off to have a refused opening shown (lt_bounce); 0 none */
    int unseen;               /* the front view up now was refused by SYNC: not on the screen (lt_unseen) */
    /* a rear view outside Reverse (latch.c, "Whose a rear view is") */
    uint64_t orphan_t;        /* since when it has been up above SYNC's own limit for it; 0 not */
    uint64_t rear_press_t;    /* when the driver pressed during it; 0 not */
    int rear_tries;           /* "off" requests sent to end it */
    int last_view, last_view_changed;
};

void lt_init(struct latch *r, int opts, int last_view);
void lt_opts(struct latch *r, int opts, uint64_t t);   /* the settings changed */
void lt_press(struct latch *r, uint64_t t);            /* the button's echo went from 0 to 1 */
void lt_view(struct latch *r, int view, uint64_t t);   /* the module's view report (call on every report) */
void lt_flag(struct latch *r, int on, uint64_t t);
void lt_gear(struct latch *r, int gear, uint64_t t);   /* SYNC's gear state (0 Park, 1 Reverse, 2 Neutral, 3 Drive) */
void lt_ign(struct latch *r, int run, uint64_t t);
void lt_speed(struct latch *r, double kmh, uint64_t t);
void lt_lifted(struct latch *r, int lifted);
/* The host is asking the module "off" because SYNC refused the view of the driver's own press (camctl's gate; the
 * view report and the button's echo arrive within some 40 ms of each other, in either order). The moment the module
 * reports off it is asked for the view again: no wait for the module to act on a press, it already has. */
void lt_bounce(struct latch *r, uint64_t t);
/* The front view just reported was refused by SYNC's camera program (camctl's gate): it is on at the module and not
 * on the screen. Asking the module off for it is no fight anyone sees, so it is not counted towards standing aside:
 * an off-road mode can reopen the camera as often as it likes (every slow-down) and it stays off. */
void lt_unseen(struct latch *r);
/* 1 when a request should go now: feature 0x81b = *view (0 off). Call often (50 times a second). */
int lt_tick(struct latch *r, uint64_t t, int *view);

#endif
