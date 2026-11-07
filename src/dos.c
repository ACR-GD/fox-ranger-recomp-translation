#define _POSIX_C_SOURCE 200809L
#include "dos.h"
#include "mem.h"
#include "vga_modex.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <io.h>
#  define strtok_r strtok_s
#else
#  include <dirent.h>
#endif

static char g_dir[1024] = ".";
void (*dos_exit_hook)(int code);

#define MAX_HANDLES 20
static FILE *g_handles[MAX_HANDLES];

void dos_init(const char *game_dir)
{
    snprintf(g_dir, sizeof g_dir, "%s", game_dir ? game_dir : ".");
    size_t n = strlen(g_dir);
    while (n > 1 && (g_dir[n - 1] == '/' || g_dir[n - 1] == '\\')) g_dir[--n] = 0;
    for (int i = 5; i < MAX_HANDLES; i++)
        if (g_handles[i]) { fclose(g_handles[i]); g_handles[i] = NULL; }
}

const char *dos_game_dir(void) { return g_dir; }

void dos_read_cstr(uint16_t seg, uint16_t off, char *out, size_t outsz)
{
    size_t i = 0;
    for (; i + 1 < outsz; i++) {
        char ch = (char)rd8(seg, (uint16_t)(off + i));
        if (!ch) break;
        out[i] = ch;
    }
    out[i] = 0;
}

/* find `name` in directory `dir` ignoring case; writes the real spelling */
static int match_ci(const char *dir, const char *name, char *real, size_t realsz)
{
#ifdef _WIN32
    (void)dir; snprintf(real, realsz, "%s", name);   /* NTFS/FAT are case-insensitive */
    return 1;
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d))) {
        const char *a = e->d_name, *b = name;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) { snprintf(real, realsz, "%s", e->d_name); found = 1; break; }
    }
    closedir(d);
    return found;
#endif
}

int dos_resolve(const char *dos_path, char *out, size_t outsz, int must_exist)
{
    char tmp[260];
    snprintf(tmp, sizeof tmp, "%s", dos_path);
    char *p = tmp;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') p += 2;        /* drive letter */
    for (char *q = p; *q; q++) if (*q == '\\') *q = '/';
    while (*p == '/') p++;                                           /* root -> game dir */

    char cur[1024];
    snprintf(cur, sizeof cur, "%s", g_dir);
    char *save = NULL;
    for (char *comp = strtok_r(p, "/", &save); comp; comp = strtok_r(NULL, "/", &save)) {
        if (!strcmp(comp, ".")) continue;
        char real[256];
        int last = (save == NULL || *save == 0);
        if (!match_ci(cur, comp, real, sizeof real)) {
            if (must_exist || !last) return -1;
            snprintf(real, sizeof real, "%s", comp);
        }
        size_t l = strlen(cur);
        snprintf(cur + l, sizeof cur - l, "/%s", real);
    }
    snprintf(out, outsz, "%s", cur);
    return 0;
}

int dos_open(const char *dos_path, int mode, int create)
{
    char host[1024];
    if (dos_resolve(dos_path, host, sizeof host, !create)) return -2;   /* file not found */
    const char *m = create ? "w+b" : ((mode & 3) == 0 ? "rb" : "r+b");
    FILE *f = fopen(host, m);
    if (!f) return create ? -3 : -2;
    for (int h = 5; h < MAX_HANDLES; h++)
        if (!g_handles[h]) { g_handles[h] = f; return h; }
    fclose(f);
    return -4;                                                        /* too many open files */
}

FILE *dos_handle_file(int h) { return (h >= 5 && h < MAX_HANDLES) ? g_handles[h] : NULL; }

int dos_close(int h)
{
    FILE *f = dos_handle_file(h);
    if (!f) return -6;
    fclose(f); g_handles[h] = NULL;
    return 0;
}

/* --------------------------------------------------------------- INT 21h */
static void ok(Cpu *c)            { c->cf = 0; }
static void err(Cpu *c, int code) { c->cf = 1; c->ax = (uint16_t)code; }

void dos_int21(Cpu *c)
{
    char path[260];
    switch (c->ah) {
    case 0x02: putchar(c->dl); ok(c); break;
    case 0x09: {                                       /* print '$'-terminated */
        for (uint16_t o = c->dx; ; o++) { char ch = (char)rd8(c->ds, o); if (ch == '$') break; putchar(ch); }
        ok(c); break; }
    case 0x25: wr16(0, (uint16_t)(c->al * 4), c->dx); wr16(0, (uint16_t)(c->al * 4 + 2), c->ds); ok(c); break;
    case 0x35: c->bx = rd16(0, (uint16_t)(c->al * 4)); c->es = rd16(0, (uint16_t)(c->al * 4 + 2)); ok(c); break;
    case 0x30: c->ax = 0x0005; c->bx = 0; c->cx = 0; ok(c); break;   /* DOS 5.0 */
    case 0x2C: c->cx = 0; c->dx = 0; ok(c); break;                   /* time: deterministic */
    case 0x2A: c->cx = 1992; c->dx = 0x0A17; c->al = 5; ok(c); break;
    case 0x3C: case 0x3D: {
        dos_read_cstr(c->ds, c->dx, path, sizeof path);
        int h = dos_open(path, c->al, c->ah == 0x3C);
        if (h < 0) err(c, -h); else { c->ax = (uint16_t)h; ok(c); }
        break; }
    case 0x3E: { int r = dos_close(c->bx); if (r < 0) err(c, -r); else ok(c); break; }
    case 0x3F: case 0x40: {
        FILE *f = dos_handle_file(c->bx);
        if (!f) { err(c, 6); break; }
        uint16_t n = c->cx, done = 0;
        if (c->ah == 0x3F) {
            for (; done < n; done++) { int ch = fgetc(f); if (ch == EOF) break; wr8(c->ds, (uint16_t)(c->dx + done), (uint8_t)ch); }
        } else {
            for (; done < n; done++) if (fputc(rd8(c->ds, (uint16_t)(c->dx + done)), f) == EOF) break;
        }
        c->ax = done; ok(c); break; }
    case 0x42: {
        FILE *f = dos_handle_file(c->bx);
        if (!f) { err(c, 6); break; }
        long off = (long)(int32_t)(((uint32_t)c->cx << 16) | c->dx);
        if (fseek(f, off, c->al == 0 ? SEEK_SET : c->al == 1 ? SEEK_CUR : SEEK_END)) { err(c, 25); break; }
        long pos = ftell(f);
        c->ax = (uint16_t)pos; c->dx = (uint16_t)(pos >> 16); ok(c); break; }
    case 0x4C:
        if (dos_exit_hook) dos_exit_hook(c->al);
        exit(c->al);
    default:
        fprintf(stderr, "int21: unhandled AH=%02Xh\n", c->ah);
        err(c, 1);
        break;
    }
}

void bios_int10(Cpu *c)
{
    switch (c->ah) {
    case 0x00:
        if ((c->al & 0x7F) == 0x13) vga_set_mode13(&g_vga);
        break;
    case 0x0F: c->al = 0x13; c->ah = 40; c->bh = 0; break;
    default: break;
    }
}

void rt_interrupt(Cpu *c, uint8_t n)
{
    switch (n) {
    case 0x21: dos_int21(c); break;
    case 0x10: bios_int10(c); break;
    default: fprintf(stderr, "int %02Xh ignored\n", n); break;
    }
}
