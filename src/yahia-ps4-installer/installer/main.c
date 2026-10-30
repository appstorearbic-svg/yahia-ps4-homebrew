#include <stddef.h>
#include <orbis/CommonDialog.h>
#include <orbis/MsgDialog.h>
#include <orbis/Sysmodule.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PATH_CAP 512
#define BUTTON_FIRST 1
#define BUTTON_SECOND 2
#define BAR_TARGET 0

static inline void set_magic(uint32_t *magic, const OrbisCommonDialogBaseParam *param) {
    *magic = (uint32_t)(ORBIS_COMMON_DIALOG_MAGIC_NUMBER + (uint64_t)param);
}

static void init_base(OrbisCommonDialogBaseParam *param) {
    memset(param, 0, sizeof(*param));
    param->size = (uint32_t)sizeof(*param);
    set_magic(&param->magic, param);
}

static void init_msg(OrbisMsgDialogParam *param) {
    memset(param, 0, sizeof(*param));
    init_base(&param->baseParam);
    param->size = sizeof(*param);
}

static int show_choice(const char *message, const char *first, const char *second) {
    OrbisMsgDialogParam param;
    OrbisMsgDialogUserMessageParam user;
    OrbisMsgDialogButtonsParam buttons;
    OrbisMsgDialogResult result;
    memset(&user, 0, sizeof(user));
    memset(&buttons, 0, sizeof(buttons));
    memset(&result, 0, sizeof(result));
    buttons.msg1 = first;
    buttons.msg2 = second;
    user.buttonType = ORBIS_MSG_DIALOG_BUTTON_TYPE_2BUTTONS;
    user.msg = message;
    user.buttonsParam = &buttons;
    init_msg(&param);
    param.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &user;
    if (sceMsgDialogInitialize() < 0) return -1;
    if (sceMsgDialogOpen(&param) < 0) {
        sceMsgDialogTerminate();
        return -1;
    }
    while (sceMsgDialogUpdateStatus() != ORBIS_COMMON_DIALOG_STATUS_FINISHED) { }
    sceMsgDialogGetResult(&result);
    sceMsgDialogClose();
    sceMsgDialogTerminate();
    if (result.buttonId == BUTTON_FIRST) return 0;
    if (result.buttonId == BUTTON_SECOND) return 1;
    return -1;
}

static void show_info(const char *message) {
    OrbisMsgDialogParam param;
    OrbisMsgDialogUserMessageParam user;
    OrbisMsgDialogResult result;
    memset(&user, 0, sizeof(user));
    memset(&result, 0, sizeof(result));
    user.buttonType = ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
    user.msg = message;
    init_msg(&param);
    param.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &user;
    if (sceMsgDialogInitialize() < 0) return;
    if (sceMsgDialogOpen(&param) >= 0) {
        while (sceMsgDialogUpdateStatus() != ORBIS_COMMON_DIALOG_STATUS_FINISHED) { }
        sceMsgDialogGetResult(&result);
        sceMsgDialogClose();
    }
    sceMsgDialogTerminate();
}

static int open_progress(const char *message) {
    OrbisMsgDialogParam param;
    OrbisMsgDialogProgressBarParam progress;
    memset(&progress, 0, sizeof(progress));
    progress.barType = ORBIS_MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE;
    progress.msg = message;
    init_msg(&param);
    param.mode = ORBIS_MSG_DIALOG_MODE_PROGRESS_BAR;
    param.progBarParam = &progress;
    if (sceMsgDialogInitialize() < 0) return -1;
    if (sceMsgDialogOpen(&param) < 0) {
        sceMsgDialogTerminate();
        return -1;
    }
    sceMsgDialogProgressBarSetValue(BAR_TARGET, 3);
    return 0;
}

static void progress_set(unsigned int percent, const char *message) {
    sceMsgDialogProgressBarSetMsg(BAR_TARGET, message);
    sceMsgDialogProgressBarSetValue(BAR_TARGET, percent);
    sceMsgDialogUpdateStatus();
}

static void close_progress(void) {
    sceMsgDialogProgressBarSetValue(BAR_TARGET, 100);
    sceMsgDialogClose();
    sceMsgDialogTerminate();
}

static int mkdirs(const char *path) {
    char buffer[PATH_CAP];
    size_t n = strlen(path);
    if (!n || n >= sizeof(buffer)) return -1;
    memcpy(buffer, path, n + 1);
    for (char *p = buffer + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0777) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(buffer, 0777) < 0 && errno != EEXIST) return -1;
    return 0;
}

static int parent_dir(const char *path, char *out, size_t cap) {
    size_t n = strlen(path);
    if (n >= cap) return -1;
    memcpy(out, path, n + 1);
    char *slash = strrchr(out, '/');
    if (!slash) return -1;
    if (slash == out) slash[1] = '\0';
    else *slash = '\0';
    return 0;
}

static int backup_name(const char *path, char *out, size_t cap) {
    struct stat st;
    if (stat(path, &st) < 0) return errno == ENOENT ? 0 : -1;
    for (int i = 0; i < 1000; ++i) {
        int written = snprintf(out, cap, "%s.bak.%ld.%d", path, (long)time(NULL), i);
        if (written < 0 || (size_t)written >= cap) return -1;
        if (lstat(out, &st) < 0 && errno == ENOENT) return 1;
    }
    return -1;
}

static int install_resource(const char *source, const char *destination) {
    char directory[PATH_CAP], temporary[PATH_CAP], backup[PATH_CAP];
    struct stat st;
    int in = -1, out = -1, status = -1;
    int moved_old = 0;
    if (parent_dir(destination, directory, sizeof(directory)) < 0 || mkdirs(directory) < 0) return -1;
    in = open(source, O_RDONLY);
    if (in < 0) return -1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", destination) >= (int)sizeof(temporary)) goto done;
    unlink(temporary);
    out = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) goto done;
    char buffer[32768];
    for (;;) {
        ssize_t count = read(in, buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) goto done;
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t wrote = write(out, buffer + offset, (size_t)(count - offset));
            if (wrote <= 0) goto done;
            offset += wrote;
        }
    }
    if (fsync(out) < 0) goto done;
    if (close(out) < 0) { out = -1; goto done; }
    out = -1;
    close(in); in = -1;
    int b = backup_name(destination, backup, sizeof(backup));
    if (b < 0) goto done;
    if (b == 1) {
        if (rename(destination, backup) < 0) {
            if (errno != ENOENT) goto done;
        } else moved_old = 1;
    } else if (stat(destination, &st) == 0) {
        goto done;
    }
    if (rename(temporary, destination) < 0) {
        if (moved_old) rename(backup, destination);
        goto done;
    }
    status = 0;
done:
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    if (status < 0) unlink(temporary);
    return status;
}

static int resource_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int version_153_ready(void) {
    static const char *files[] = {
        "/app0/payload/1.53/GTXMOD153.prx",
        "/app0/payload/1.53/plugins.ini",
        "/app0/payload/1.53/GTX/Ai/Ai_ok.aik",
        "/app0/payload/1.53/GTX/photo/.keep",
        "/app0/payload/1.53/GTX/photo/GTX.dds",
        "/app0/payload/1.53/GTX/photo/GTX.otd",
        "/app0/payload/1.53/GTX/photo/GTX.png",
        "/app0/payload/1.53/GTX/photo/GTX.rpf",
        "/app0/payload/1.53/GTX/script/.keep",
        "/app0/payload/1.53/GTX/xml/.keep"
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        if (!resource_exists(files[i])) return 0;
    return 1;
}

typedef struct PayloadFile {
    const char *source;
    const char *destination;
} PayloadFile;

static int install_gtx_folder(void) {
    static const char *dirs[] = {
        "/data/GTX", "/data/GTX/Ai", "/data/GTX/photo", "/data/GTX/script", "/data/GTX/xml"
    };
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
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        if (mkdirs(dirs[i]) < 0) ok = 0;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        if (install_resource(files[i].source, files[i].destination) < 0) ok = 0;
    return ok ? 0 : -1;
}

static void run_install(void) {
    int s1, s2, s3;
    if (open_progress("جاري تثبيت ملفات الإصدار 1.53، يرجى الانتظار...انتهى النقل.") < 0) {
        show_info("تعذر فتح شاشة التقدم. لم يبدأ التثبيت.");
        return;
    }
    s1 = install_resource("/app0/payload/1.53/GTXMOD153.prx", "/data/GoldHEN/plugins/GTXMOD153.prx");
    progress_set(36, "الخطوة 1 من 3");
    s2 = install_resource("/app0/payload/1.53/plugins.ini", "/data/GoldHEN/plugins.ini");
    progress_set(68, "الخطوة 2 من 3");
    s3 = install_gtx_folder();
    progress_set(96, "الخطوة 3 من 3");
    close_progress();

    char result[256];
    snprintf(result, sizeof(result), "1 %s    2 %s    3 %s\n\nتم حفظ نسخ احتياطية للملفات التي استُبدلت باسم .bak.",
             s1 == 0 ? "✓" : "✗", s2 == 0 ? "✓" : "✗", s3 == 0 ? "✓" : "✗");
    show_info(result);
}

int main(void) {
    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG) < 0 || sceCommonDialogInitialize() < 0) {
        for (;;) { }
    }

    for (;;) {
        int pick = show_choice("اختر الإصدار المطلوب:", "1.53", "المزيد");
        if (pick < 0) continue;
        const char *version = "1.53";
        if (pick == 1) {
            int next = show_choice("اختر الإصدار:", "1.56", "1.57");
            if (next < 0) continue;
            version = next == 0 ? "1.56" : "1.57";
        }

        if (strcmp(version, "1.53") != 0) {
            show_info("لا توجد ملفات لهذا الإصدار داخل التطبيق.");
            continue;
        }
        if (!version_153_ready()) {
            show_info("ملفات الإصدار 1.53 غير مكتملة داخل التطبيق.");
            continue;
        }

        int confirm = show_choice("تم العثور على ملفات 1.53. سيتم نسخها إلى المسارات المطلوبة واستبدال الملفات المتطابقة.\n\nسيُنشئ التطبيق نسخة احتياطية قبل الاستبدال.", "تثبيت", "إلغاء");
        if (confirm == 0) run_install();
    }
    return 0;
}
