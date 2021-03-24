
/**
 * shipedit -- WipeOut Pulse PSP Ship Skin Editor
 * Copyright (c) 2021 Thomas Perl <m@thp.io>
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/


#define _GNU_SOURCE

#include "saveskin.h"

#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// https://stackoverflow.com/a/40160038

#if defined(_MSC_VER)
static int
vasprintf(char** strp, const char* fmt, va_list ap)
{
    // _vscprintf tells you how big the buffer needs to be
    int len = _vscprintf(fmt, ap);
    if (len == -1) {
        return -1;
    }
    size_t size = (size_t)len + 1;
    char* str = malloc(size);
    if (!str) {
        return -1;
    }
    // _vsprintf_s is the "secure" version of vsprintf
    int r = vsprintf_s(str, len + 1, fmt, ap);
    if (r == -1) {
        free(str);
        return -1;
    }
    *strp = str;
    return r;
}

static int
asprintf(char** strp, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}
#endif /* _MSC_VER */
#else
#include <unistd.h>
#endif

#include "psp-save.h"
#include "kirk_engine.h"


static const unsigned char
PULSE_KEY[16] = {
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

static const int
PULSE_MODE = 5;

void
ensure_kirk_inited()
{
    static bool kirk_inited = false;
    if (!kirk_inited) {
        kirk_init();
        kirk_inited = true;
    }
}

bool
saveskin_decrypt(char *buf, size_t *len)
{
    ensure_kirk_inited();

    uint8_t compare[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    if (memcmp(buf + 8, compare, 8) == 0 && *len == 24816) {
        // No need to decrypt?
        *len = 24800;
        return true;
    }

    int len_int = *len;
    int aligned_len = len_int;
    char key[16];
    memcpy(key, PULSE_KEY, 16);
    int res = decrypt_data(PULSE_MODE, buf, &len_int, &aligned_len, key);
    if (res != 0) {
        return false;
    }

    *len = len_int;
    return true;
}

#include "template_paramsfo.h"

// https://www.psdevwiki.com/ps3/PARAM.SFO#Description
struct sfo_header {
    uint32_t magic; /************ Always PSF */
    uint32_t version; /********** Usually 1.1 */
    uint32_t key_table_start; /** Start offset of key_table */
    uint32_t data_table_start; /* Start offset of data_table */
    uint32_t tables_entries; /*** Number of entries in all tables */
};

struct sfo_index_table_entry {
    uint16_t key_offset; /*** param_key offset (relative to start offset of key_table) */
    uint16_t data_fmt; /***** param_data data type */
    uint32_t data_len; /***** param_data used bytes */
    uint32_t data_max_len; /* param_data total bytes */
    uint32_t data_offset; /** param_data offset (relative to start offset of data_table) */
};

void
param_sfo_set_key(char *param_sfo, size_t len, const char *key, const char *value)
{
    struct sfo_header *hdr = (struct sfo_header *)param_sfo;

    struct sfo_index_table_entry *index_table = (struct sfo_index_table_entry *)&hdr[1];
    for (int i=0; i<hdr->tables_entries; ++i) {
        const char *index_key = param_sfo + hdr->key_table_start + index_table[i].key_offset;
        if (strcmp(index_key, key) == 0) {
            if (index_table[i].data_fmt == 0x0204) {
                char *dest = param_sfo + hdr->data_table_start + index_table[i].data_offset;
                if (strlen(value) + 1 <= index_table[i].data_max_len) {
                    memset(dest, 0, index_table[i].data_max_len);
                    strcpy(dest, value);
                    index_table[i].data_len = strlen(value) + 1;
                }
            }
        }
    }
}

void
saveskin_save(const char *out_dir, char *buf, size_t len, int slot,
        void (*icon0_save_callback)(const char *filename, void *user_data), void *user_data)
{
    ensure_kirk_inited();

    char *team_name = strdup(buf);

    char *folder;
    asprintf(&folder, "UCES00465DTEAMSKIN%04d", slot);

    char *save_dir;
    asprintf(&save_dir, "%s/%s", out_dir, folder);

#if !defined(_WIN32)
    mkdir(save_dir, 0777);
#else
    CreateDirectory(save_dir, NULL);
#endif

    const char *name = "16034453";

    char *in_filename;
    asprintf(&in_filename, "%s/%s.in", save_dir, name);

    char *out_filename;
    asprintf(&out_filename, "%s/%s", save_dir, name);

    char *sfo_in_filename;
    asprintf(&sfo_in_filename, "%s/PARAM.SFO.in", save_dir);

    char *sfo_out_filename;
    asprintf(&sfo_out_filename, "%s/PARAM.SFO", save_dir);

    printf("in_filename: %s\nout_filename: %s\nsfo_in_filename: %s\nsfo_out_filename: %s\n",
            in_filename, out_filename, sfo_in_filename, sfo_out_filename);

    FILE *tmp = fopen(in_filename, "wb");
    fwrite(buf, len, 1, tmp);
    fclose(tmp);

    char *param_sfo = malloc(PARAM_SFO_len);
    memcpy(param_sfo, PARAM_SFO, PARAM_SFO_len);

    param_sfo_set_key(param_sfo, PARAM_SFO_len, "SAVEDATA_TITLE", "Custom Ship Skin");

    char *detail;
    asprintf(&detail, "Team: %s\n"
            "Ship Skin Slot %d\n"
            "Created with thp's shipedit\n",
            team_name, slot+1);

    free(team_name);
    param_sfo_set_key(param_sfo, PARAM_SFO_len, "SAVEDATA_DETAIL", detail);
    free(detail);

    param_sfo_set_key(param_sfo, PARAM_SFO_len, "SAVEDATA_DIRECTORY", folder);
    free(folder);

    tmp = fopen(sfo_in_filename, "wb");
    fwrite(param_sfo, PARAM_SFO_len, 1, tmp);
    fclose(tmp);

    FILE *in = fopen(in_filename, "rb");
    FILE *out = fopen(out_filename, "wb");

    FILE *sfo_in = fopen(sfo_in_filename, "rb");
    FILE *sfo_out = fopen(sfo_out_filename, "wb");

    char key[16];
    memcpy(key, PULSE_KEY, 16);
    encrypt_file(in, out, name, sfo_in, sfo_out, key, PULSE_MODE);

    fclose(in);
    fclose(out);

    fclose(sfo_in);
    fclose(sfo_out);

    unlink(in_filename);
    unlink(sfo_in_filename);

    char *tmpstr;
    asprintf(&tmpstr, "%s/ICON0.PNG", save_dir);
    icon0_save_callback(tmpstr, user_data);
    free(tmpstr);

    free(sfo_out_filename);
    free(sfo_in_filename);
    free(in_filename);
    free(out_filename);
}
