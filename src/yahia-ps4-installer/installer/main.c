#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define PATH_CAP 512

static int mkdirs(const char *path) {
    char buffer[PATH_CAP]; size_t n = strlen(path);
    if (!n || n >= sizeof(buffer)) return -1;
    memcpy(buffer, path, n + 1);
    for (char *p = buffer + 1; *p; ++p) if (*p == '/') {
        *p = '\0'; if (mkdir(buffer, 0777) < 0 && errno != EEXIST) return -1; *p = '/';
    }
    if (mkdir(buffer, 0777) < 0 && errno != EEXIST) return -1;
    return 0;
}
static int parent_dir(const char *path, char *out, size_t cap) {
    size_t n = strlen(path); if (n >= cap) return -1;
    memcpy(out, path, n + 1); char *slash = strrchr(out, '/'); if (!slash) return -1;
    if (slash == out) slash[1] = '\0'; else *slash = '\0'; return 0;
}
static int backup_name(const char *path, char *out, size_t cap) {
    struct stat st; if (stat(path, &st) < 0) return errno == ENOENT ? 0 : -1;
    for (int i = 0; i < 1000; ++i) {
        int n = snprintf(out, cap, "%s.bak.%ld.%d", path, (long)time(NULL), i);
        if (n < 0 || (size_t)n >= cap) return -1;
        if (lstat(out, &st) < 0 && errno == ENOENT) return 1;
    }
    return -1;
}
static int install_resource(const char *source, const char *destination) {
    char directory[PATH_CAP], temporary[PATH_CAP], backup[PATH_CAP]; struct stat st;
    int in = -1, out = -1, status = -1, moved_old = 0;
    if (parent_dir(destination, directory, sizeof(directory)) < 0 || mkdirs(directory) < 0) return -1;
    in = open(source, O_RDONLY); if (in < 0) return -1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", destination) >= (int)sizeof(temporary)) goto done;
    unlink(temporary); out = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (out < 0) goto done;
    char buffer[32768];
    for (;;) {
        ssize_t count = read(in, buffer, sizeof(buffer)); if (count == 0) break; if (count < 0) goto done;
        ssize_t offset = 0; while (offset < count) {
            ssize_t wrote = write(out, buffer + offset, (size_t)(count - offset)); if (wrote <= 0) goto done; offset += wrote;
        }
    }
    if (fsync(out) < 0) goto done;
    if (close(out) < 0) { out = -1; goto done; } out = -1; close(in); in = -1;
    int b = backup_name(destination, backup, sizeof(backup)); if (b < 0) goto done;
    if (b == 1) {
        if (rename(destination, backup) < 0) { if (errno != ENOENT) goto done; } else moved_old = 1;
    } else if (stat(destination, &st) == 0) goto done;
    if (rename(temporary, destination) < 0) { if (moved_old) rename(backup, destination); goto done; }
    status = 0;
done:
    if (in >= 0) close(in); if (out >= 0) close(out); if (status < 0) unlink(temporary); return status;
}
static int resource_exists(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode); }
int installer_153_ready(void) {
    static const char *files[] = {
        "/app0/payload/1.53/GTXMOD153.prx", "/app0/payload/1.53/plugins.ini",
        "/app0/payload/1.53/GTX/Ai/Ai_ok.aik", "/app0/payload/1.53/GTX/photo/.keep",
        "/app0/payload/1.53/GTX/photo/GTX.dds", "/app0/payload/1.53/GTX/photo/GTX.otd",
        "/app0/payload/1.53/GTX/photo/GTX.png", "/app0/payload/1.53/GTX/photo/GTX.rpf",
        "/app0/payload/1.53/GTX/script/.keep", "/app0/payload/1.53/GTX/xml/.keep"
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) if (!resource_exists(files[i])) return 0;
    return 1;
}
typedef struct PayloadFile { const char *source; const char *destination; } PayloadFile;
static int install_gtx_folder(void) {
    static const char *dirs[] = {"/data/GTX", "/data/GTX/Ai", "/data/GTX/photo", "/data/GTX/script", "/data/GTX/xml"};
    static const PayloadFile files[] = {
        {"/app0/payload/1.53/GTX/Ai/Ai_ok.aik", "/data/GTX/Ai/Ai_ok.aik"},
        {"/app0/payload/1.53/GTX/photo/.keep", "/data/GTX/photo/.keep"},
        {"/app0/payload/1.53/GTX/photo/GTX.dds", "/data/GTX/photo/GTX.dds"},
        {"/app0/payload/1.53/GTX/photo/GTX.otd", "/data/GTX/photo/GTX.otd"},
        {"/app0/payload/1.53/GTX/photo/GTX.png", "/data/GTX/photo/GTX.png"},
        {"/app0/payload/1.53/GTX/photo/GTX.rpf", "/data/GTX/photo/GTX.rpf"},
        {"/app0/payload/1.53/GTX/script/.keep", "/data/GTX/script/.keep"},
        {"/app0/payload/1.53/GTX/xml/.keep", "/data/GTX/xml/.keep"}
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) if (mkdirs(dirs[i]) < 0) ok = 0;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) if (install_resource(files[i].source, files[i].destination) < 0) ok = 0;
    return ok ? 0 : -1;
}
void installer_run_153(int results[3]) {
    results[0] = install_resource("/app0/payload/1.53/GTXMOD153.prx", "/data/GoldHEN/plugins/GTXMOD153.prx");
    results[1] = install_resource("/app0/payload/1.53/plugins.ini", "/data/GoldHEN/plugins.ini");
    results[2] = install_gtx_folder();
}
