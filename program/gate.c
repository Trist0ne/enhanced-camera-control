/* gate.c: see gate.h. The memory part is rvcpatch.c's, kept apart so that program stays exactly as proven. */
#define _LARGEFILE64_SOURCE 1   /* mem_offset64 */
#include "gate.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct {
    unsigned long addr;
    unsigned char orig[8], ours[8];
    char name[48];
} site = { 0x127664, { 0x00, 0x00, 0x50, 0xe3, 0x76, 0xff, 0xff, 0x1a }, { 0x00, 0x00, 0x50, 0xe3, 0x76, 0xff, 0xff, 0xea }, "SS_RVC" };

static pthread_mutex_t gm = PTHREAD_MUTEX_INITIALIZER;
static int wanted;          /* refuse (1) or not (0), as last asked */
static int applied = -1;    /* what the program runs now: -1 not ours to say (not found, or given up) */
static volatile int quit;
static char state_path[256];

int gate_refusing(void) { return applied == 1; }

/* SYNC's camera screen, as SS_RVC last set it and keeps it (the status byte of the message it sends Ford's screen,
 * 0x147fbc + 1, written by Camera_Enable_Disable and the gear and IPC handlers; isHmiOpMsgChanged holds a change back
 * while the rear camera's delay after Reverse is on): 2 on, 3 off. Its data is laid out the same in builds 22110 to
 * 26057 (only a 16-byte build id differs). */
#define SCREEN_AT 0x147fbdUL

#include <devctl.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/procfs.h>
#include <sys/stat.h>
#include <sys/syspage.h>

enum { PG = 4096 };
static unsigned char *page[2];   /* the code page, mapped for writing (one or two copies of it in RAM) */
static int npage, asfd = -1;
static int quick, no_quick;      /* the page was found through the program's file (file_page); that way failed once */
static char exe_path[256];
static long pid = -1;

static void set_state(const char *s)
{
    if (!state_path[0]) return;
    FILE *f = fopen(state_path, "w");
    if (f) {
        fprintf(f, "%s\n", s);
        fclose(f);
    }
}

static long find_pid(void)   /* as rvcpatch: the process whose program's name is site.name */
{
    DIR *d = opendir("/proc");
    if (!d) return -1;
    struct dirent *e;
    long found = -1;
    while (found < 0 && (e = readdir(d))) {
        char *end, path[64];
        long n = strtol(e->d_name, &end, 10);
        if (*end || n <= 0) continue;
        snprintf(path, sizeof path, "/proc/%ld/as", n);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        struct { procfs_debuginfo i; char buf[256]; } dbg;
        memset(&dbg, 0, sizeof dbg);
        if (devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &dbg, sizeof dbg, 0) == EOK) {
            const char *base = strrchr(dbg.i.path, '/');
            if (!strcmp(base ? base + 1 : dbg.i.path, site.name)) found = n, snprintf(exe_path, sizeof exe_path, "%s", dbg.i.path);
        }
        close(fd);
    }
    closedir(d);
    return found;
}

static int rd(int fd, unsigned long a, void *b, size_t n)
{
    return lseek(fd, (off_t)a, SEEK_SET) == (off_t)a && read(fd, b, n) == (ssize_t)n ? 0 : -1;
}

static void drop(void)   /* (gm held) */
{
    for (int i = 0; i < npage; i++) munmap_device_memory(page[i], PG);
    npage = 0;
    if (asfd >= 0) close(asfd);
    asfd = -1;
    pid = -1;
    applied = -1;
    quick = 0;
}

/* (gm held) Makes the program refuse (1) or not (0). Every byte there is checked first, in each copy. */
static void apply(int refuse)
{
    if (!npage) return;
    const unsigned char *to = refuse ? site.ours : site.orig;
    unsigned off = (unsigned)(site.addr & (PG - 1));
    for (int i = 0; i < npage; i++)
        if (memcmp(page[i] + off, site.orig, 8) && memcmp(page[i] + off, site.ours, 8)) {
            drop();   /* (the program ended and its memory went to something else: looked for again) */
            return;
        }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < npage; i++) {
            for (int k = 0; k < 8; k++)
                if (page[i][off + k] != to[k]) page[i][off + k] = to[k];   /* (one byte differs: nothing half-written) */
            msync(page[i], PG, MS_SYNC | MS_INVALIDATE_ICACHE);
        }
        unsigned char back[8];
        int shows = !rd(asfd, site.addr, back, 8) && !memcmp(back, to, 8);   /* what the program itself now runs */
        if (shows && !pass) {
            applied = refuse;
            return;
        }
        if (pass) break;
        to = site.orig;
    }
    if (quick) {   /* found through the program's file, and it didn't take: put back (above); the search instead */
        no_quick = 1;
        drop();
        return;
    }
    set_state("refused");
    applied = -1;
    quit = 1;
}

/* where procfs says the mapping holding va (page-aligned) comes from: its object's offset for that page. 0 found */
static int map_offset(int fd, unsigned long va, uint64_t *off)
{
    int n = 0;
    if (devctl(fd, DCMD_PROC_MAPINFO, NULL, 0, &n) != EOK || n <= 0 || n > 4096) return -1;
    procfs_mapinfo *m = malloc((size_t)n * sizeof *m);
    int got = 0, rc = -1;
    if (m && devctl(fd, DCMD_PROC_MAPINFO, m, (int)(n * sizeof *m), &got) == EOK)
        for (int i = 0; i < got && i < n && rc; i++)
            if (m[i].vaddr <= va && va < m[i].vaddr + m[i].size) *off = (uint64_t)m[i].offset + (va - m[i].vaddr), rc = 0;
    free(m);
    return rc;
}

/* as rvcpatch: the program's page at pv the quick way, through its own file (SYNC's camera program runs in place from
 * an image in RAM: its file mapped here is that memory). Taken only if procfs has the program's mapping of the page
 * and ours coming from the same offset, and RAM there holds exactly the page. 0 found. */
static int file_page(int fd_as, unsigned long pv, const unsigned char *want, uint64_t *pa)
{
    char fpath[300];
    snprintf(fpath, sizeof fpath, "%s%s", exe_path[0] == '/' ? "" : "/", exe_path);
    int fd = open(fpath, O_RDONLY);
    struct stat st;
    if (fd < 0) return -1;
    if (fstat(fd, &st) || st.st_size < PG || st.st_size > (16 << 20)) {
        close(fd);
        return -1;
    }
    size_t len = (size_t)st.st_size & ~(size_t)(PG - 1);
    unsigned char *m = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return -1;
    int rc = -1;
    for (size_t o = 0; o < len && rc; o += PG) {
        if (m[o] != want[0] || memcmp(m + o, want, PG)) continue;
        off64_t p;
        size_t contig;
        uint64_t theirs, ours;
        int self = open("/proc/self/as", O_RDONLY);
        if (self >= 0 && !mem_offset64(m + o, NOFD, PG, &p, &contig) && !map_offset(fd_as, pv, &theirs)
            && !map_offset(self, (unsigned long)(m + o), &ours) && theirs == ours) {
            unsigned char *d = mmap_device_memory(NULL, PG, PROT_READ, 0, (uint64_t)p);
            if (d != MAP_FAILED) {
                if (!memcmp(d, want, PG)) *pa = (uint64_t)p, rc = 0;
                munmap_device_memory(d, PG);
            }
        }
        if (self >= 0) close(self);
    }
    munmap(m, len);
    return rc;
}

int gate_screen(void)
{
    unsigned char b = 0;
    pthread_mutex_lock(&gm);
    int ok = asfd >= 0 && !rd(asfd, SCREEN_AT, &b, 1);
    pthread_mutex_unlock(&gm);
    return ok && (b == 2 || b == 3) ? b : -1;
}

static uint64_t ram[16][2];
static int nram;
static int find_page(const unsigned char *want, uint64_t hit[2])   /* as rvcpatch */
{
    int hits = 0;
    for (int i = 0; i < nram; i++) {
        uint64_t end = ram[i][0] + ram[i][1];
        for (uint64_t s = (ram[i][0] + PG - 1) & ~(uint64_t)(PG - 1); s + PG <= end && !quit; s += 0x100000) {
            uint64_t len = end - s < 0x100000 ? (end - s) & ~(uint64_t)(PG - 1) : 0x100000;
            unsigned char *p = mmap_device_memory(NULL, (size_t)len, PROT_READ, 0, s);
            if (p == MAP_FAILED) continue;
            for (uint64_t o = 0; o < len; o += PG)
                if (p[o] == want[0] && !memcmp(p + o, want, PG)) {
                    if (hits < 2) hit[hits] = s + o;
                    hits++;
                }
            munmap_device_memory(p, (size_t)len);
        }
    }
    return hits;
}

/* Finds the program and its page. 0 found (and set as wanted), 1 never again this start, 2 not now.
 * `only_undo`: nothing to do if the program is stock (no search). */
static int locate(int only_undo)
{
    long p = find_pid();
    if (p < 0) return 2;
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/as", p);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 2;
    unsigned char now[8];
    static unsigned char whole[PG];
    unsigned long pv = site.addr & ~(unsigned long)(PG - 1);
    if (rd(fd, site.addr, now, 8) || rd(fd, pv, whole, PG)) {
        close(fd);
        return 2;
    }
    if (memcmp(now, site.orig, 8) && memcmp(now, site.ours, 8)) {
        close(fd);
        set_state("refused");
        return 1;
    }
    if (only_undo && !memcmp(now, site.orig, 8)) {
        close(fd);
        return 0;
    }
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        close(fd);
        return 1;
    }
    nram = 0;
    struct asinfo_entry *as = SYSPAGE_ENTRY(asinfo);
    int n = _syspage_ptr->asinfo.entry_size / sizeof *as;
    char *names = SYSPAGE_ENTRY(strings)->data;
    for (int i = 0; i < n && nram < 16; i++)
        if (!strcmp(names + as[i].name, "ram")) ram[nram][0] = as[i].start, ram[nram][1] = as[i].end + 1 - as[i].start, nram++;
    uint64_t hit[2];
    int hits, found_quick = 0;
    if (!no_quick && !file_page(fd, pv, whole, &hit[0])) hits = 1, found_quick = 1;
    else hits = find_page(whole, hit);
    if (quit) {
        close(fd);
        return 2;
    }
    if (hits < 1 || hits > 2) {
        close(fd);
        set_state("refused");
        return 1;
    }
    unsigned char *m[2];
    for (int i = 0; i < hits; i++) {
        m[i] = mmap_device_memory(NULL, PG, PROT_READ | PROT_WRITE, 0, hit[i]);
        if (m[i] == MAP_FAILED) {
            while (i--) munmap_device_memory(m[i], PG);
            close(fd);
            return 2;
        }
    }
    pthread_mutex_lock(&gm);
    drop();
    for (int i = 0; i < hits; i++) page[i] = m[i];
    npage = hits, asfd = fd, pid = p, quick = found_quick;
    apply(wanted);
    int ok = applied >= 0;
    pthread_mutex_unlock(&gm);
    return ok ? 0 : quit ? 1 : 2;
}

static void *gate_thread(void *arg)
{
    (void)arg;
    pthread_setschedprio(pthread_self(), 8);   /* the search reads all of RAM: below the rule and Ford's own jobs */
    /* a search that never finished (SYNC switched off in the middle is one way) is marked; three in a row: not again */
    int tries = 0;
    char s[32] = "";
    FILE *f = state_path[0] ? fopen(state_path, "r") : NULL;
    if (f) {
        if (!fgets(s, sizeof s, f)) s[0] = 0;
        fclose(f);
    }
    if (!strncmp(s, "searching", 9)) tries = atoi(s + 9) > 0 ? atoi(s + 9) : 1;
    if (tries >= 3 || !strncmp(s, "refused", 7)) {
        return NULL;
    }
    while (!quit) {
        pthread_mutex_lock(&gm);
        unsigned char b[8];
        if (npage && rd(asfd, site.addr, b, 8)) {   /* the program we found is gone */
            drop();
        }
        int have = npage;
        pthread_mutex_unlock(&gm);
        if (!have) {
            char mark[32];
            snprintf(mark, sizeof mark, "searching %d", tries + 1);
            set_state(mark);
            int rc = locate(0);
            if (rc == 1) return NULL;
            if (!quit) set_state(rc ? "waiting" : "armed");
        }
        for (int i = 0; i < 50 && !quit; i++) usleep(100000);
    }
    return NULL;
}

static pthread_t th;
static int started;

void gate_start(const char *dir)
{
    snprintf(state_path, sizeof state_path, "%s/gate.state", dir);
    started = !pthread_create(&th, NULL, gate_thread, NULL);
}

void gate_set(int refuse)
{
    refuse = !!refuse;
    pthread_mutex_lock(&gm);
    wanted = refuse;
    if (npage && applied != refuse && applied >= 0) apply(refuse);
    pthread_mutex_unlock(&gm);
}

void gate_stop(void)
{
    quit = 1;
    if (started) pthread_join(th, NULL);
    pthread_mutex_lock(&gm);
    wanted = 0;
    if (npage && applied == 1) apply(0);
    drop();
    pthread_mutex_unlock(&gm);
}

int gate_once(int refuse)
{
    wanted = !!refuse;
    int rc = locate(!refuse);
    if (rc == 2) rc = 0;   /* the program is not running: nothing of ours is in it */
    pthread_mutex_lock(&gm);
    drop();
    pthread_mutex_unlock(&gm);
    return rc;
}
