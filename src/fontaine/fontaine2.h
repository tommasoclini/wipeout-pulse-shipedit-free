#pragma once

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


#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

struct FontaineHeader;
struct IndexEntry;
struct PixelDataEntry;

struct InMemoryFont {
    char *name;
    uint8_t *pixels_packed;
    uint32_t char_offset[256];
    uint8_t char_width[256];
    uint8_t char_height[256];
    uint8_t char_xspacing[256];

    int n_chars; // number of characters defined in font
    int max_char_width; // maximum width of chars in font
    int max_char_height; // maximum height of chars in font
    int is_monospace; // 1 if all chars have the same size, 0 otherwise
    int codepage;
};

struct FontaineFontReader {
    /* in-memory buffer of encoded font */
    char *buf;
    size_t len;

    struct FontaineHeader *header;
    struct IndexEntry *entries;
    char *string_table;
};


struct FontaineFontReader *
fontaine_font_reader_new(char *buf, size_t len);


typedef void *(*fontaine_font_reader_callback_t)(int index, const char *name,
        struct PixelDataEntry *chars, int count,
        uint8_t *encoded_pixel_data, int codepage, void *user_data);

void *
fontaine_font_reader_foreach(struct FontaineFontReader *reader, fontaine_font_reader_callback_t callback, void *user_data);


void
fontaine_font_reader_destroy(struct FontaineFontReader *reader);



struct InMemoryFont *in_memory_font_new_name(struct FontaineFontReader *reader,
        const char *name, bool replacements);

struct InMemoryFont *in_memory_font_new(const char *name,
        struct PixelDataEntry *chars, int count,
        uint8_t *encoded_pixel_data, int codepage,
        bool replacements);


void
in_memory_font_measure(struct InMemoryFont *font, const char *text, int *w, int *h);


void
in_memory_font_render_rgba_to_buffer(struct InMemoryFont *font, const char *text, int w, int h, uint8_t *pixels);

uint8_t *
in_memory_font_render_rgba(struct InMemoryFont *font, const char *text, int *w, int *h);

void
in_memory_font_render_rgba_free(struct InMemoryFont *font, uint8_t *pixels);

void
in_memory_font_free(struct InMemoryFont *font);
