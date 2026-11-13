#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <orbis/SaveData.h>
#include "saves.h"
#include "common.h"
#include "menu.h"
#include "libfont.h"
#include <mini18n.h>

#define SAHL_ROOT_PATH   APOLLO_PATH "السهل/"
#define SAHL_COPIED_PATH SAHL_ROOT_PATH "المنسوخ/"
#define SAHL_TAKEN_PATH  SAHL_ROOT_PATH "الماخوذ/"

static void ensure_sahl_dirs(void)
{
    mkdirs(SAHL_COPIED_PATH "x");
    mkdirs(SAHL_TAKEN_PATH "x");
}

void sahl_init_dirs(void)
{
    ensure_sahl_dirs();
}

static int sahl_copy_to_folder(const save_entry_t *save)
{
    char mount[ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE];
    char source[256];
    char target[256];
    int ret;
    ensure_sahl_dirs();
    if (!save || !(save->flags & SAVE_FLAG_HDD) || !save->title_id || !save->dir_name)
        return FAILED;
    if (!orbis_SaveMount(save, ORBIS_SAVE_DATA_MOUNT_MODE_RDONLY, mount))
        return FAILED;
    snprintf(source, sizeof(source), APOLLO_SANDBOX_PATH "%s/", mount);
    snprintf(target, sizeof(target), SAHL_COPIED_PATH "%s/%s/", save->title_id, save->dir_name);
    ret = copy_directory(source, source, target);
    orbis_SaveUmount(mount);
    if (ret == SUCCESS)
		show_message("%s\n%s", _("تم نسخ ملف الحفظ إلى مجلد المنسوخ"), target);
    return ret;
}

static int sahl_find_source(const save_entry_t *save, char *source, size_t size)
{
	char direct[256];
	char nested[256];
	char sfo_dir[256];
    DIR *dir;
    struct dirent *entry;
    if (!save || !save->title_id)
        return FAILED;
    snprintf(direct, sizeof(direct), SAHL_TAKEN_PATH "%s/", save->title_id);
    snprintf(nested, sizeof(nested), "%s%s/", direct, save->dir_name ? save->dir_name : "");
	if (dir_exists(direct) == SUCCESS)
	{
		snprintf(sfo_dir, sizeof(sfo_dir), "%ssce_sys/", direct);
		if (dir_exists(sfo_dir) == SUCCESS)
        {
            snprintf(source, size, "%s", direct);
            return SUCCESS;
        }
        if (save->dir_name && dir_exists(nested) == SUCCESS)
        {
            snprintf(source, size, "%s", nested);
            return SUCCESS;
        }
    }
    /* Also accept a folder named <title_id>_<dir_name>. */
    dir = opendir(SAHL_TAKEN_PATH);
    if (!dir)
        return FAILED;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_DIR || entry->d_name[0] == '.')
            continue;
        if (strncmp(entry->d_name, save->title_id, strlen(save->title_id)) == 0)
        {
            snprintf(source, size, SAHL_TAKEN_PATH "%s/", entry->d_name);
            closedir(dir);
            return SUCCESS;
        }
    }
    closedir(dir);
    return FAILED;
}

int sahl_restore_from_folder(const save_entry_t *save)
{
    char source[256];
    char mount[ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE];
    char target[256];
    int ret;
    ensure_sahl_dirs();
    if (!save || !(save->flags & SAVE_FLAG_HDD) || !save->title_id)
        return FAILED;
    if (sahl_find_source(save, source, sizeof(source)) != SUCCESS)
    {
		show_message("%s\n%s%s", _("لا يوجد ملف حفظ مطابق لهذه اللعبة داخل مجلد الماخوذ"), SAHL_TAKEN_PATH, save->title_id);
        return FAILED;
    }
    if (!show_dialog(DIALOG_TYPE_YESNO, _("استبدال حفظ اللعبة بالملف الموجود في السهل؟\nأغلق اللعبة قبل المتابعة.")))
        return FAILED;
    if (!orbis_SaveMount(save, ORBIS_SAVE_DATA_MOUNT_MODE_RDWR, mount))
    {
        show_message(_("تعذر تركيب ملف الحفظ للكتابة"));
        return FAILED;
    }
    snprintf(target, sizeof(target), APOLLO_SANDBOX_PATH "%s/", mount);
    init_loading_screen(_("جاري وضع ملف الحفظ من السهل..."));
    ret = copy_directory(source, source, target);
    stop_loading_screen();
    orbis_SaveUmount(mount);
    if (ret == SUCCESS)
        show_message(_("تم وضع ملف الحفظ داخل اللعبة"));
    else
        show_message(_("فشل استبدال ملف الحفظ"));
    return ret;
}

int sahl_copy_save(const save_entry_t *save)
{
    ensure_sahl_dirs();
    init_loading_screen(_("جاري نسخ ملف الحفظ إلى السهل..."));
    int ret = sahl_copy_to_folder(save);
    stop_loading_screen();
    if (ret != SUCCESS)
        show_message(_("فشل نسخ ملف الحفظ إلى مجلد المنسوخ"));
    return ret;
}
