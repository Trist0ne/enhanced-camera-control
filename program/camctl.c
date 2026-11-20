/* camctl: the front camera does what its button says. A package of its own: nothing
 * of the front-tire-guides package is needed, and nothing here reads or clears a module.
 *
  *   camctl [-d <data dir>] [-r]
 *
 * Listens to five of the truck's messages through SYNC's CAN shadow (the camera module's view, the button's echo,
 * the off-road camera flag, speed, ignition) and to the gear state SYNC's own camera program goes by (the
 * notification CANShadow/GearStateEvent), runs the latch (latch.c) and, when the camera module shows something
 * other than what the driver asked for, sends it the same 8-byte request SYNC's own programs send (feature 0x81b).
 * While the driver does not want the camera (and "G.O.A.T. Mode Auto Camera Launch" is off), SYNC's own camera
 * program is made to refuse a front view (gate.c): a mode's opening is then never drawn, not drawn and closed.
  * -r: puts SYNC's camera program back to stock and ends (the installers).
 *
 * Files in the data dir (default /fs/rwdata/camcontrol):
 *   settings.json   written by the switches on Ford's Camera Settings page; read twice a second
 *                   {"control":true,"speed":true,"auto":false,"last":true}; a missing key is its default
 *   lastview        the front view used last (8 or 9)
 *   rvcpatch.state  the speed-limit program's outcome ("lifted" ...), written by rvcpatch, read here
  *   gate.state      the gate's outcome ("armed", "waiting", "refused", "searching <n>") */
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "canshadow.h"
#include "gate.h"
#include "latch.h"
#include "mc.h"

#define M_IPMB 0x203EE    /* Personality_IPMB_Data: the camera module's feature reports */
#define M_PAC 0x20303     /* PAC_Send_Signals: byte 8 = the front camera button's echo */
#define M_GWM8 0x20275    /* GWM_Send_Signals_8: byte 7 = CamraFrntOffRd_B_Stat */
#define M_SPEED 0x20202   /* EngVehicleSpThrottle2: float at 4 = km/h */
#define M_BODY3 0x203B3   /* BodyInformation_3: byte 2 = Ignition_Status (4 run, 8 start) */

/* SYNC's notification service (NS_NPPService), as SYNC's camera program SS_RVC uses it (NPSubscribeToNotification
 * at 0x10b470): ask with 0x14 (data = the name, 64 bytes; source = our queue); a publication
 * comes to our queue as 0x13 (a kept value as 0x18) with the name in the header's sysinfo field. */
#define NP_SERVICE "NS_NPPService"
#define NP_SUBSCRIBE 0x14
#define NP_PUBLISHED 0x13
#define NP_STORED 0x18
#define N_GEAR "CANShadow/GearStateEvent"   /* data: s32. 0 Park, 1 Reverse, 2 Neutral, 3 and up Drive (SS_RVC 0x1267f4) */

static volatile sig_atomic_t stop;
static volatile int rx_ready, gear_asked;   /* our queue is open; the gear notification has been asked for */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct latch lt;
static char dir[200] = "/fs/rwdata/camcontrol";
static int echo_down = -1, flag = -1, ign = -1, gear = -1;
static uint64_t refused_t;   /* when SYNC's camera program refused a front view because of the gate (0: it has not) */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* one message struct from the CAN shadow (lock held) */
static void on_msg(uint32_t msg, const uint8_t *d, uint32_t len, uint64_t t)
{
    if (msg == M_IPMB && len >= 4) {
        unsigned le = d[2] | d[3] << 8, be = d[3] | d[2] << 8;   /* either byte order, as the program takes it */
        if (le != 0x81b && be != 0x81b) return;
        int view = d[0] ? d[0] : d[1];
        int is_front = view >= 7 && view <= 9, was_front = lt.view >= 7 && lt.view <= 9;
        int refused = is_front && !was_front && gate_refusing();   /* SYNC is not showing this one */
        if (refused) refused_t = t;
        if (!is_front) refused_t = 0;
        lt_view(&lt, view, t);
        if (refused) lt_unseen(&lt);
    } else if (msg == M_PAC && len > 8) {
        int down = d[8] == 1;
        if (down != echo_down) {
            if (down) lt_press(&lt, t);   /* (also when it is the first report we ever see) */
            echo_down = down;
        }
    } else if (msg == M_GWM8 && len > 7) {
        int on = d[7] == 1;
        if (on != flag) {
            flag = on;
            lt_flag(&lt, on, t);
        }
    } else if (msg == M_SPEED && len >= 8) {
        float f;
        memcpy(&f, d + 4, 4);
        if (f == f && f >= 0 && f < 400) {
            lt_speed(&lt, f, t);
        }
    } else if (msg == M_BODY3 && len > 2) {
        int run = d[2] == 4 || d[2] == 8;
        if (d[2] != ign) {
            ign = d[2];
            lt_ign(&lt, run, t);
        }
    }
}

/* After every decision (lock held): should SYNC's camera program refuse a front view now, and was one refused that
 * the driver did ask for? The second happens when a press's view report reaches us before its echo (on the truck
 * about one opening press in three, by up to 30 ms): the module then has the camera on and SYNC shows nothing. *bounce is set to have the module asked
 * "off"; the latch sees the camera taken away and asks it back, this time through an open gate. */
static int gate_wanted(uint64_t t, int *bounce)
{
    *bounce = 0;
    if (refused_t && lt.want == 1 && lt.view >= 7 && lt.view <= 9) {
        if (t - refused_t < 400) {
            *bounce = 1;
            lt_bounce(&lt, t);   /* the view is asked again the moment the module reports off (not 0.5 s after the press) */
        }
        refused_t = 0;
    }
    return (lt.opts & LT_ON) && !(lt.opts & LT_AUTO_OPEN) && lt.run == 1 && lt.want == 0 && t >= lt.pause_until;
}

/* the gear state, as SS_RVC reads it (Process_GearChange: 3 to 15 are all "not Park, not Reverse") (lock held) */
static void on_gear(int32_t v, uint64_t t)
{
    if (v < 0 || v > 15) return;          /* (-1 and the rest: SS_RVC's "unexpected gear value") */
    int g = v > 3 ? 3 : v;
    if (g == gear) return;
    gear = g;
    lt_gear(&lt, g, t);
}

/* 0 when the request went out. Asking again is harmless. */
static int ask_gear(const char *queue)
{
    char name[MC_NAME_LEN];
    memset(name, 0, sizeof name);
    strncpy(name, N_GEAR, sizeof name - 1);
    mc_handle tx = McOpenSender(NP_SERVICE);
    if (!tx) return -1;
    int st = McSend(tx, queue, NP_SUBSCRIBE, sizeof name, name);
    McClose(tx);
    return st ? -1 : 0;
}

/* The request SS_RVC and HMI_AL send the camera module through VS_CANShadow: {op 2, 0, feature 0x81b, value, 4, 0}.
 * "Off" as SS_RVC sends it (0x5269: seen obeyed from us on the truck), a view as the HMI's view buttons
 * send it (0x5458). */
static void send_view(const char *queue, int view)
{
    uint8_t b[8] = { 2, 0, 0x1b, 0x08, (uint8_t)view, 0, 4, 0 };
    mc_handle tx = McOpenSender(CS_SERVICE);
    if (!tx) return;
    McSend(tx, queue, view ? 0x5458 : 0x5269, sizeof b, b);
    McClose(tx);
}

/* The request for the truck's messages. 0 when it went out. */
static int ask_messages(const char *queue)
{
    static const uint32_t want[] = { M_IPMB, M_PAC, M_GWM8, M_SPEED, M_BODY3 };
    struct cs_request req;
    mc_handle tx = McOpenSender(CS_SERVICE);
    if (!tx) return -1;
    memset(&req, 0, sizeof req);
    for (unsigned i = 0; i < sizeof want / sizeof *want; i++) {
        req.e[i].msg = want[i];
        req.e[i].mode = 1;   /* whole message */
    }
    req.count = sizeof want / sizeof *want;
    int st = McSend(tx, queue, CS_CMD_SUBSCRIBE, sizeof req, &req);
    McClose(tx);
    return st ? -1 : 0;
}

/* Started with SYNC's own services (the early start), the request can go out before VS_CANShadow reads its queue and
 * is then lost: nothing would ever come, and the rule would never act. So while nothing at all has come since the
 * request it is sent again (the main loop: every 3 s for the first minute, then every 30 s; a sleeping truck sends
 * nothing either). Once anything has come, never. */
static volatile int got_any;

static void *cs_thread(void *arg)
{
    const char *queue = arg;
    static uint8_t buf[MC_HDR_SIZE + 4096], data[4096];
    while (!stop) {
        mc_handle rx = McOpenReceiver(queue);
        int st = rx ? ask_messages(queue) : 1;
        if (st) {
            if (rx) McClose(rx);
            sleep(5);
            continue;
        }
        gear_asked = 0;
        got_any = 0;
        rx_ready = 1;
        while (!stop) {
            char src[MC_NAME_LEN];
            uint32_t cmd = 0;
            if (McReceive(rx, src, &cmd, sizeof buf, buf)) break;
            if (cmd != NP_PUBLISHED && cmd != NP_STORED) got_any = 1;   /* (one of the truck's messages, not the gear's answer) */
            uint32_t len = McGetLength(buf);
            if (len > sizeof data || McGetDataOfSize(buf, data, sizeof data)) continue;
            int32_t g = -1;
            if (cmd == NP_PUBLISHED || cmd == NP_STORED) {
                char name[MC_NAME_LEN + 1];
                memset(name, 0, sizeof name);
                if (len < 4 || McGetSysInfoData(buf, name) || strcmp(name, N_GEAR)) continue;
                memcpy(&g, data, 4);
            }
            /* decided here and now, not at the main loop's next 20 ms turn: when something has to be undone,
             * every millisecond of it is on the screen */
            int view = 0, req, refuse, bounce;
            pthread_mutex_lock(&lock);
            uint64_t t = now_ms();
            if (cmd == NP_PUBLISHED || cmd == NP_STORED) on_gear(g, t);
            else on_msg(cmd, data, len, t);
            req = lt_tick(&lt, t, &view);
            refuse = gate_wanted(t, &bounce);
            pthread_mutex_unlock(&lock);
            gate_set(refuse);   /* before anything is asked of the module: its answer goes through the gate */
            if (bounce) send_view(queue, 0);
            if (req) send_view(queue, view);
        }
        rx_ready = 0;
        McClose(rx);
        sleep(1);
    }
    return NULL;
}

/* "key": true / false in the settings text; anything else (or no file) is the default */
static int setting(const char *text, const char *key, int dflt)
{
    char k[32];
    snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(text, k);
    if (!p) return dflt;
    p += strlen(k);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return *p == 't' ? 1 : *p == 'f' ? 0 : dflt;
}
static int read_small(const char *name, char *text, size_t cap)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    text[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(text, 1, cap - 1, f);
    text[n] = 0;
    fclose(f);
    return 0;
}
/* -1: the file is there but not whole (the settings page is writing it): keep what we have */
static int read_opts(void)
{
    char text[256];
    if (!read_small("settings.json", text, sizeof text) && !strchr(text, '}')) return -1;
    return (setting(text, "control", 1) ? LT_ON : 0) | (setting(text, "auto", 0) ? LT_AUTO_OPEN : 0)
           | (setting(text, "last", 1) ? LT_LAST_VIEW : 0);
}

static void on_signal(int s) { (void)s; stop = 1; }

int main(int argc, char **argv)
{
    const char *queue = "ENHANCED_CAMERA_CONTROL";
    int restore = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc) snprintf(dir, sizeof dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-r")) restore = 1;
        else {
            fprintf(stderr, "usage: camctl [-d data dir] [-r]\n");
            return 2;
        }
    }
    if (restore) return gate_once(0);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    mkdir(dir, 0755);

    char text[64];
    int last = 8, opts = read_opts();
    if (opts < 0) opts = LT_ON | LT_LAST_VIEW;
    if (!read_small("lastview", text, sizeof text) && atoi(text) == 9) last = 9;
    lt_init(&lt, opts, last);

    gate_start(dir);
    pthread_t th;
    pthread_create(&th, NULL, cs_thread, (void *)queue);

    uint64_t t_opts = 0, t_gear = 0, t_ask = 0;
    unsigned asks = 0;
    uint64_t t_screen = 0;
    while (!stop) {
        usleep(20000);
        /* the truck's messages asked for again while none has come (see got_any) */
        if (!rx_ready || got_any) t_ask = now_ms(), asks = 0;
        else if (now_ms() - t_ask >= (asks < 20 ? 3000u : 30000u)) {
            t_ask = now_ms(), asks++;
            ask_messages(queue);
        }
        /* the gear notification: asked for once our queue is open, every 2 s until the service takes the request
         * (it may still be starting), and once more 10 s later in case the gear was not announced yet */
        if (rx_ready && gear_asked < 2 && now_ms() - t_gear >= (gear_asked ? 10000 : 2000)) {
            t_gear = now_ms();
            int ok = !ask_gear(queue);
            if (ok) gear_asked++;
        }
        int view = 0, req, save = 0, refuse, bounce;
        /* SYNC's camera screen, from its camera program (read before taking the lock: the gate's own lock and ours are
         * never held together) */
        int screen = -2;
        if (now_ms() - t_screen >= 100) t_screen = now_ms(), screen = gate_screen();
        pthread_mutex_lock(&lock);
        uint64_t now = now_ms();   /* (after the lock: never older than a message's time, the times being unsigned) */
        if (screen != -2 && screen != lt.screen) {
            lt_screen(&lt, screen);
        }
        if (now - t_opts >= 500) {
            t_opts = now;
            int o = read_opts();
            if (o >= 0 && o != lt.opts) {
                lt_opts(&lt, o, now);
            }
            lt_lifted(&lt, !read_small("rvcpatch.state", text, sizeof text) && !strncmp(text, "lifted", 6));
            /* (no "the truck went quiet" rule: the CAN shadow only sends a message when it changes, so seconds
             * of silence are normal standing still. Ignition off arrives as a change like any other.) */
        }
        req = lt_tick(&lt, now, &view);
        refuse = gate_wanted(now, &bounce);
        if (lt.last_view_changed) lt.last_view_changed = 0, save = lt.last_view;
        pthread_mutex_unlock(&lock);
        gate_set(refuse);
        if (bounce) send_view(queue, 0);
        if (req) send_view(queue, view);
        if (save) {
            char path[256];
            snprintf(path, sizeof path, "%s/lastview", dir);
            FILE *f = fopen(path, "w");
            if (f) {
                fprintf(f, "%d\n", save);
                fclose(f);
            }
        }
    }
    gate_stop();
    return 0;
}
