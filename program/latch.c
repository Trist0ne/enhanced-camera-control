/* latch.c: see latch.h. */
#include "latch.h"

#include <string.h>

/* Timings of presses measured on a truck: the echo lasts 0.14 s; the module reported the camera on
 * 0.29 s after the echo, and off within 10 ms of it. On a truck the report came 3 ms BEFORE the echo. And
 * SYNC starts drawing the picture 0.05 s after the module's report, its popup 0.12 s after: an opening we undo is
 * seen for a moment whatever we do, so every wait here is as short as those figures allow. */
#define GRACE 100           /* ms an unexpected change waits for the button's echo before it is undone */
#define GRACE_MODE 40       /* shorter when the off-road flag has just come on (the mode's own opening). Not zero:
                             * someone who selects a mode and then presses the button would have that press's
                             * camera closed and reopened if its report came before its echo */
#define PRESS_WAIT_ON 500   /* ms the module gets to follow a press before we ask: to switch on (0.29 s seen) */
#define PRESS_WAIT_OFF 200  /* ... to switch off (0.01 s seen) */
#define PRESS_WAIT 700      /* a change this soon after a press belongs to the press */
#define BLIP 150            /* a picture up for less than this was not what the driver pressed at */
#define RESPONSE 350        /* the module answers a press within this (0.29 s seen): a change later than that after
                             * one press, with the next press's echo right behind it, is the next press's */
#define PRESS_BACK 400      /* a view change this soon before the echo was that press */
#define RETRY 1200          /* ms between requests for the same thing */
#define MAX_TRIES 3
#define OURS 600            /* a change this soon after a request of ours, and the way we asked, is ours (the module
                             * answered in 86 ms on the truck) */
#define ECHO_NEAR 100       /* an echo this soon after a view change belongs to it, whatever else was going on */
#define MODE_WINDOW 3000    /* an opening this soon after the off-road flag comes on is the mode's */
#define REAR_SETTLE 1500    /* after a rear view ends: Ford's own camera changes get this long */
#define PARK_WAIT 400       /* after a shift into Park: SYNC closes the camera itself (70 to 90 ms later) */
#define VIEW_SETTLE 100     /* after the camera comes on: before asking for the other front view */
/* Whose a rear view is. In Reverse it is Ford's. After Reverse SYNC keeps it up and ends it itself the moment the
 * truck passes 10 km/h (SS_RVC 0x109134, its "delay mode"), within a tenth of a second. So outside Reverse a rear
 * view above that speed should not exist: when one does, SYNC has lost track of it. Seen on a truck: SYNC restarted
 * on the move (its camera program reads no gear at its start: "received gear info ... but not processing"), the
 * module kept the rear view for three minutes at up to 70 km/h and ignored the front camera button throughout.
 * Such a view is ended with the "off" SYNC sends for it every day. Under that speed the module itself answers the
 * button by switching to the front view; if it has not by the time a press's wait is over, we end the rear view
 * for the press. */
#define REAR_LIMIT 10.0     /* km/h: SYNC's own limit for a rear view outside Reverse */
#define REAR_ORPHAN_MS 500  /* ms above it before the view counts as left behind (SYNC acts in under 100) */
#define RATE_N 8
#define RATE_WINDOW 60000
#define PAUSE 120000

static int front(int v) { return v == 1 || v == 7 || v == 8 || v == 9; }
static int rear(int v) { return v > 0 && !front(v); }
static int recent(uint64_t t, uint64_t at, uint64_t span) { return at && t >= at && t - at < span; }
/* The camera module's own limit, which nothing in SYNC can lift: it stops sending the picture at 32 km/h (20 mph)
 * while still reporting the view, and sends it again just under that (measured: lost at 31.9 and 32.1 km/h,
 * back at 29.8 to 32.4). Left up, SYNC shows a black picture and after 5 s "camera unavailable". So with SYNC's own
 * limits lifted the camera is closed 1 km/h under the module's limit and comes back 1 km/h under that, if it was on. */
#define MODULE_OFF 31.0     /* km/h: above this the camera is closed (still wanted) */
#define MODULE_ON 30.0      /* at or under this it comes back */
#define LIMIT_DWELL 2000    /* ms closed at least, so a speed held at the limit doesn't flutter the screen */
/* the limit in force: SYNC's own on front views (SS_RVC 0x10b858), or with those lifted the module's */
static int over_limit(const struct latch *r, double margin)
{
    if (r->speed < 0) return 0;
    if (r->lifted) return r->speed > MODULE_OFF - margin;
    return r->speed > (r->flag ? 24.0 : 10.0) - margin;
}
/* closed for the limit and slow enough again: back (2 km/h under SYNC's limit, so it doesn't flicker) */
static void release(struct latch *r, uint64_t t)
{
    if (!r->held) return;
    if (r->lifted ? (r->speed > MODULE_ON || t < r->limit_t + LIMIT_DWELL) : over_limit(r, 2)) return;
    r->held = 0;
    r->tries = 0;
    r->limit_reopen = r->lifted;
    if (r->hold_until < t) r->hold_until = t;
}

void lt_init(struct latch *r, int opts, int last_view)
{
    memset(r, 0, sizeof *r);
    r->opts = opts;
    r->run = -1;
    r->view = r->prev_view = -1;
    r->want = -1;
    r->speed = -1;
    r->gear = -1;
    r->last_view = last_view == 9 ? 9 : 8;
    r->target = r->last_view;
}

static void forget(struct latch *r)   /* nothing wanted until the next report says what is showing */
{
    r->want = -1;
    r->mode_owned = r->held = r->tries = 0;
    r->hold_until = 0;
}

void lt_opts(struct latch *r, int opts, uint64_t t)
{
    (void)t;
    if (opts == r->opts) return;
    int was = r->opts;
    r->opts = opts;
    if ((opts ^ was) & LT_ON) {
        forget(r);
        if ((opts & LT_ON) && r->view >= 0 && !rear(r->view)) r->want = front(r->view);   /* as it is now */
    }
    if (!(opts & LT_AUTO_OPEN)) r->mode_owned = 0;
}

void lt_press(struct latch *r, uint64_t t)
{
    if (!(r->opts & LT_ON)) return;
    if (recent(t, r->press_t, 60)) return;                   /* one press, seen twice (the echo lasts 0.15 s) */
    r->press_t = t;
    /* the driver is at the button: whatever we were counting or sitting out starts afresh (the limit is against
     * a fight with the module that nobody is watching) */
    memset(r->sent_t, 0, sizeof r->sent_t);
    r->pause_until = 0;
    if (r->run == 0) return;
    if (r->view < 0) return;                                 /* unknown: wait for a report */
    if (rear(r->view)) {
        /* In Reverse: Ford's. Outside it the press asks for the front camera; the module usually switches by itself
         * (then lt_view takes its answer as wanted), and lt_tick sees to it if it has not */
        if (r->gear != 1 && r->gear >= 0) r->rear_press_t = t;
        return;
    }
    /* this press just ended a rear view (the echo right behind the change; a rear view that ended by itself a
     * moment ago is not this press's doing): the module's answer to it is what the driver asked for */
    if (rear(r->prev_view) && recent(t, r->view_t, ECHO_NEAR)) {
        r->want = front(r->view);
        r->mode_owned = r->mode_fresh = r->held = r->tries = 0;
        return;
    }
    /* what the driver was looking at when pressing: the module may already have acted on this press. A change is
     * not the press's if it was the module answering us, unless the echo is right behind it (a request of ours that
     * the module was ignoring must not make the driver's own press look like our doing) */
    int shown = front(r->view);
    /* This press caused the last change if nothing had explained that change (it was not what was wanted), or if
     * the echo is right behind it and no earlier press was (so: not the second press of two; but yes a press
     * that did what a request of ours was failing to do). */
    if (!r->sw_from_rear && recent(t, r->sw_t, PRESS_BACK)
        && (t - r->sw_t <= ECHO_NEAR ? (r->view_unexpected || !r->view_after_press)
                                       : (r->view_unexpected && !r->view_after_press))   /* (not an earlier press's late doing) */
        /* (the state before that change is the opposite of what shows now) */
        /* ... and what showed before it had been up long enough to be seen (
         * a request of ours answered in the same instant as a press, the module then toggling it away) */
        && !(r->sw_prev_since && r->sw_t - r->sw_prev_since < BLIP)) {
        shown = !shown;
    }
    r->mode_owned = r->mode_fresh = 0;
    if (!shown && r->held && r->want == 1) {
        /* closed for the speed limit and due back when slower: to the driver it is still on, so the press switches
         * it off, and it does not come back (at any speed: what the driver pressed for is the return) */
        r->want = 0;
        r->held = r->tries = r->limit_reopen = 0;
        r->hold_until = t + PRESS_WAIT_OFF;
        return;
    }
    if (!(r->lifted && over_limit(r, 0))) r->held = 0;
    if (!shown && over_limit(r, 0)) {
        /* (the camera off and the driver asking for it above the limit: nothing to show) */
        return;
    }
    r->want = !shown;
    r->tries = 0;
    if (r->want) r->target = (r->opts & LT_LAST_VIEW) ? r->last_view : 8;
    r->hold_until = t + (r->want ? PRESS_WAIT_ON : PRESS_WAIT_OFF);
}

void lt_view(struct latch *r, int view, uint64_t t)
{
    int old = r->view;
    if (view == old) return;
    r->prev_view = old;
    r->view = view;
    r->view_t = t;
    /* ours only if it is what we asked for and that request was still unanswered: a change the other way, or a
     * second change after the module had already followed us, is somebody else's (a
     * press 1.2 s after a request of ours was taken for our own doing) */
    if (!front(view)) r->unseen = 0;
    int by_us = r->req_open && recent(t, r->req_t, OURS) && front(view) == r->req_front;
    if (by_us || !recent(t, r->req_t, OURS)) r->req_open = 0;
    if (old < 0 || rear(view) || front(view) != front(old)) {   /* on, off, or to and from a rear view: not 8 <-> 9 */
        r->sw_prev_since = r->sw_t;
        r->sw_t = t;
        r->sw_from_rear = old < 0 || rear(old) || rear(view);
        r->view_by_us = by_us;
        r->view_after_press = recent(t, r->press_t, RESPONSE);
        r->view_unexpected = r->want >= 0 && !r->sw_from_rear && front(view) != r->want;
    }
    if (!(r->opts & LT_ON) || r->run == 0) return;
    if (rear(view)) return;                                  /* Ford's; what is wanted stays as it was */
    if (r->want < 0) {                                       /* the first report: what shows is what is wanted */
        r->want = front(view);
        if (front(view) && (view == 8 || view == 9)) r->target = view;
        return;
    }
    if (rear(old) && by_us) {                                /* a rear view outside Reverse, ended by us (lt_tick) */
        r->tries = 0;
        r->hold_until = t;                                   /* what is wanted now, without Ford's settling time */
        return;
    }
    if (rear(old) && recent(t, r->press_t, PRESS_WAIT)) {    /* ended by a press: its result is what was asked for */
        r->want = front(view);
        r->tries = 0;
        return;
    }
    if (t < r->pause_until) {                                /* standing aside: what shows is what is wanted */
        r->want = front(view);
        return;
    }
    if (rear(old)) {                                         /* a rear view ended */
        if (r->hold_until < t + REAR_SETTLE) r->hold_until = t + REAR_SETTLE;
        r->tries = 0;
        return;
    }
    if (front(view) && front(old)) {                         /* normal <-> split: the driver's choice on the screen */
        if ((view == 8 || view == 9) && !by_us) {
            r->target = view;
            if (view != r->last_view) r->last_view = view, r->last_view_changed = 1;
        }
        return;
    }
    if (front(view) == r->want) {                            /* as wanted (the module followed a press, or us) */
        r->tries = 0;
        if (front(view) && r->target != view && (r->opts & LT_LAST_VIEW)) r->hold_until = t + VIEW_SETTLE;
        return;
    }
    /* not what is wanted */
    if (!front(view) && recent(t, r->bounce_t, 1000)) {      /* off as the host asked (lt_bounce): the view again, now */
        r->bounce_t = 0;
        r->tries = 0;
        r->hold_until = t;
        return;
    }
    if (!front(view) && r->held && r->lifted) { r->tries = 0; return; }   /* closed for the module's limit: as meant */
    int by_mode = r->flag && r->mode_fresh && recent(t, r->flag_on_t, MODE_WINDOW);
    if (front(view) && (r->opts & LT_AUTO_OPEN) && by_mode) {
        r->want = 1;
        r->mode_owned = 1;
        r->mode_fresh = 0;
        r->target = (r->opts & LT_LAST_VIEW) ? r->last_view : view;
        r->tries = 0;
        return;
    }
    if (!front(view) && over_limit(r, 0)) {
        r->held = 1;
        return;
    }
    /* the short wait only when the flag has just come on (the mode's own opening, expected right then). Later in the mode
     * a press must get its 0.1 s like anywhere else: the report can come before the echo */
    uint64_t wait = t + (front(view) && r->flag && r->mode_fresh && recent(t, r->flag_on_t, MODE_WINDOW) ? GRACE_MODE : GRACE);
    if (front(view)) r->mode_fresh = 0;
    if (r->hold_until < wait) r->hold_until = wait;
    r->tries = 0;
    if (front(old) && (old == 8 || old == 9)) r->target = old;
}

void lt_flag(struct latch *r, int on, uint64_t t)
{
    if (on && !r->flag) {
        r->flag_on_t = t;
        r->mode_fresh = 1;
        /* the opening it caused may have been reported a moment before it (as the echo trails the view report) */
        if ((r->opts & LT_ON) && (r->opts & LT_AUTO_OPEN) && r->want == 0 && front(r->view) && !r->view_by_us
            && !r->sw_from_rear && recent(t, r->sw_t, GRACE) && !recent(t, r->press_t, PRESS_WAIT)) {
            r->want = 1;
            r->mode_owned = 1;
            r->mode_fresh = 0;
            r->tries = 0;
        }
    }
    if (!on && r->flag && r->mode_owned && (r->opts & LT_ON)) {   /* the mode that opened it ended, untouched */
        r->mode_owned = 0;
        r->want = 0;
        r->tries = 0;
        r->hold_until = t + PRESS_WAIT;                      /* the module usually closes it itself */
    }
    r->flag = on;
}

/* A shift into Park closes the front camera. SYNC's camera program sends the module "off" on that
 * shift by itself (Camera_Enable_Disable, seen twice); here it stops being wanted, so nothing brings it
 * back, and if SYNC's own close did not come we ask for it. Only a shift counts: Park as the first gear we hear of
 * (the truck started in Park, a press opened the camera) changes nothing. */
void lt_gear(struct latch *r, int gear, uint64_t t)
{
    int old = r->gear;
    if (gear == old) return;
    r->gear = gear;
    if (gear != 0 || old < 0) return;
    if (!(r->opts & LT_ON) || r->run == 0 || r->want < 0) return;
    r->want = 0;
    r->mode_owned = r->mode_fresh = r->held = r->tries = 0;
    r->view_unexpected = 0;                                  /* the close that goes with it is explained: not a press's */
    if (r->hold_until < t + PARK_WAIT) r->hold_until = t + PARK_WAIT;
}

void lt_ign(struct latch *r, int run, uint64_t t)
{
    (void)t;
    if (run == r->run) return;
    if (run && (r->opts & LT_ON)) {                          /* a new drive: what shows now is what is wanted */
        forget(r);
        if (r->view >= 0 && !rear(r->view)) r->want = front(r->view);
    }
    if (!run) forget(r);
    r->run = run;
}

void lt_speed(struct latch *r, double kmh, uint64_t t)
{
    r->speed = kmh;
    /* SYNC's limits lifted and the camera up (or about to be): at the module's limit it is closed, still wanted */
    if (r->lifted && !r->held && (r->opts & LT_ON) && r->run == 1 && r->want == 1 && over_limit(r, 0)) {
        r->held = 1;
        r->limit_t = t;
        r->tries = 0;
        r->hold_until = t;                                   /* now: whatever wait was running, the picture is about to go */
    }
    release(r, t);
}

void lt_unseen(struct latch *r) { if (front(r->view)) r->unseen = 1; }

void lt_bounce(struct latch *r, uint64_t t)
{
    r->bounce_t = t;
    r->req_t = t, r->req_open = 1, r->req_front = 0;         /* the "off" that follows is ours, not a press's */
}

void lt_lifted(struct latch *r, int lifted)
{
    if (lifted && !r->lifted) r->held = 0;
    r->lifted = lifted;
}

int lt_tick(struct latch *r, uint64_t t, int *view)
{
    if (!(r->opts & LT_ON) || r->run != 1 || r->view < 0) return 0;
    if (rear(r->view)) {
        /* Reverse (or the gear not known yet): Ford's */
        if (r->gear == 1 || r->gear < 0) { r->orphan_t = r->rear_press_t = 0; r->rear_tries = 0; return 0; }
        if (r->speed <= REAR_LIMIT) r->orphan_t = 0;
        else if (!r->orphan_t) r->orphan_t = t;
        int orphan = r->orphan_t && t - r->orphan_t >= REAR_ORPHAN_MS;
        int pressed = r->rear_press_t && t - r->rear_press_t >= PRESS_WAIT_ON;
        if ((!orphan && !pressed) || r->rear_tries >= MAX_TRIES || t < r->hold_until) return 0;
        if (pressed && !over_limit(r, 0)) {                  /* the press asked for the front camera */
            r->want = 1;
            r->target = (r->opts & LT_LAST_VIEW) ? r->last_view : 8;
            r->mode_owned = r->mode_fresh = r->held = 0;
        }
        r->rear_tries++;
        r->req_t = t, r->req_open = 1, r->req_front = 0;
        r->hold_until = t + RETRY;
        *view = 0;
        return 1;
    }
    r->orphan_t = r->rear_press_t = 0, r->rear_tries = 0;
    if (r->want < 0) return 0;
    release(r, t);                                           /* (the time closed is part of it: not only on a new speed) */
    if (t < r->hold_until || t < r->pause_until || rear(r->view)) return 0;
    int v, for_limit = 0;
    if (r->held) {
        if (!(r->lifted && front(r->view) && r->tries < MAX_TRIES)) return 0;
        if (r->speed >= 0 && r->speed <= MODULE_ON && r->want == 1) {   /* up again (the module's doing) and slow
                                                              * enough: closing it to sit out the dwell would flicker */
            r->held = r->tries = r->limit_reopen = 0;
            return 0;
        }
        v = 0, for_limit = 1;                                /* above the module's limit with the camera up: closed */
    } else if (!r->want) {
        if (!front(r->view)) return 0;                       /* as wanted */
        v = 0;
    } else {
        int tgt = r->target == 9 ? 9 : 8;
        if (r->view == tgt) return 0;                        /* as wanted */
        if (front(r->view)) {                                /* on, in the other front view */
            if (!(r->opts & LT_LAST_VIEW) || r->tries >= 2) { r->target = r->view; return 0; }
        } else if (over_limit(r, 0)) {
            r->held = 1;                                     /* nothing would show: wait to be under the limit */
            r->limit_t = t;
            return 0;
        }
        v = tgt;
    }
    if (r->tries >= MAX_TRIES) {
        r->want = front(r->view);                            /* what shows is taken as wanted: the next press toggles it */
        r->tries = 0;
        return 0;
    }
    /* (closing for the limit and coming back after it are the speed's doing, not a fight with the module: not
     * counted, and at most one pair every LIMIT_DWELL) */
    /* (nor is closing an opening SYNC refused to show: nothing of it is on the screen, and an off-road mode makes
     * one at every slow-down) */
    int counted = !for_limit && !(r->limit_reopen && v != 0) && !(v == 0 && r->unseen);
    r->limit_reopen = 0;
    uint64_t oldest = r->sent_t[r->sent_i];
    if (counted && oldest && t - oldest < RATE_WINDOW) {
        r->pause_until = t + PAUSE;
        r->want = front(r->view);
        r->tries = 0;
        return 0;
    }
    if (counted) {
        r->sent_t[r->sent_i] = t;
        r->sent_i = (r->sent_i + 1) % RATE_N;
    }
    r->tries++;
    r->req_t = t;
    r->req_open = 1;
    r->req_front = v != 0;
    r->hold_until = t + RETRY;
    *view = v;
    return 1;
}
