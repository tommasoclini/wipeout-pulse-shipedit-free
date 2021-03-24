
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

#include "fileio.h"
#include "util.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

typedef struct DecompressionContext {
    const char *buf;
    size_t buf_len;

    uint32_t lookback_buffer_write_index;
    uint8_t bit_in_current_byte;
    uint8_t current_byte;
    uint32_t current_byte_read_pos;
    char lookback_buffer[8192];
} DecompressionContext;

uint32_t
DecompressionContext_read_bits(DecompressionContext *self, int size_bits)
{
    uint32_t result = 0;
    uint32_t current_bit = 1 << (size_bits - 1U & 0x1f);

    while (current_bit != 0 ) {
        if (self->bit_in_current_byte == 0x80) {
            self->current_byte = self->buf[self->current_byte_read_pos++];
        }

        if ((self->current_byte & self->bit_in_current_byte) != 0) {
            result |= current_bit;
        }

        self->bit_in_current_byte >>= 1;
        current_bit >>= 1;

        if (self->bit_in_current_byte == 0) {
            self->bit_in_current_byte = 0x80;
        }
    }

    return result;
}

void
DecompressionContext_unpack(DecompressionContext *self, char *buffer, uint32_t length)
{
    while (length > 0) {
        if (DecompressionContext_read_bits(self, 1)) {
            // If bit 1 is set, it's a verbatim byte
            char tmp = DecompressionContext_read_bits(self, 8);
            *buffer++ = tmp;
            length--;

            self->lookback_buffer[self->lookback_buffer_write_index] = tmp;
            self->lookback_buffer_write_index = self->lookback_buffer_write_index + 1 & 0x1fff;
        } else {
            // If bit 1 is not set, it's a copy from previous contents:
            // 13 bits offset
            // 4 bits repetition count (- 3)  -- up to 18 bytes repeated
            int copy_from_lookback_offset = DecompressionContext_read_bits(self, 13);
            int repetition_count = 3 + DecompressionContext_read_bits(self, 4);

            for (int i=0; i<repetition_count; ++i) {
                char tmp = self->lookback_buffer[(copy_from_lookback_offset + i) & 0x1fff];
                *buffer++ = tmp;
                length--;

                self->lookback_buffer[self->lookback_buffer_write_index] = tmp;
                self->lookback_buffer_write_index = self->lookback_buffer_write_index + 1 & 0x1fff;
            }
        }
    }
}

DecompressionContext *
DecompressionContext_init(DecompressionContext *self, const char *buf, size_t buf_len)
{
    self->buf = buf;
    self->buf_len = buf_len;
    self->lookback_buffer_write_index = 1;
    self->current_byte_read_pos = 0;
    self->current_byte = 0;
    self->bit_in_current_byte = 0x80;
    return self;
}

struct WADEntry {
    uint32_t name;
    uint32_t start_offset;
    uint32_t length;
    uint32_t compressed_length;
};

struct WADHeader {
    uint32_t version;
    uint32_t nfiles;
    struct WADEntry entries[];
};

struct MountedWAD {
    char *filename;
    struct WADHeader *header;
    size_t len;
    struct MountedWAD *next;
};

static struct MountedWAD *
g_mounted_wads = NULL;

bool
mount_wad(const char *filename)
{
    struct MountedWAD *wad = malloc(sizeof(struct MountedWAD));
    memset(wad, 0, sizeof(wad));

    wad->header = (struct WADHeader *)read_file(filename, &wad->len);

    if (wad->header == NULL) {
        free(wad);
        return false;
    }

    wad->filename = strdup(filename);

    wad->next = g_mounted_wads;
    g_mounted_wads = wad;
    return true;
}

char *
read_wad_file(const char *filename, size_t *len)
{
    struct MountedWAD *wad = g_mounted_wads;

    uint32_t name = crc32(0xFFFFFFFF, (const Bytef *)filename, strlen(filename));

    while (wad != NULL) {
        for (int i=0; i<wad->header->nfiles; ++i) {
            struct WADEntry *entry = &wad->header->entries[i];
            if (entry->name == name) {
                //printf("%s -> %s\n", wad->filename, filename);

                char *result = NULL;

                const char *read_ptr = (const uint8_t *)wad->header + entry->start_offset;

                if (entry->length != entry->compressed_length) {
                    if (entry->length & 0x80000000) {
                        *len = entry->length;
                        *len &= ~0x80000000;

                        if (*((uint8_t *)read_ptr) != 0x78) {
                            fail("Invalid zlib header");
                        }

                        z_stream stream;
                        stream.next_in = (char *)read_ptr;
                        stream.avail_in = entry->compressed_length;
                        stream.zalloc = Z_NULL;
                        stream.zfree = Z_NULL;
                        int res = inflateInit(&stream);
                        if (res != Z_OK) {
                            fail("Failed to init zlib");
                        }

                        result = malloc(*len);

                        stream.next_out = result;
                        stream.avail_out = *len;

                        res = inflate(&stream, Z_SYNC_FLUSH);
                        if (res != Z_STREAM_END || stream.total_out != *len) {
                            fail("%s: zlib error for %s", wad->filename, filename);
                        }

                        inflateEnd(&stream);
                    } else {
                        result = malloc(entry->length);
                        *len = entry->length;

                        DecompressionContext tmp;
                        DecompressionContext_init(&tmp, read_ptr, entry->compressed_length);
                        DecompressionContext_unpack(&tmp, result, entry->length);
                    }
                } else {
                    result = malloc(entry->length);
                    *len = entry->length;

                    memcpy(result, read_ptr, entry->length);
                }

                return result;
            }
        }

        wad = wad->next;
    }

    return NULL;
}

char *
read_file(const char *filename, size_t *len)
{
    char *buf = read_wad_file(filename, len);
    if (buf != NULL) {
        return buf;
    }

    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        printf("Could not open %s\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = malloc(size);

    if (fread(buf, 1, size, fp) != size) {
        fail("Could not read %zu bytes from %s", size, filename);
    }

    fclose(fp);

    if (len) {
        *len = size;
    }

    return buf;
}


void
parse_file_lines(const char *filename, void (*line_callback)(const char *, void *), void *user_data)
{
    size_t len;
    char *buf = read_file(filename, &len);

    char *cur = buf;
    char *end = buf + len;
    while (cur < end) {
        char *lineend = cur;
        while (*lineend != '\n' && *lineend != '\0') {
            ++lineend;
        }

        *lineend = '\0';

        line_callback(cur, user_data);

        cur = lineend + 1;
    }

    free(buf);
}
