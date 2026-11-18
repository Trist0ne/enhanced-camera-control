/* rvcpatch: lifts SYNC's own speed limits on the front camera, in the running camera program (SS_RVC), in memory
 * only. Seeing ahead is a safety matter in every mode.
 *
  *   rvcpatch [-u] [-loop] [-name <program>] [-places <file>] [-state <file>]
 *
 * Three places in SS_RVC (SYNC 3.4, a fixed-address program whose code is the same in builds 22110 to 26057):
 *   0x10b680 isIPMBFeatureMaxSpeedExceeded_Entry  front view refused above 10 km/h (20 off-road): -> "not exceeded"
 *   0x10b858 isIPMBFeatureMaxSpeedExceeded        front view switched off above 10 km/h (24 off-road): -> "not exceeded"
 *   0x122a58 the off-road flag's handler           front view switched off when the flag drops above 10 km/h:
 *                                                 its call of the generic 10 km/h check -> "not exceeded"
 * Only front views reach these (views 7..9). The generic check itself (0x109134) is left alone: the rear camera's
 * "stays on after Reverse until 10 km/h" uses it.
 *
 * How: QNX 6.5 refuses writes into a
 * program's code through /proc/<pid>/as, stopped or not. So each code page is read through /proc/<pid>/as, found by
 * its whole 4 kB content in the RAM ranges the system page lists, written through a physical mapping, and the
 * instruction cache invalidated; then read back through /proc/<pid>/as. Found once (the boot image's page, run in
 * place) or twice (that and a copy the program runs from): every copy is written, both being SS_RVC's own code.
 *
 * Safety: every byte is checked first; if any place doesn't hold exactly this build's bytes (or ours), nothing is
 * written. If a page isn't found exactly once, nothing is written. If a read-back differs, everything is put back.
 * A run that dies mid-way leaves the state file saying "searching": no later run tries again until it is removed.
 * Nothing is written to flash: a SYNC restart is stock; -u puts the original bytes back now. -loop checks every
  * 30 s (a restarted SS_RVC is patched again). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <devctl.h>
#include <sys/neutrino.h>
#include <sys/procfs.h>
#include <sys/syspage.h>

struct place {
    unsigned long addr;
    unsigned char orig[8], ours[8];
    char what[48];
};

#define MOV_R0_0 0x00, 0x00, 0xa0, 0xe3   /* mov r0, #0 */
#define BX_LR 0x1e, 0xff, 0x2f, 0xe1      /* bx lr */
static struct place places[8] = {
    { 0x10b680, { 0xf0, 0x45, 0x2d, 0xe9, 0xac, 0x41, 0x9f, 0xe5 }, { MOV_R0_0, BX_LR }, "front view entry limit" },
    { 0x10b858, { 0xf0, 0x45, 0x2d, 0xe9, 0x98, 0x41, 0x9f, 0xe5 }, { MOV_R0_0, BX_LR }, "front view speed shut-off" },
    { 0x122a58, { 0xb5, 0x99, 0xff, 0xeb, 0x08, 0x00, 0x50, 0xe1 }, { MOV_R0_0, 0x08, 0x00, 0x50, 0xe1 }, "off-road flag drop" },
};
static int nplaces = 3;

static const char *state_path, *name = "SS_RVC";
enum { PG = 4096 };

static void set_state(const char *s)
{
    static char last[32];
    if (!state_path || !strcmp(last, s)) return;   /* only a change is written (flash) */
    snprintf(last, sizeof last, "%s", s);
    FILE *f = fopen(state_path, "w");
    if (f) {
        fprintf(f, "%s\n", s);
        fclose(f);
    }
}

/* ---- physical memory: QNX's own calls ---- */
static uint64_t ram[16][2];   /* start, length */
static int nram;
static int phys_init(void)
{
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) return -1;
    nram = 0;   /* called again for every patch in a -loop run: the list must not grow (each page found twice over) */
    struct asinfo_entry *as = SYSPAGE_ENTRY(asinfo);
    int n = _syspage_ptr->asinfo.entry_size / sizeof *as;
    char *names = SYSPAGE_ENTRY(strings)->data;
    for (int i = 0; i < n && nram < 16; i++)
        if (!strcmp(names + as[i].name, "ram")) ram[nram][0] = as[i].start, ram[nram][1] = as[i].end + 1 - as[i].start, nram++;
    return nram ? 0 : -1;
}
static unsigned char *phys_map(uint64_t pa, size_t len, int rw)
{
    void *p = mmap_device_memory(NULL, len, PROT_READ | (rw ? PROT_WRITE : 0), 0, pa);
    return p == MAP_FAILED ? NULL : p;
}
static void phys_unmap(unsigned char *p, size_t len) { munmap_device_memory(p, len); }
static void phys_sync(unsigned char *p, size_t len) { msync(p, len, MS_SYNC | MS_INVALIDATE_ICACHE); }

/* The physical pages whose 4 kB equal `page` (up to 2 kept in hit[]); returns how many there are. */
static int find_page(const unsigned char *page, uint64_t hit[2])
{
    int hits = 0;
    for (int i = 0; i < nram; i++) {
        uint64_t end = ram[i][0] + ram[i][1];
        for (uint64_t s = (ram[i][0] + PG - 1) & ~(uint64_t)(PG - 1); s + PG <= end; s += 0x100000) {
            uint64_t len = end - s < 0x100000 ? (end - s) & ~(uint64_t)(PG - 1) : 0x100000;
            unsigned char *p = phys_map(s, (size_t)len, 0);
            if (!p) continue;
            for (uint64_t o = 0; o < len; o += PG)
                if (p[o] == page[0] && !memcmp(p + o, page, PG)) {
                    if (hits < 2) hit[hits] = s + o;
                    hits++;
                }
            phys_unmap(p, (size_t)len);
        }
    }
    return hits;
}

/* SS_RVC's pid: the process whose executable's name is `name` (QNX: /proc/<pid>/exefile holds its path). */
static long find_pid(const char *proc)
{
    DIR *d = opendir(proc);
    if (!d) return -1;
    struct dirent *e;
    long pid = -1;
    while (pid < 0 && (e = readdir(d))) {
        char *end;
        long n = strtol(e->d_name, &end, 10);
        if (*end || n <= 0) continue;
        char path[300], exe[256] = "";
        /* the program's path, as pidin finds it (DCMD_PROC_MAPDEBUG_BASE on the process's base address) */
        snprintf(path, sizeof path, "%s/%s/as", proc, e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        struct { procfs_debuginfo i; char buf[256]; } dbg;
        memset(&dbg, 0, sizeof dbg);
        if (devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &dbg, sizeof dbg, 0) == EOK) snprintf(exe, sizeof exe, "%s", dbg.i.path);
        close(fd);
        const char *base = strrchr(exe, '/');
        if (!strcmp(base ? base + 1 : exe, name)) pid = n;
    }
    closedir(d);
    return pid;
}

static int rd(int fd, unsigned long a, void *b, size_t n)
{
    return lseek(fd, (off_t)a, SEEK_SET) == (off_t)a && read(fd, b, n) == (ssize_t)n ? 0 : -1;
}

static int search_tries, write_fails;

/* 0 done (or nothing to do), 1 refused (unexpected bytes, page not found once: never retried), 2 failed (retry) */
static int once(const char *proc, int undo, long *last_pid, int *last_state)
{
    long pid = find_pid(proc);
    if (pid < 0) {
        *last_state = 3;
        return 2;
    }
    char path[300];
    snprintf(path, sizeof path, "%s/%ld/as", proc, pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 2;
    }
    unsigned char now[8][8];
    int n_orig = 0, n_ours = 0;
    for (int i = 0; i < nplaces; i++) {
        if (rd(fd, places[i].addr, now[i], 8)) {
            close(fd);
            return 2;
        }
        if (!memcmp(now[i], places[i].orig, 8)) n_orig++;
        else if (!memcmp(now[i], places[i].ours, 8)) n_ours++;
        else {
            close(fd);
            set_state("refused");
            return 1;
        }
    }
    int want_ours = !undo;
    if ((want_ours && n_ours == nplaces) || (!want_ours && n_orig == nplaces)) {
        if (pid != *last_pid || *last_state != want_ours)
        *last_pid = pid;
        *last_state = want_ours;
        set_state(want_ours ? "lifted" : "stock");
        close(fd);
        return 0;
    }
    if (phys_init()) {
        close(fd);
        return 2;
    }
    /* each place's page, found in RAM by its content: pa[i][0..npa[i]) */
    uint64_t pa[8][2];
    int npa[8];
    char srch[32];
    snprintf(srch, sizeof srch, "searching %d", search_tries + 1);   /* (see main: three unfinished searches at most) */
    set_state(srch);
    for (int i = 0; i < nplaces; i++) {
        unsigned long pv = places[i].addr & ~(unsigned long)(PG - 1);
        int same = -1;
        for (int j = 0; j < i; j++)
            if ((places[j].addr & ~(unsigned long)(PG - 1)) == pv) same = j;
        if (same >= 0) {
            npa[i] = npa[same];
            for (int k = 0; k < npa[i]; k++) pa[i][k] = (pa[same][k] & ~(uint64_t)(PG - 1)) + (places[i].addr - pv);
            continue;
        }
        static unsigned char page[PG];
        if (rd(fd, pv, page, PG)) {
            close(fd);
            set_state("stock");
            return 2;
        }
        uint64_t hit[2];
        int hits = find_page(page, hit);
        if (hits < 1 || hits > 2) {
            close(fd);
            set_state("refused");
            return 1;
        }
        npa[i] = hits;
        for (int k = 0; k < hits; k++) pa[i][k] = hit[k] + (places[i].addr - pv);
    }
    int bad = 0;
    for (int pass = 0; pass < 2; pass++) {   /* pass 0: write what's wanted; pass 1 (on a failure): put it all back */
        for (int i = 0; i < nplaces; i++) {
            const unsigned char *to = pass ? now[i] : want_ours ? places[i].ours : places[i].orig;
            unsigned char back[8];
            int ok = 1;
            for (int k = 0; k < npa[i]; k++) {
                uint64_t pg0 = pa[i][k] & ~(uint64_t)(PG - 1);
                unsigned char *m = phys_map(pg0, PG, 1);
                if (!m) {
                    ok = 0;
                    continue;
                }
                memcpy(m + (pa[i][k] - pg0), to, 8);
                phys_sync(m, PG);
                phys_unmap(m, PG);
            }
            ok = ok && !rd(fd, places[i].addr, back, 8) && !memcmp(back, to, 8);   /* what SS_RVC itself now runs */
            if (!ok && !pass) {
                bad = 1;
            }
        }
        if (!bad) break;
    }
    close(fd);
    if (bad) {
        if (++write_fails >= 3) {   /* not every 30 s for ever: the search and the writes are not free */
            set_state("refused");
            return 1;
        }
        set_state("stock");
        return 2;
    }
    *last_pid = pid;
    *last_state = want_ours;
    set_state(want_ours ? "lifted" : "stock");
    return 0;
}

/* -places file: lines "<hex address> <16 hex digits now> <16 hex digits ours> [what]" */
static int load_places(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[160], a[24], b[24];
    nplaces = 0;
    while (nplaces < 8 && fgets(line, sizeof line, f)) {
        unsigned long addr;
        if (sscanf(line, "%lx %20s %20s", &addr, a, b) != 3 || strlen(a) != 16 || strlen(b) != 16) continue;
        struct place *p = &places[nplaces++];
        p->addr = addr;
        for (int i = 0; i < 8; i++) {
            unsigned x, y;
            sscanf(a + 2 * i, "%2x", &x);
            sscanf(b + 2 * i, "%2x", &y);
            p->orig[i] = (unsigned char)x, p->ours[i] = (unsigned char)y;
        }
        snprintf(p->what, sizeof p->what, "place %d", nplaces);
    }
    fclose(f);
    return nplaces ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *proc = "/proc";
    int undo = 0, loop = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) undo = 1;
        else if (!strcmp(argv[i], "-loop")) loop = 1;
        else if (!strcmp(argv[i], "-state") && i + 1 < argc) state_path = argv[++i];
        else if (!strcmp(argv[i], "-name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "-places") && i + 1 < argc) {
            if (load_places(argv[++i])) { fprintf(stderr, "rvcpatch: no places in %s\n", argv[i]); return 2; }
        }
        else {
            fprintf(stderr, "usage: rvcpatch [-u] [-loop] [-state file] [-name program] [-places file]\n");
            return 2;
        }
    }
    /* A search that never finished is written as "searching <n>". SYNC switched off, or the stick's installer
     * stopping us, during the search leaves that too, so it is tried again; three in a row means the search itself
     * is the trouble: never again until the file is removed (every install removes it). */
    if (state_path && !undo) {
        FILE *f = fopen(state_path, "r");
        char s[32] = "";
        if (f) {
            if (!fgets(s, sizeof s, f)) s[0] = 0;
            fclose(f);
        }
        if (!strncmp(s, "searching", 9)) {
            search_tries = atoi(s + 9) > 0 ? atoi(s + 9) : 1;
        }
        if (search_tries >= 3 || !strncmp(s, "refused", 7)) {
            return 1;
        }
    }
    long last_pid = -1;
    int state = -1, rc;
    for (;;) {
        rc = once(proc, undo, &last_pid, &state);
        if (!loop || rc == 1) return rc;
        sleep(30);
    }
}
