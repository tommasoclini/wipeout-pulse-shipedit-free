
/**
 * fontaine2
 * Copyright (c) 2017-2021 Thomas Perl <m@thp.io>
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


#include "fontaine2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define FONTAINE_MAGIC "https://thp.io/2017/fontaine/\r\n\0"
#define FONTAINE_VERSION 0x00010000

struct FontaineHeader {
    char magic[32];
    uint32_t version;
    uint16_t n_fonts;
    uint16_t reserved;
};

struct IndexEntry {
    uint16_t string_table_offset;
    uint8_t number_of_chars_minus_1;
    uint8_t font_name_first_character;
    uint32_t file_offset_pixeldata;
    uint8_t codepage;
    uint8_t reserved0;
    uint8_t reserved1;
    uint8_t reserved2;
};

struct PixelDataEntry {
    uint8_t character;
    uint8_t width;
    uint8_t height;
    uint8_t xspacing;
};

static void decode_2bit_data(uint8_t *pixels, struct PixelDataEntry *chars, int count, uint8_t *encoded_pixel_data)
{
    int bits = 0;

    int i;
    for (i=0; i<count; i++) {
        int row;
        for (row=0; row<chars->height; row++) {
            int column;
            for (column=0; column<chars->width; column++) {
                uint8_t v = (encoded_pixel_data[0] >> (6 - bits)) & 0x03;

                *pixels++ = v;

                bits += 2;
                if (bits == 8) {
                    encoded_pixel_data++;
                    bits = 0;
                }
            }
        }

        if (bits != 0) {
            encoded_pixel_data++;
            bits = 0;
        }

        chars++;
    }
}

struct FontaineFontReader *
fontaine_font_reader_new(char *buf, size_t len)
{
    struct FontaineFontReader *reader = malloc(sizeof(struct FontaineFontReader));

    reader->buf = buf;
    reader->len = len;

    reader->header = (struct FontaineHeader *)reader->buf;
    reader->entries = (struct IndexEntry *)(reader->buf + sizeof(struct FontaineHeader));
    reader->string_table = buf + sizeof(struct FontaineHeader) + sizeof(struct IndexEntry) * reader->header->n_fonts;

    return reader;
}

void *
fontaine_font_reader_foreach(struct FontaineFontReader *reader, fontaine_font_reader_callback_t callback, void *user_data)
{
    void *result = NULL;
    struct IndexEntry *entry = reader->entries;
    int i;

    for (i=0; i<reader->header->n_fonts; i++) {
        const char *name = reader->string_table + entry->string_table_offset;
        int count = entry->number_of_chars_minus_1 + 1;
        size_t offset = entry->file_offset_pixeldata;
        struct PixelDataEntry *chars = (struct PixelDataEntry *)(reader->buf + offset);
        uint8_t *encoded_pixel_data = (uint8_t *)(chars + count);

        result = callback(i, name, chars, count, encoded_pixel_data, entry->codepage, user_data);
        if (result != NULL) {
            break;
        }

        entry++;
    }

    return result;
}

void
fontaine_font_reader_destroy(struct FontaineFontReader *reader)
{
    free(reader);
}

struct InMemoryFont *in_memory_font_new(const char *name,
        struct PixelDataEntry *chars, int count,
        uint8_t *encoded_pixel_data, int codepage,
        bool replacements)
{
    struct InMemoryFont *font = malloc(sizeof(struct InMemoryFont));

    int i;
    struct PixelDataEntry *e = chars;
    size_t total_output_pixels = 0;

    static const struct {
        const char *from;
        const char *to;
    } REPLACEMENT[] = {
        {
            "abcdefghijklmnopqrstuvwxyz",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        },
        {
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
            "abcdefghijklmnopqrstuvwxyz"
        },
    };

    memset(font, 0, sizeof(*font));

    font->name = strdup(name);
    font->codepage = codepage;

    font->n_chars = count;
    font->max_char_width = chars[0].width;
    font->max_char_height = chars[0].height;
    font->is_monospace = 1;

    for (i=0; i<255; i++) {
        font->char_offset[i] = (uint32_t)-1;
    }

    for (i=0; i<count; i++) {
        if (e->width != font->max_char_width || e->height != font->max_char_height) {
            font->is_monospace = 0;

            if (e->width > font->max_char_width) {
                font->max_char_width = e->width;
            }

            if (e->height > font->max_char_height) {
                font->max_char_height = e->height;
            }
        }

        font->char_offset[e->character] = total_output_pixels;
        font->char_width[e->character] = e->width;
        font->char_height[e->character] = e->height;
        font->char_xspacing[e->character] = e->xspacing;

        total_output_pixels += e->width * e->height;

        e++;
    }

    if (replacements) {
        /* fill undefined lowercase/uppwercase characters from other set */
        for (i=0; i<(int)(sizeof(REPLACEMENT)/sizeof(REPLACEMENT[0])); i++) {
            int j;
            const char *from = REPLACEMENT[i].from;
            const char *to = REPLACEMENT[i].to;
            for (j=0; j<26; j++) {
                const int f = from[j];
                const int t = to[j];
                if (font->char_offset[t] == (uint32_t)-1 && font->char_offset[f] != (uint32_t)-1) {
                    font->char_offset[t] = font->char_offset[f];
                    font->char_width[t] = font->char_width[f];
                    font->char_height[t] = font->char_height[f];
                    font->char_xspacing[t] = font->char_xspacing[f];
                }
            }
        }
    }

    font->pixels_packed = malloc(total_output_pixels);

    decode_2bit_data(font->pixels_packed, chars, count, encoded_pixel_data);

    return font;
}

void
in_memory_font_measure(struct InMemoryFont *font, const char *text, int *w, int *h)
{
    *w = 0;
    *h = 0;

    while (1) {
        int c = *((unsigned char *)text++);
        if (c == '\0') {
            break;
        }

        // TODO: UTF-8

        if (font->char_offset[c] == (uint32_t)-1) {
            // fall back to ascii substitute
            c = 26;
        }

        if (font->char_offset[c] == (uint32_t)-1) {
            // does not exist at all, skip
            continue;
        }

        //printf("char '%c', width: %d, height:%d, xspacing: %d\n",
        //        c, font->char_width[c], font->char_height[c], font->char_xspacing[c]);

        if (h && font->char_height[c] > *h) {
            *h = font->char_height[c];
        }

        if (w) {
            *w += font->char_width[c] + font->char_xspacing[c];
        }
    }
}

void
in_memory_font_render_rgba_to_buffer(struct InMemoryFont *font, const char *text, int w, int h, uint8_t *pixels)
{
    (void)h;

    int x = 0;

    /* little endian: ABGR */
    static uint32_t LOOKUP[] = {
        0x00000000,
        0x55ffffff,
        0xaaffffff,
        0xffffffff,
    };

    while (1) {
        int c = *((unsigned char *)text++);
        if (c == '\0') {
            break;
        }

        // TODO: UTF-8

        {
            if (font->char_offset[c] == (uint32_t)-1) {
                // fall back to ascii substitute
                c = 26;
            }

            if (font->char_offset[c] == (uint32_t)-1) {
                // does not exist at all, skip
                continue;
            }

            uint8_t *read = font->pixels_packed + font->char_offset[c];

            int row;
            for (row=0; row<font->char_height[c]; row++) {
                uint32_t *write = (uint32_t *)(pixels + (row * w + x) * 4);

                int column;
                for (column=0; column<font->char_width[c]; column++) {
                    *write++ = LOOKUP[*read++];
                }
            }
        }

        x += font->char_width[c] + font->char_xspacing[c];
    }
}

uint8_t *in_memory_font_render_rgba(struct InMemoryFont *font, const char *text, int *w, int *h)
{
    uint8_t *pixels = NULL;
    size_t len;

    in_memory_font_measure(font, text, w, h);

    len = 4 * (*w) * (*h);
    pixels = malloc(len);
    memset(pixels, 0x00, len);

    in_memory_font_render_rgba_to_buffer(font, text, *w, *h, pixels);

    return pixels;
}

void in_memory_font_render_rgba_free(struct InMemoryFont *font, uint8_t *pixels)
{
    (void)font;
    free(pixels);
}

void
in_memory_font_free(struct InMemoryFont *font)
{
    free(font->name);
    free(font->pixels_packed);
    free(font);
}

struct LoadByNameCtx {
    const char *name;
    bool replacements;
};

static void *
in_memory_font_load_by_name(int index, const char *name,
        struct PixelDataEntry *chars, int count,
        uint8_t *encoded_pixel_data, int codepage, void *user_data)
{
    (void)index;

    struct LoadByNameCtx *ctx = user_data;

    if (strstr(name, ctx->name) != NULL) {
        return in_memory_font_new(name, chars, count, encoded_pixel_data, codepage, ctx->replacements);
    }

    return NULL;
}

struct InMemoryFont *
in_memory_font_new_name(struct FontaineFontReader *reader, const char *name, bool replacements)
{
    int free_reader = 0;
    if (reader == NULL) {
        reader = fontaine_font_reader_new(NULL, 0);
        free_reader = 1;
    }
    if (name == NULL) {
        name = "";
    }
    struct LoadByNameCtx ctx = { name, replacements };
    struct InMemoryFont *font = fontaine_font_reader_foreach(reader,
            in_memory_font_load_by_name, &ctx);
    if (free_reader) {
        fontaine_font_reader_destroy(reader);
    }
    return font;
}
