#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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
static int install_resource(const char *source, const char *destination) {
    char directory[PATH_CAP], temporary[PATH_CAP];
    int in = -1, out = -1, status = -1;
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
    /* Replace any old file with the same name. */
    if (unlink(destination) < 0 && errno != ENOENT) goto done;
    if (rename(temporary, destination) < 0) goto done;
    status = 0;
done:
    if (in >= 0) close(in); if (out >= 0) close(out); if (status < 0) unlink(temporary); return status;
}
static int resource_exists(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode); }
typedef struct PayloadFile { const char *source; const char *destination; } PayloadFile;

static int make_payload_paths(int version, char *prx, size_t prx_cap, char files[8][PATH_CAP]) {
    const char *prx_name = version == 156 ? "GTXMOD156.prx" : version == 153 ? "GTXMOD153.prx" : NULL;
    const char *folder = version == 156 ? "1.56" : version == 153 ? "1.53" : NULL;
    if (!prx_name || !folder) return -1;
    if (snprintf(prx, prx_cap, "/app0/payload/%s/%s", folder, prx_name) >= (int)prx_cap) return -1;
    static const char *rel[] = {
        "GTX/Ai/Ai_ok.aik", "GTX/photo/.keep", "GTX/photo/GTX.dds", "GTX/photo/GTX.otd",
        "GTX/photo/GTX.png", "GTX/photo/GTX.rpf", "GTX/script/.keep", "GTX/xml/.keep"
    };
    for (size_t i = 0; i < 8; ++i)
        if (snprintf(files[i], PATH_CAP, "/app0/payload/%s/%s", folder, rel[i]) >= PATH_CAP) return -1;
    return 0;
}

int installer_version_ready(int version) {
    char prx[PATH_CAP], files[8][PATH_CAP];
    if (make_payload_paths(version, prx, sizeof(prx), files) < 0 || !resource_exists(prx)) return 0;
    if (!resource_exists(files[0]) || !resource_exists(files[1]) || !resource_exists(files[2]) || !resource_exists(files[3]) ||
        !resource_exists(files[4]) || !resource_exists(files[5]) || !resource_exists(files[6]) || !resource_exists(files[7])) return 0;
    return resource_exists(version == 153 ? "/app0/payload/1.53/plugins.ini" : "/app0/payload/1.56/plugins.ini");
}

static int install_gtx_folder_version(int version) {
    char prx[PATH_CAP], sources[8][PATH_CAP];
    if (make_payload_paths(version, prx, sizeof(prx), sources) < 0) return -1;
    static const char *dirs[] = {"/data/GTX", "/data/GTX/Ai", "/data/GTX/photo", "/data/GTX/script", "/data/GTX/xml"};
    static const char *destinations[] = {
        "/data/GTX/Ai/Ai_ok.aik", "/data/GTX/photo/.keep", "/data/GTX/photo/GTX.dds", "/data/GTX/photo/GTX.otd",
        "/data/GTX/photo/GTX.png", "/data/GTX/photo/GTX.rpf", "/data/GTX/script/.keep", "/data/GTX/xml/.keep"
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) if (mkdirs(dirs[i]) < 0) ok = 0;
    for (size_t i = 0; i < 8; ++i) if (install_resource(sources[i], destinations[i]) < 0) ok = 0;
    return ok ? 0 : -1;
}

void installer_run_version(int version, int results[3]) {
    char prx_source[PATH_CAP], sources[8][PATH_CAP], plugins_source[PATH_CAP];
    if (make_payload_paths(version, prx_source, sizeof(prx_source), sources) < 0) {
        results[0] = results[1] = results[2] = -1; return;
    }
    snprintf(plugins_source, sizeof(plugins_source), "/app0/payload/%s/plugins.ini", version == 156 ? "1.56" : "1.53");
    results[0] = install_resource(prx_source, version == 156 ? "/data/GoldHEN/plugins/GTXMOD156.prx" : "/data/GoldHEN/plugins/GTXMOD153.prx");
    results[1] = install_resource(plugins_source, "/data/GoldHEN/plugins.ini");
    results[2] = install_gtx_folder_version(version);
}
int installer_153_ready(void) { return installer_version_ready(153); }
int installer_156_ready(void) { return installer_version_ready(156); }
void installer_run_153(int results[3]) { installer_run_version(153, results); }
void installer_run_156(int results[3]) { installer_run_version(156, results); }
