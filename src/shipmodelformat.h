#pragma once

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



#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

struct ShipModelHeader {
    uint32_t n_materials;
    uint32_t n_objects;
};

struct MaterialHeader {
    int8_t index;
    uint8_t is_cockpit_png;
    uint8_t is_canopy;
    uint8_t is_other;
};

struct ObjectHeader {
    uint32_t n_vertices;
    uint32_t vertexdata_offset;
    uint8_t material_index;
    uint8_t unused0;
    uint8_t unused1;
    uint8_t unused2;
};

struct Vertex {
    float x;
    float y;
    float z;

    float u;
    float v;
};

struct Material {
    struct Material *next;

    // serialization stuff
    const char *name;
    int index;
    bool is_cockpit_png;
    bool is_canopy;
    bool is_other;

    // runtime stuff
    char *pixels;
    int width;
    int height;
    int channels;
    bool pixels_dirty;
    uint8_t *pixels_drawn;

    uint32_t *palette;
    uint32_t current_color;

    uint32_t texture;
    uint32_t picker_texture;
};

struct ObjectTemp;

struct Object {
    struct Object *next;

    const char *used_material;
    struct Material *material;

    struct Vertex *vertexdata;
    size_t vertexdata_size;

    struct ObjectTemp *temp;
};

struct ShipModelTemp;

struct ShipModel {
    struct Material *materials;
    struct Object *objects;

    struct ShipModelTemp *temp;
};

