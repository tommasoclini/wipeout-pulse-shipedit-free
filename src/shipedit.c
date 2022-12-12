
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
#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL.h>
#include <SDL_opengl.h>
#if defined(__APPLE__)
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#include <png.h>
#include <zlib.h>

#include "fontaine2.h"
#include "spatial_color_quant.h"
#include "nativeui.h"

#include "saveskin.h"
#include "shipmodelformat.h"
#include "fileio.h"
#include "util.h"
#include "fps.h"

#define VERSION "v1.0.2"

struct InMemoryFont *
g_font_gui = NULL;

struct InMemoryFont *
g_font_heading = NULL;

static const float
ZOOM_MIN_FOV = 5.f;

static const float
ZOOM_MAX_FOV = 90.f;

static uint32_t
g_current_color = 0x000000;

static void
set_uint32_callback(uint32_t new_value, void *user_data)
{
    *((uint32_t *)user_data) = new_value;
}

static bool
g_batch_mode = false;

static struct {
    bool dragging;
    bool panning;
    bool drawing;
    float x;
    float y;
    struct {
        float x;
        float y;
    } down_location;
    uint32_t last_movement;
    const char *tooltip;
} g_mouse = { false, false, false, 0, 0, { 0, 0 }, -1, NULL };

static struct TeamToObject {
    const char *team_name;
    const char *team_label;
    const char *slug;
    struct ShipModel *loaded_model;
} *g_teams = NULL;


static bool
match_team_name(const char *name, struct TeamToObject *team)
{
    if (strcmp(name, team->team_name) == 0) {
        return true;
    }

    if (strcmp(name, "Mirage") == 0 && strcmp(team->team_name, "Mantis") == 0) {
        // Previous versions got this wrong; when loading, treat "Mirage" like "Mantis"
        // saving the file will write the new team_name (Mantis) and fix loading in the game
        return true;
    }

    return false;
}

static int
g_num_teams = 0;

static const int
MAX_SLOTS = 40;

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Vec2 {
    union {
        float x;
        float u;
    };
    union {
        float y;
        float v;
    };
};


struct Rect {
    int x;
    int y;
    int w;
    int h;
};

bool
rect_contains(struct Rect *r, int x, int y)
{
    return r->x <= x && r->x + r->w >= x &&
           r->y <= y && r->y + r->h >= y;
}


void
png_write_rgba(const char *filename, int w, int h, uint32_t *buf, bool yflip)
{
    png_image image;
    memset(&image, 0, sizeof(image));

    image.version = PNG_IMAGE_VERSION;
    image.width = w;
    image.height = h;
    image.format = PNG_FORMAT_RGBA;
    //image.flags = PNG_IMAGE_FLAG_COLORSPACE_NOT_sRGB;

    int res = png_image_write_to_file(&image, filename, 0, buf, (yflip ? -1 : 1) * w * 4, NULL);

    png_image_free(&image);
}

char *
png_load_rgba(const char *filename, int *w, int *h, int *channels)
{
    size_t len;
    char *buf = read_file(filename, &len);

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (png_image_begin_read_from_memory(&image, buf, len)) {
        image.format = PNG_FORMAT_RGBA;
        *channels = 4;

        png_bytep buffer = malloc(PNG_IMAGE_SIZE(image));
        *w = image.width;
        *h = image.height;

        if (png_image_finish_read(&image, NULL, buffer, 0, NULL) != 0) {
            png_image_free(&image);
            free(buf);
            return buffer;
        }
    }

    fail("Could not load png: %s", filename);
}

#define SHIP_FROM_SCENE(scene) (g_teams[(scene)->current_ship].loaded_model)

struct UndoOperation {
    struct Material *material;

    uint32_t *old_pixels;
    size_t old_pixels_length;

    struct UndoOperation *next;
};

struct UndoStep {
    char *label;
    struct UndoOperation *operations;

    struct UndoStep *next;
};

struct Undo {
    struct UndoStep *step;
};

enum Mode {
    MODE_EDITOR = 0,
    MODE_OVERVIEW,
    MODE_ABOUT,
};

struct Scene {
    int current_ship;
    int save_slot;

    float longitude;
    float target_longitude;
    float longitude_delta;
    float longitude_delta_target;
    float latitude;
    float target_latitude;
    float zoom;

    float dx;
    float dy;
    float target_dx;
    float target_dy;

    float overview_transition;
    float overview_transition_target;

    float about_transition;
    float about_transition_target;

    int mode;

    int overview_x;
    int overview_y;
    int overview_ww;
    int overview_hh;

    bool ortho;

    struct {
        bool inited;

        float longitude;
        float latitude;
        float zoom;
        float dx;
        float dy;
        bool ortho;

        uint32_t *pixels;
    } picking;

    struct {
        GLuint texture;
        uint32_t *pixels;
        int size;
        struct {
            int x;
            int y;
        } pos;
        bool want;
        bool visible;
    } magnifier;

    float time;

    struct Undo *undo;
};

static struct Scene *
g_scene = NULL;

struct LayoutItem {
    const char *name;
    struct Rect rect;
    int item;
    const char *tooltip;
};

enum {
    ITEM_WINDOW = 1,
    ITEM_SHIPVIEW,
    ITEM_TEXTURE,
    ITEM_TOGGLE_PROJECTION,
    ITEM_AUTO_MOVE,
    ITEM_ZOOM,
    ITEM_OPEN_PNG,
    ITEM_QUANTIZE_COLORS,
    ITEM_SAVE_PNG,
    ITEM_CHOOSE_COLOR,
    ITEM_BUILD_SAVEFILE,
    ITEM_NEXT_SHIP,
    ITEM_DEFAULT_SKIN,
    ITEM_ALTERNATIVE_SKIN,
    ITEM_ELIMINATOR_SKIN,
    ITEM_SAVE_SLOT,
    ITEM_RESET_VIEW,
    ITEM_ICON0_PREVIEW,
    ITEM_RENDER_UV_MAP,
    ITEM_UNDO,
    ITEM_MAGNIFIER,
    ITEM_ABOUT,
};

#define FLAG_SLIDER (1 << 16)
#define FLAG_BUTTON (1 << 17)

#define ITEM_ID(i) ((i)->item & ~(FLAG_SLIDER | FLAG_BUTTON))

static struct LayoutItem
layout[] = {
    { "",                { 0, 0, 1043, 406 }, ITEM_WINDOW, NULL }, // window background
    { "texture view",    { 769, 46, 256, 256 }, ITEM_TEXTURE, NULL }, // texture preview
    { "ship view",       { 137, 11, 616, 383 }, ITEM_SHIPVIEW, NULL }, // ship view
    { "icon0 preview",   { 768+113, 314, 144, 80}, ITEM_ICON0_PREVIEW, "Savegame icon preview"}, // icon0 preview

    { "Undo",            { 137 + 616 - 50, 11 + 10, 40, 40 }, ITEM_UNDO | FLAG_BUTTON, "Undo last texture-changing action" }, // undo
    { "save slot",       { 137 + 616 - 80 - 10, 11 + 383 - 20 - 10, 80, 20 }, ITEM_SAVE_SLOT | FLAG_BUTTON, "Slot to use when building savegame" }, // save slot

    { "Load Default",    { 769, 46 + 256 + 10 + 61 - 29 * 2, 105, 20 }, ITEM_DEFAULT_SKIN | FLAG_BUTTON, "Load default livery for current team" },
    { "Load Alternative", { 769, 46 + 256 + 10 + 61 - 29, 105, 20 }, ITEM_ALTERNATIVE_SKIN | FLAG_BUTTON, "Load alternative livery for current team" },
    { "Load Eliminator", { 769, 46 + 256 + 10 + 61, 105, 20 }, ITEM_ELIMINATOR_SKIN | FLAG_BUTTON, "Load eliminator livery for current team" },

    { "Load Debug UV Map", { 769, 11, 123, 20 }, ITEM_RENDER_UV_MAP | FLAG_BUTTON, "Render UV coordinates/colors onto the ship" },
    { "shipedit " VERSION, { 769+123+10, 11, 124, 20 }, ITEM_ABOUT | FLAG_BUTTON, "Information about this tool" },

    { "Select Team",     { 18, 11, 104, 20 }, ITEM_NEXT_SHIP | FLAG_BUTTON, "Select another team to edit" },

    { "",                { 18, 11 + 30, 104, 104 }, 0, NULL }, // pen configuration rectangle
    { "color",           { 18 + 8, 11 + 8 + 30, 88, 48 }, ITEM_CHOOSE_COLOR, "Pen preview (click to pick a new pen color)" }, // Pen Preview rectangle
    { "size",            { 18 + 8, 11 + 60 + 30, 88, 16 }, FLAG_SLIDER | 0, "Configure pen size" }, // slider for Pen Size
    { "opacity",         { 18 + 8, 11 + 80 + 30, 88, 16 }, FLAG_SLIDER | 1, "Configure pen opacity" }, // slider for Pen Opacity

    { "Load",            { 19, 139+20, 48, 48 }, ITEM_OPEN_PNG | FLAG_BUTTON, "Load a PNG, DAT or savegame (16034453) file" },
    { "Save",            { 74, 140+20, 48, 47 }, ITEM_SAVE_PNG | FLAG_BUTTON, "Save skin as PNG for sharing online" },
    { "Quant",           { 19, 195+20, 47, 47 }, ITEM_QUANTIZE_COLORS | FLAG_BUTTON, "Reduce the number of colors to 16 for saving as skin" },
    { "Build",           { 74, 194+20, 48, 49 }, ITEM_BUILD_SAVEFILE | FLAG_BUTTON, "Build savegame folder for using the skin in game" },
    { "Move",            { 18, 282+10, 49, 48 }, ITEM_AUTO_MOVE | FLAG_BUTTON, "Toggle automatic rotation of the ship 3D view" },
    { "Zoom",            { 75, 283+10, 47, 48 }, ITEM_ZOOM | FLAG_BUTTON, "Toggle between different fixed zoom levels" },
    { "Reset",           { 18, 338+10, 49, 48 }, ITEM_RESET_VIEW | FLAG_BUTTON, "Reset the ship 3D view" },
    { "Proj",            { 75, 338+10, 47, 46 }, ITEM_TOGGLE_PROJECTION | FLAG_BUTTON, "Toggle between perspective and orthographic projection" },

    { "Magnifier View",  { 768, 303, 256, 256 }, ITEM_MAGNIFIER, NULL },
};

static float
g_slider_values[] = {
    0.5f,
    0.8f,
};

static float *
g_pen_size_factor = &g_slider_values[0];

static float *
g_pen_alpha_factor = &g_slider_values[1];

static float
get_pen_size_factor()
{
    return 30.f * (0.01f + 0.99f * (*g_pen_size_factor));
}

static float
get_pen_alpha_factor()
{
    return (0.1f + (*g_pen_alpha_factor) * 0.9f);
}

static struct LayoutItem *
layout_active = NULL;

static struct LayoutItem *
layout_hover = NULL;

static struct LayoutItem *
layout_mouseover = NULL;

static struct LayoutItem *
window_layout = &layout[0];

static struct LayoutItem *
texture_layout = &layout[1];

static struct LayoutItem *
shipview_layout = &layout[2];

static struct LayoutItem *
icon0_preview_layout = &layout[3];

static struct LayoutItem *
magnifier_layout = &layout[sizeof(layout)/sizeof(layout[0])-1];

static struct LayoutItem *
drawing_on_item = NULL;


static void
layout_to_png(int w, int h, struct LayoutItem *item, const char *filename)
{
    uint32_t *buf = malloc(sizeof(uint32_t) * item->rect.w * item->rect.h);
    glReadPixels(item->rect.x, h - item->rect.y - item->rect.h, item->rect.w, item->rect.h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    //SDL_GL_SwapWindow(window);
    png_write_rgba(filename, item->rect.w, item->rect.h, buf, true);
    free(buf);
}


void
material_upload(struct Material *material)
{
    glBindTexture(GL_TEXTURE_2D, material->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, material->width, material->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, material->pixels);
}

void
material_plot(struct Material *material, int u, int v, float alpha)
{
    uint32_t *pixels_rgba = (uint32_t *)material->pixels;
    uint8_t *drawn_a = (uint8_t *)material->pixels_drawn;

    union {
        uint32_t u32;
        uint8_t u8[4];
    } color1, color2, color3;

    color1.u32 = 0xFF000000 | g_current_color;
    color2.u32 = pixels_rgba[v + u*material->width];

    alpha = 1.f - alpha;
    alpha *= get_pen_alpha_factor();

    uint8_t alpha_int = 255 * alpha;

    if (alpha_int < drawn_a[v + u*material->width]) {
        alpha_int = 0;
    }

    for (int i=0; i<3; ++i) {
        uint16_t v = (((uint16_t)color1.u8[i] * alpha_int) + ((uint16_t)color2.u8[i] * (255 - alpha_int))) / 255;
        if (v > 255) {
            v = 255;
        }
        color3.u8[i] = (uint8_t)v;
    }

    color3.u32 |= 0xFF000000;

    pixels_rgba[v + u*material->width] = color3.u32;
    drawn_a[v + u*material->width] += alpha_int;

    material->pixels_dirty = true;
}

void
undo_push(struct Undo *undo, const char *label)
{
    // TODO: Limit number of undo steps to not run out of memory
    struct UndoStep *step = calloc(1, sizeof(struct UndoStep));
    step->label = strdup(label);
    step->next = undo->step;
    undo->step = step;
}

void
undo_save_material_pixels(struct Undo *undo, struct Material *material)
{
    if (!undo->step) {
        // No undo step recording in process
        return;
    }

    struct UndoOperation *cur = undo->step->operations;
    while (cur != NULL) {
        if (cur->material == material) {
            // If the material was already saved in this step, we can skip it
            return;
        }

        cur = cur->next;
    }

    struct UndoOperation *op = calloc(1, sizeof(struct UndoOperation));

    op->material = material;

    op->old_pixels_length = sizeof(uint32_t) * material->width * material->height;
    op->old_pixels = malloc(op->old_pixels_length);
    memcpy(op->old_pixels, material->pixels, op->old_pixels_length);

    op->next = undo->step->operations;
    undo->step->operations = op;
}

void
undo_undo(struct Undo *undo)
{
    struct UndoStep *step = undo->step;

    if (step) {
        printf("Undoing: %s\n", step->label);
        struct UndoOperation *op = step->operations;
        while (op != NULL) {
            memcpy(op->material->pixels, op->old_pixels, op->old_pixels_length);
            material_upload(op->material);

            free(op->old_pixels);

            struct UndoOperation *next = op->next;
            free(op);
            op = next;
        }

        struct UndoStep *next = step->next;
        free(step->label);
        free(step);
        undo->step = next;
    } else {
        printf("Undo stack empty\n");
    }
}

void
undo_clear(struct Undo *undo)
{
    struct UndoStep *step = undo->step;

    undo->step = NULL;

    while (step) {
        printf("Pop from undo stack: %s\n", step->label);

        struct UndoOperation *op = step->operations;
        while (op != NULL) {
            free(op->old_pixels);

            struct UndoOperation *next = op->next;
            free(op);
            op = next;
        }

        struct UndoStep *next = step->next;
        free(step->label);
        free(step);
        step = next;
    }
}

char *
load_shipdat(const char *buffer, size_t buffer_len, int index, int *width, int *height, int *channels, int want_channels, uint32_t **palette)
{
    size_t expected = 32+index*(4*16+128*128/2)+128*128/2;
    if (buffer_len < expected) {
        fail("Buffer too short buffer_len=%d, expected=%d", buffer_len, expected);
    }

    *width = 128;
    *height = 128;
    *channels = 4;

    char *result = malloc((*width) * (*height) * (*channels));

    uint32_t *res_rgba = (uint32_t *)result;

    const char *read_ptr = buffer + 32+index*(4*16+128*128/2);
    *palette = malloc(4 * 16);

    memcpy(*palette, read_ptr, 4 * 16);
    read_ptr += 4 * 16;

    char *map = malloc((*width) * (*height) / 2);
    memcpy(map, read_ptr, (*width) * (*height) / 2);

    for (int y=0; y<*height; ++y) {
        for (int x=0; x<*width; ++x) {
            uint8_t pixel = map[(y*(*width) + x)/2];

            if (x % 2 == 0) {
                pixel >>= 4;
            } else {
                pixel &= 0x0F;
            }

            res_rgba[y*(*width) + x] = (*palette)[pixel];
        }
    }

    free(map);

    return result;
}

struct ShipModel *
parse_shm(const char *filename)
{
    struct ShipModel *model = malloc(sizeof(struct ShipModel));
    memset(model, 0, sizeof(*model));

    size_t len;
    char *dat = read_file(filename, &len);

    struct ShipModelHeader *smh = (struct ShipModelHeader *)dat;

    //printf("materials: %d\nobjects: %d\n", smh->n_materials, smh->n_objects);

    struct Material **material_slots = malloc(sizeof(struct Material *) * smh->n_materials);

    struct MaterialHeader *mh = (struct MaterialHeader *)(dat + sizeof(struct ShipModelHeader));
    for (int i=0; i<smh->n_materials; ++i) {
        struct Material *mat = malloc(sizeof(struct Material));
        memset(mat, 0, sizeof(struct Material));

        mat->name = "...";
        mat->index = mh[i].index;
        mat->is_cockpit_png = mh[i].is_cockpit_png;
        mat->is_canopy = mh[i].is_canopy;
        mat->is_other = mh[i].is_other;

        material_slots[i] = mat;
        mat->next = model->materials;
        model->materials = mat;
    }

    struct ObjectHeader *oh = (struct ObjectHeader *)(dat + sizeof(struct ShipModelHeader) + sizeof(struct MaterialHeader) * smh->n_materials);
    for (int i=0; i<smh->n_objects; ++i) {
        struct Object *obj = malloc(sizeof(struct Object));
        memset(obj, 0, sizeof(struct Object));

        obj->material = material_slots[oh[i].material_index];
        obj->vertexdata = (struct Vertex *)(dat + oh[i].vertexdata_offset);
        obj->vertexdata_size = oh[i].n_vertices;

        obj->next = model->objects;
        model->objects = obj;
    }

    free(material_slots);

    // keep a pointer to the data, even though we never free it
    model->temp = (struct ShipModelTemp *)dat;

    return model;
}

void
rgba32_flip_y(uint8_t *pixels, int width, int height)
{
    size_t scanline = width * sizeof(uint32_t);

    uint8_t *buf = malloc(scanline);
    for (int y=0; y<height/2; ++y) {
        uint8_t *src = pixels + scanline * y;
        uint8_t *dst = pixels + scanline * (height-1-y);

        memcpy(buf, src, scanline);
        memcpy(src, dst, scanline);
        memcpy(dst, buf, scanline);
    }
    free(buf);
}

void
instantiate_materials(struct ShipModel *model)
{
    struct Material *material = model->materials;
    while (material != NULL) {
        if (material->index != -1 || material->is_cockpit_png) {
            if (material->is_cockpit_png) {
                material->pixels = png_load_rgba("data/editor/cockpit.png", &material->width, &material->height, &material->channels);
                rgba32_flip_y(material->pixels, material->width, material->height);
            } else {
                material->width = material->height = 128;
                material->pixels = calloc(material->width * material->height, sizeof(uint32_t));
            }

            // 8 bpp how much the pixel has been drawn/blended during this draw operation
            material->pixels_drawn = malloc(sizeof(uint8_t) * material->width * material->height);

            {
                glGenTextures(1, &material->texture);
                glBindTexture(GL_TEXTURE_2D, material->texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);

                glBindTexture(GL_TEXTURE_2D, material->texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, material->width, material->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, material->pixels);
            }

            {
                glGenTextures(1, &material->picker_texture);
                glBindTexture(GL_TEXTURE_2D, material->picker_texture);
                // Must be NEAREST, as we're using it for color picking
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);

                int index = material->index;
                if (index == -1) {
                    index = 7;
                } else {
                    index++;
                }

                uint32_t *tmp = malloc(material->width * material->height * 4);
                for (uint32_t y=0; y<material->height; ++y) {
                    for (uint32_t x=0; x<material->width; ++x) {
                        tmp[y*material->width+x] = 0xFF000000 | ((x<<1) << 16) | ((y<<1) << 8) | (index << 5);
                    }
                }

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, material->width, material->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp);

                free(tmp);
            }
        }

        material = material->next;
    }
}

void
draw_rect(float x, float y, float w, float h)
{
    struct Vec2 vertices[] = {
        { x,   y,   },
        { x+w, y,   },
        { x,   y+h, },
        { x+w, y+h, },
    };

    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(struct Vec2), &vertices[0].x);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableClientState(GL_VERTEX_ARRAY);
}

void
draw_grid(float x, float y, float w, float h, float size)
{
    float xoff = fmodf(w, size) / 2.f;
    float yoff = fmodf(h, size) / 2.f;

    for (int yy=0; yy<(h+yoff+size)/size; ++yy) {
        for (int xx=0; xx<(w+xoff+size)/size; ++xx) {
            if ((xx%2) != (yy%2)) {
                float nx = x+xx*size + xoff - size;
                float ny = y+yy*size + yoff - size;
                float nw = size;
                float nh = size;
                if (nx < x) {
                    nw -= (x - nx);
                    nx = x;
                }
                if (ny < y) {
                    nh -= (y - ny);
                    ny = y;
                }
                if (nx + nw > x+w) {
                    nw = x+w-nx;
                }
                if (ny + nh > y+h) {
                    nh = y+h-ny;
                }
                if (nw > 0 && nh > 0) {
                    draw_rect(nx, ny, nw, nh);
                }
            }
        }
    }
}

void
draw_circle(float x, float y, float radius)
{
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    float color[4];
    glGetFloatv(GL_CURRENT_COLOR, color);

    float ri = fmaxf(2.f, radius * 0.8f);
    float ro = radius;

    int steps = 24;

    glBegin(GL_TRIANGLES);
    for (int i=0; i<steps; ++i) {
        float dx = sinf(i * 360.f / steps / 180.f * M_PI);
        float dy = cosf(i * 360.f / steps / 180.f * M_PI);

        float dx2 = sinf((i+1) * 360.f / steps / 180.f * M_PI);
        float dy2 = cosf((i+1) * 360.f / steps / 180.f * M_PI);

        glColor4f(color[0], color[1], color[2], color[3]);
        glVertex2f(x, y);
        glVertex2f(x + dx * ri, y + dy * ri);
        glVertex2f(x + dx2 * ri, y + dy2 * ri);

        glVertex2f(x + dx * ri, y + dy * ri);
        glColor4f(color[0], color[1], color[2], 0.f);
        glVertex2f(x + dx * ro, y + dy * ro);
        glColor4f(color[0], color[1], color[2], color[3]);
        glVertex2f(x + dx2 * ri, y + dy2 * ri);

        glColor4f(color[0], color[1], color[2], color[3]);
        glVertex2f(x + dx2 * ri, y + dy2 * ri);
        glColor4f(color[0], color[1], color[2], 0.f);
        glVertex2f(x + dx2 * ro, y + dy2 * ro);
        glVertex2f(x + dx * ro, y + dy * ro);
    }
    glEnd();

    glColor4f(color[0], color[1], color[2], color[3]);
}

struct TexVertex {
    struct Vec2 pos;
    struct Vec2 tex;
};

void
draw_with_font_xy(struct InMemoryFont *font, float x, float y, const char *text)
{
    if (text[0] == '\0') {
        return;
    }

    int w, h;
    uint8_t *pixels = in_memory_font_render_rgba(font, text, &w, &h);

    int w2 = 1;
    while (w2 < w) {
        w2 *= 2;
    }
    int h2 = 1;
    while (h2 < h) {
        h2 *= 2;
    }

    float sw = (float)w / (float)w2;
    float sh = (float)h / (float)h2;

    struct TexVertex vertices[] = {
        { { (float)x,          (float)y          }, { 0.f, 0.f } },
        { { (float)x+(float)w, (float)y          }, { sw,  0.f } },
        { { (float)x,          (float)y+(float)h }, { 0.f, sh  } },
        { { (float)x+(float)w, (float)y+(float)h }, { sw,  sh  } },
    };

    GLuint tex;
    glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w2, h2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(struct TexVertex), &vertices[0].tex.u);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(struct TexVertex), &vertices[0].pos.x);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    in_memory_font_render_rgba_free(font, pixels);

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
}

void
draw_with_font(struct InMemoryFont *font, struct Rect *rect, const char *text)
{
    int w, h;
    in_memory_font_measure(font, text, &w, &h);

    int x = rect->x + (rect->w - w) / 2;
    int y = rect->y + (rect->h - h - 2) / 2;

    draw_with_font_xy(font, x, y, text);
}

void
draw_floor()
{
    struct Vec3 tile[] = {
        { -0.5f, 0.f, -0.5f },
        { -0.5f, 0.f, +0.5f },
        { +0.5f, 0.f, -0.5f },
        { +0.5f, 0.f, +0.5f },
    };

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(struct Vec3), &tile[0].x);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glScalef(5.f, 1.f, 5.f);
    for (int y=0; y<10; ++y) {
        for (int x=0; x<10; ++x) {
            if ((x ^ y) & 1) {
                glPushMatrix();
                glTranslatef(x-5.f, 0.f, y-5.f);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glPopMatrix();
            }
        }
    }
    glPopMatrix();
}

void
render_shipview(struct Scene *scene, struct ShipModel *model, int w, int h, bool picking, bool overview)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    static float s_projection[16];
    static bool s_projection_inited = false;

    if (scene->ortho) {
        float s = 2.f + scene->zoom * 0.1f;
        float t = s * (float)h / (float)w;
        glOrtho(-s, s, -t, t, -100.f, 200.f);
    } else {
        gluPerspective(scene->zoom, 1.1f * w / h, .01f, 3000.f);
    }

    if (!s_projection_inited) {
        glGetFloatv(GL_PROJECTION_MATRIX, s_projection);
        s_projection_inited = true;
    } else {
        float tmp[16];
        glGetFloatv(GL_PROJECTION_MATRIX, tmp);

        float alpha = g_batch_mode ? 0.f : 0.9f;
        for (int i=0; i<16; ++i) {
            tmp[i] = alpha * s_projection[i] + (1.f - alpha) * tmp[i];
        }

        scene->dx = alpha * scene->dx + (1.f - alpha) * scene->target_dx;
        scene->dy = alpha * scene->dy + (1.f - alpha) * scene->target_dy;
        scene->latitude = alpha * scene->latitude + (1.f - alpha) * scene->target_latitude;
        scene->longitude = alpha * scene->longitude + (1.f - alpha) * scene->target_longitude;

        glLoadMatrixf(tmp);
        glGetFloatv(GL_PROJECTION_MATRIX, s_projection);
    }

    enum { DRAW_REFLECTION, DRAW_SHIP, DRAW_LINES };

    for (int i=DRAW_SHIP; i<=(picking?DRAW_SHIP:DRAW_LINES); ++i) {
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        float height = 16.f * scene->latitude;
        float height_factor = (1.f - height / 16.f);
        // This "4.f" is just here so we can avoid gimbal lock;
        // once we fix the "up" vector properly, no need for it
        float dist = 4.f + 10.f * height_factor;
        struct Vec3 up = { 0.f, 1.f, 0.f };
        if (i==DRAW_REFLECTION) {
            // reflection
            glScalef(1.f, -1.f, 1.f);
            float darken = 0.2f;
            glColor4f(darken, darken, darken, scene->latitude);
            glEnable(GL_BLEND);
        } else if (i == DRAW_SHIP) {
            // normal
            //glClear(GL_DEPTH_BUFFER_BIT);
            glColor4f(1.f, 1.f, 1.f, 1.f);
        } else if (i == DRAW_LINES) {
            glColor4f(0.f, 0.f, 0.f, 1.f);
        }

        if (i==DRAW_SHIP) {
            glPushMatrix();
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);

            glTranslatef(scene->dx, scene->dy, 0.f);
            gluLookAt(dist * sinf(scene->longitude), height, dist * cosf(scene->longitude),
                      0.f, 0.f, 0.f,
                      up.x, up.y, up.z);

            float darken = 0.6f * scene->latitude;
            glColor4f(0.f, 0.f, 0.f, darken);
            if (!overview) {
                draw_floor();
            }

            glColor4f(1.f, 1.f, 1.f, 1.f);
            glDisable(GL_BLEND);
            glPopMatrix();
        }

        glTranslatef(scene->dx, scene->dy, 0.f);
        gluLookAt(dist * sinf(scene->longitude), height, dist * cosf(scene->longitude),
                  0.f, 0.f, 0.f,
                  up.x, up.y, up.z);

        glEnable(GL_DEPTH_TEST);

        struct Object *cur = model->objects;

        while (cur) {
            // Skip cockpit and draw it later (transparent sorting)
            if (cur->material && cur->material->is_canopy) {
                cur = cur->next;
                continue;
            }

            bool have_material = cur->material && cur->material->pixels;

            if (i == DRAW_LINES) {
                if (!have_material) {
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    glEnable(GL_DEPTH_TEST);
                    glDisable(GL_TEXTURE_2D);
                    glEnable(GL_POLYGON_OFFSET_LINE);
                    glPolygonOffset(0.f, -1.f);
                } else {
                    cur = cur->next;
                    continue;
                }
            }

            if (have_material) {
                glEnable(GL_TEXTURE_2D);
                if (picking) {
                    glBindTexture(GL_TEXTURE_2D, cur->material->picker_texture);
                } else {
                    glBindTexture(GL_TEXTURE_2D, cur->material->texture);
                }
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, sizeof(struct Vertex), &cur->vertexdata[0].u);
            } else {
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }

            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(3, GL_FLOAT, sizeof(struct Vertex), &cur->vertexdata[0].x);

            glDrawArrays(GL_TRIANGLES, 0, cur->vertexdata_size);

            if (i == DRAW_LINES) {
                glDisable(GL_POLYGON_OFFSET_LINE);
            }

            cur = cur->next;
        }

        // Draw transparent cockpit (if any)

        glEnable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glColor4f(0.3f, 0.9f, 0.9f, 0.5f);

        cur = model->objects;

        while (cur) {
            if (cur->material && cur->material->is_canopy && i != DRAW_LINES) {
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(3, GL_FLOAT, sizeof(struct Vertex), &cur->vertexdata[0].x);

                glDrawArrays(GL_TRIANGLES, 0, cur->vertexdata_size);
            }

            cur = cur->next;
        }

        glColor4f(1.f, 1.f, 1.f, 1.f);

        glDisable(GL_DEPTH_TEST);
        glPopMatrix();
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

void
slider(struct LayoutItem *item, bool render)
{
    int x = item->rect.x;
    int y = item->rect.y;
    int h = item->rect.h;

    // draw slider line
    int padding = 0;
    int sl_height = 2;

    // slider line
    struct Rect sl = { x + padding, y + h / 2 - 1, item->rect.w - 2 * padding, sl_height };

    int index = ITEM_ID(item);

    // slider button
    float value = g_slider_values[index];

    int sb_width = 8;
    int sb_height = h;
    struct Rect sb = { x + padding + (sl.w - sb_width) * value, y + (h - sb_height) / 2, sb_width, sb_height };

    if (render) {
        glEnable(GL_BLEND);
        glColor4f(1.f, 1.f, 1.f, .5f);
        draw_rect(sl.x, sl.y, sl.w, sl.h);

        glColor4f(1.f, 1.f, 1.f, 1.f);
        draw_rect(sb.x, sb.y, sb.w, sb.h);
    } else {
        sl.y = item->rect.y;
        sl.h = item->rect.h;
        int mx = g_mouse.x;
        int my = g_mouse.y;

        if (rect_contains(&sl, g_mouse.down_location.x, g_mouse.down_location.y)) {
            g_slider_values[index] = fminf(1.f, fmaxf(0.f, (float)(mx - sl.x) / (float)sl.w));
        }
    }
}

// https://gist.github.com/postspectacular/2a4a8db092011c6743a7

// HSV->RGB conversion based on GLSL version
// expects hsv channels defined in 0.0 .. 1.0 interval
float fract(float x) { return x - (int)x; }
float mix(float a, float b, float t) { return a + (b - a) * t; }
float step(float e, float x) { return x < e ? 0.0 : 1.0; }
float constrain(float f, float min, float max) { return fminf(max, fmaxf(min, f)); }

void hsv2rgb(float h, float s, float b, float* rgb)
{
  rgb[0] = b * mix(1.0, constrain(fabsf(fract(h + 1.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
  rgb[1] = b * mix(1.0, constrain(fabsf(fract(h + 0.6666666) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
  rgb[2] = b * mix(1.0, constrain(fabsf(fract(h + 0.3333333) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
}

void
button(struct Scene *scene, struct LayoutItem *item)
{
    bool pressed = (layout_hover == item);
    bool hovering = (layout_mouseover == item);

    intptr_t offset = item - layout;

    float rgb[3];

    hsv2rgb(offset*0.11f+SDL_GetTicks()*0.0001f, 0.4f, 0.4f, rgb);

    float r = rgb[0];
    float g = rgb[1];
    float b = rgb[2];

    float lighter = 0.7f + 0.08f * fabsf(sinf((SDL_GetTicks() - g_mouse.last_movement) * 0.004f));

#define LIGHTER(x) (1.f - lighter * (1.f - (x)))
#define DARKER(x) (0.8f * (x))

#define MUCHLIGHTER(x) (1.f - 0.5f * (1.f - (x)))
#define MUCHDARKER(x) (0.5f * (x))

    if (pressed) {
        glColor4f(MUCHDARKER(r), MUCHDARKER(g), MUCHDARKER(b), 1.f);
    } else {
        glColor4f(MUCHLIGHTER(r), MUCHLIGHTER(g), MUCHLIGHTER(b), 1.f);
    }
    draw_rect(item->rect.x+1, item->rect.y, item->rect.w-2, 1);
    draw_rect(item->rect.x, item->rect.y+1, 1, item->rect.h-2);

    if (!pressed) {
        glColor4f(MUCHDARKER(r), MUCHDARKER(g), MUCHDARKER(b), 1.f);
    } else {
        glColor4f(MUCHLIGHTER(r), MUCHLIGHTER(g), MUCHLIGHTER(b), 1.f);
    }
    draw_rect(item->rect.x+1, item->rect.y+item->rect.h-1, item->rect.w-2, 1);
    draw_rect(item->rect.x+item->rect.w-1, item->rect.y+1, 1, item->rect.h-2);

    if (pressed) {
        glColor4f(DARKER(r), DARKER(g), DARKER(b), 1.f);
    } else if (hovering) {
        glColor4f(LIGHTER(r), LIGHTER(g), LIGHTER(b), 1.f);
    } else {
        glColor4f(r, g, b, 1.f);
    }

    draw_rect(item->rect.x+1, item->rect.y+1, item->rect.w-2, item->rect.h-2);
    glColor4f(MUCHLIGHTER(r), MUCHLIGHTER(g), MUCHLIGHTER(b), 1.f);
    struct Rect txtr = item->rect;
    if (pressed) {
        txtr.x += 1;
        txtr.y += 1;
    }

    const char *label = item->name;
    char save_slot_label[16];
    if (ITEM_ID(item) == ITEM_SAVE_SLOT) {
        // hackish but does the job
        sprintf(save_slot_label, "Slot %04d", scene->save_slot);
        label = save_slot_label;
    }
    draw_with_font(g_font_gui, &txtr, label);

#undef DARKER
#undef LIGHTER
#undef MUCHDARKER
#undef MUCHLIGHTER
}

static const char *
about_lines[] = {
    "shipedit " VERSION,
    "(c) 2021, 2022 Thomas Perl <m@thp.io> -- https://thp.io/2021/shipedit/",
    "",
    "This is an unofficial/fan-made ship skin editor for the 2007 PSP game WipEout Pulse.",
    "",
    "To load default liveries, copy fedata.wad (from the main game) and optionally pack1_ui1.edat,",
    "pack2_ui1.edat, pack3_ui1.edat, pack4_ui1.edat (from the DLCs) into the current directory.",
    "",
    "  [m] ... Toggle magnifier",
    "  [right mouse button] or [left mouse button + CTRL] ... Rotate view",
    "  [middle mouse button] or [left mouse button + ALT] ... Pan view",
    "  [q] ... Exit",
    "",
    "Open source code used:",
    "",
    "psp-save -- https://github.com/38-vita-38/psp-save",
    "  chnnlsv.c (GNU GPLv2 or later) (c) 2012- PPSSPP Project",
    "  hash.c, psf.c (BSD) (c) 2005 Jim Paris <jim@jtan.com>, psp123",
    "  psp-save.c (GNU GPLv2 or later, BSD) (c) 2018 38_ViTa_38 (based on PSPSDK code)",
    "",
    "libkirk -- https://github.com/hrydgard/ppsspp",
    "  AES.c (Public Domain) Authors: Vincent Rijmen, Antoon Bosselaers, Paulo Barreto.",
    "  SHA1.c David Ireland, adapted from code by A.M. Kuchling 1995, based on Peter Gutmann's code",
    "  kirk_engine.c (GNU GPLv3 or later) by Draan with help from community members (see source)",
    "  bn.c (GNU GPLv2) Copyright 2007,2008,2010  Segher Boessenkool  <segher@kernel.crashing.org>",
    "  ec.c (GNU GPLv2) Copyright 2007,2008,2010  Segher Boessenkool  <segher@kernel.crashing.org>",
    "",
    "scolorq (MIT license) -- Copyright (c) 2006 Derrick Coetzee",
    "Native File Dialog (zlib license) -- Copyright 2014-2019 Frogtoss Games, Inc.",
    "",
    "Also uses zlib (1995-2017 Jean-loup Gailly and Mark Adler), libpng (1995-2019 PNG Authors)",
    "and SDL2 (1997-2020 Sam Lantinga) under a zlib-style license.",
};


void
scene_render(struct Scene *scene, int w, int h, float t, bool picking)
{
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.f, w, h, 0.f, -1.f, +1.f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (scene->about_transition < 0.01f && scene->about_transition_target == 0.f && scene->mode == MODE_ABOUT) {
        scene->mode = MODE_EDITOR;
    }

    if (scene->overview_transition < 0.1f && scene->mode == MODE_OVERVIEW) {
        scene->mode = MODE_EDITOR;
    } else if (scene->overview_transition > 0.1f) {
        scene->mode = MODE_OVERVIEW;

        struct LayoutItem *item = layout + 2;

        scene->overview_ww = item->rect.w / 3;
        scene->overview_hh = item->rect.h / 3;
        scene->overview_x = (w - scene->overview_ww * 4) / 2;
        scene->overview_y = (h - scene->overview_hh * 3) / 2;
        for (int yy=0; yy<3; ++yy) {
            for (int xx=0; xx<4; ++xx) {
                int tw = scene->overview_ww * scene->overview_transition;
                int th = scene->overview_hh * scene->overview_transition;

                int index = yy*4+xx;

                int x0 = scene->overview_x + scene->overview_ww * xx;
                int x = x0 + (scene->overview_ww - tw) / 2;
                int y0 = scene->overview_y + scene->overview_hh * yy;
                int y = y0 + (scene->overview_hh - th) / 2;

                glViewport(x0, h-scene->overview_hh-y0, scene->overview_ww, scene->overview_hh);
                glScissor(x, h-th-y, tw, th);
                glEnable(GL_SCISSOR_TEST);
                //glClearColor(0.4f, 0.3f, 0.4f, 1.f);
                glClearColor(0.1f + 0.3f * sinf(scene->time*0.1f + yy*4+xx), 0.2f, 0.2f + 0.1f * (xx % 2) + 0.1f * (yy % 2), 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                render_shipview(scene, g_teams[yy*4+xx].loaded_model, scene->overview_ww, scene->overview_hh, picking, true);

                glViewport(0, 0, w, h);

                if (scene->current_ship == index) {
                    float intensity = fabsf(sinf(scene->time));
                    glColor4f(intensity, intensity, intensity, scene->overview_transition);
                    draw_rect(x0, y0, scene->overview_ww, 2);
                    draw_rect(x0, y0+scene->overview_hh-3, scene->overview_ww, 2);
                    draw_rect(x0, y0, 2, scene->overview_hh);
                    draw_rect(x0+scene->overview_ww-3, y0, 2, scene->overview_hh);
                }

                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0.f, w, h, 0.f, -1.f, +1.f);

                glEnable(GL_BLEND);

                int lw, lh;
                const char *label = g_teams[yy*4+xx].team_label;
                in_memory_font_measure(g_font_heading, label, &lw, &lh);

                int padding = 10;

                glColor4f(0.f, 0.f, 0.f, 0.8f * scene->overview_transition);
                draw_rect(x0 + (scene->overview_ww-lw) / 2 - padding / 2, y0 + scene->overview_hh - lh - 10 - padding / 2 + 3, lw+padding, lh+padding);

                glColor4f(1.f, 1.f, 1.f, scene->overview_transition);
                draw_with_font_xy(g_font_heading, x0 + (scene->overview_ww-lw) / 2, y0 + scene->overview_hh - lh - 10, label);

                glDisable(GL_SCISSOR_TEST);
                glScissor(0, 0, w, h);
            }
        }
    }

    if (scene->mode == MODE_EDITOR || scene->mode == MODE_ABOUT) {
        for (int i=0; i<sizeof(layout)/sizeof(layout[0]); ++i) {
            struct LayoutItem *item = layout + i;

            if (ITEM_ID(item) == ITEM_TOGGLE_PROJECTION) {
                item->name = scene->ortho ? "Ortho" : "Persp";
            }

            if ((item->item & FLAG_SLIDER) != 0) {
                slider(item, true);
            } else if ((item->item & FLAG_BUTTON) != 0) {
                button(scene, item);
            } else if (ITEM_ID(item) == ITEM_CHOOSE_COLOR) {
                float b = ((g_current_color >> 16) & 0xFF) / 255.f;
                float g = ((g_current_color >> 8) & 0xFF) / 255.f;
                float r = ((g_current_color >> 0) & 0xFF) / 255.f;

                static float bgcolor = 1.f;
                static float bgcolor_target = 1.f;

                float alpha = 0.95f;
                bgcolor = alpha * bgcolor + (1.f - alpha) * bgcolor_target;

                if (((r + g + b) / 3.f) > 0.5f) {
                    bgcolor_target = 0.f;
                } else {
                    bgcolor_target = 1.f;
                }

                glColor4f(bgcolor, bgcolor, bgcolor, 1.f);
                draw_rect(item->rect.x, item->rect.y, item->rect.w, item->rect.h);

                float grid_color = 0.5f - 0.5f * (0.5f - bgcolor);

                glColor4f(grid_color, grid_color, grid_color, 1.f);
                draw_grid(item->rect.x, item->rect.y, item->rect.w, item->rect.h, 13.f);

                glColor4f(r, g, b, 0.5f + 0.5f * get_pen_alpha_factor());

                glScissor(item->rect.x, h-item->rect.h-item->rect.y, item->rect.w, item->rect.h);
                glEnable(GL_SCISSOR_TEST);

                glEnable(GL_BLEND);
                draw_circle(item->rect.x + item->rect.w / 2.f,
                            item->rect.y + item->rect.h / 2.f,
                            get_pen_size_factor());

                glDisable(GL_SCISSOR_TEST);
            } else if (ITEM_ID(item) == ITEM_SHIPVIEW) {
                glViewport(item->rect.x, h-item->rect.h-item->rect.y, item->rect.w, item->rect.h);
                glScissor(item->rect.x, h-item->rect.h-item->rect.y, item->rect.w, item->rect.h);
                glEnable(GL_SCISSOR_TEST);
                glClearColor(0.2f, 0.2f, 0.2f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                render_shipview(scene, SHIP_FROM_SCENE(scene), item->rect.w, item->rect.h, picking, false);
                glDisable(GL_SCISSOR_TEST);
                glViewport(0, 0, w, h);
            } else if (ITEM_ID(item) == ITEM_ICON0_PREVIEW) {
                glViewport(item->rect.x, h-item->rect.h-item->rect.y, item->rect.w, item->rect.h);
                glScissor(item->rect.x, h-item->rect.h-item->rect.y, item->rect.w, item->rect.h);
                glEnable(GL_SCISSOR_TEST);
                glClearColor(0.f, 0.f, 0.f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                render_shipview(scene, SHIP_FROM_SCENE(scene), item->rect.w, item->rect.h, false, false);
                glDisable(GL_SCISSOR_TEST);
                glViewport(0, 0, w, h);

                int tw, th;
                in_memory_font_measure(g_font_heading, g_teams[scene->current_ship].team_label, &tw, &th);
                glColor4f(1.f, 1.f, 1.f, 1.f);
                draw_with_font_xy(g_font_gui, item->rect.x + 4, item->rect.y + item->rect.h - th + 2, g_teams[scene->current_ship].team_label);
            } else if (ITEM_ID(item) == ITEM_MAGNIFIER && !picking) {
                if (scene->magnifier.visible && scene->magnifier.want) {
                    glReadPixels(scene->magnifier.pos.x, h-scene->magnifier.size-scene->magnifier.pos.y, scene->magnifier.size, scene->magnifier.size, GL_RGBA, GL_UNSIGNED_BYTE,
                            scene->magnifier.pixels);
                    glBindTexture(GL_TEXTURE_2D, scene->magnifier.texture);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scene->magnifier.size, scene->magnifier.size, 0, GL_RGBA, GL_UNSIGNED_BYTE, scene->magnifier.pixels);

                    struct Vertex vertices[] = {
                        { item->rect.x,              item->rect.y,              0.f,   0.f, 1.f },
                        { item->rect.x+item->rect.w, item->rect.y,              0.f,   1.f, 1.f },
                        { item->rect.x,              item->rect.y+item->rect.h, 0.f,   0.f, 0.f },
                        { item->rect.x+item->rect.w, item->rect.y+item->rect.h, 0.f,   1.f, 0.f },
                    };

                    glEnable(GL_TEXTURE_2D);

                    glEnableClientState(GL_VERTEX_ARRAY);
                    glVertexPointer(3, GL_FLOAT, sizeof(struct Vertex), &vertices[0].x);

                    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                    glTexCoordPointer(2, GL_FLOAT, sizeof(struct Vertex), &vertices[0].u);

                    glColor4f(1.f, 1.f, 1.f, 1.f);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                    glDisableClientState(GL_VERTEX_ARRAY);
                    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

                    glColor4f(0.f, 0.f, 0.f, 1.f);
                    draw_rect(item->rect.x + (item->rect.w * 3/4) / 2, item->rect.y + item->rect.h / 2, item->rect.w / 4, 1);
                    draw_rect(item->rect.x + item->rect.w / 2, item->rect.y + (item->rect.h * 3/4) / 2, 1, item->rect.h / 4);
                    draw_rect(item->rect.x - 1, item->rect.y - 1, item->rect.w + 2, 1);
                    draw_rect(item->rect.x - 1, item->rect.y + item->rect.h, item->rect.w + 2, 1);
                    draw_rect(item->rect.x - 1, item->rect.y - 1, 1, item->rect.h + 2);
                    draw_rect(item->rect.x + item->rect.w, item->rect.y - 1, 1, item->rect.h + 2);
                }
            } else if (ITEM_ID(item) == ITEM_TEXTURE) {
                int x = item->rect.x;
                int y = item->rect.y;

                struct Material *mat = SHIP_FROM_SCENE(scene)->materials;
                while (mat) {
                    if (mat->index != -1 || mat->is_cockpit_png) {
                        int mat_index = mat->is_cockpit_png ? 3 : mat->index;

                        glEnable(GL_TEXTURE_2D);

                        if (picking) {
                            glBindTexture(GL_TEXTURE_2D, mat->picker_texture);
                        } else {
                            glBindTexture(GL_TEXTURE_2D, mat->texture);
                        }

                        x = item->rect.x + (mat_index % 2) * 128;
                        y = item->rect.y + (mat_index / 2) * 128;

                        struct Vertex vertices[] = {
                            { x,            y,             0.f,   0.f, 1.f },
                            { x+mat->width, y,             0.f,   1.f, 1.f },
                            { x,            y+mat->height, 0.f,   0.f, 0.f },
                            { x+mat->width, y+mat->height, 0.f,   1.f, 0.f },
                        };

                        glEnableClientState(GL_VERTEX_ARRAY);
                        glVertexPointer(3, GL_FLOAT, sizeof(struct Vertex), &vertices[0].x);

                        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                        glTexCoordPointer(2, GL_FLOAT, sizeof(struct Vertex), &vertices[0].u);

                        glColor4f(1.f, 1.f, 1.f, 1.f);
                        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    }

                    mat = mat->next;
                }
            } else {
                if (!picking) {
                    float darken = (layout_hover == item) ? 0.3f : 0.1f;
                    glColor4f(darken*0.9f, darken*0.7f, darken, 1.f);
                    draw_rect(item->rect.x, item->rect.y, item->rect.w, item->rect.h);
                    glColor4f(1.f, 1.f, 1.f, 1.f);
                    draw_with_font(g_font_gui, &item->rect, item->name);
                }
            }
        }
    }

    if (scene->mode == MODE_EDITOR) {
        if (g_mouse.tooltip && g_mouse.last_movement + 200 < SDL_GetTicks()) {
            int tooltip_w, tooltip_h;
            in_memory_font_measure(g_font_gui, g_mouse.tooltip, &tooltip_w, &tooltip_h);
            tooltip_w += 3;
            tooltip_h += 3;

            int x = g_mouse.x + 20;
            int y = g_mouse.y;

            if (x + tooltip_w > w - 10) {
                x = w - 10 - tooltip_w;
                y -= 20;
            }

            if (y + tooltip_h > h - 40) {
                y = h - 40 - tooltip_h;
            }

            if (y + tooltip_h < 40) {
                y = 40;
            }
            float opacity = fminf(1.f, (SDL_GetTicks() - g_mouse.last_movement - 200) / 500.f);

            glEnable(GL_BLEND);

            glColor4f(0.f, 0.f, 0.f, opacity * 0.8f);
            draw_rect(x, y, tooltip_w, tooltip_h);

            glColor4f(1.f, 1.f, 1.f, opacity);
            draw_with_font_xy(g_font_gui, x + 1, y + 1, g_mouse.tooltip);

            glDisable(GL_BLEND);
        }
    }

    if (scene->mode == MODE_ABOUT || scene->mode == MODE_EDITOR) {
        glColor4f(1.f, 1.f, 1.f, 1.f - scene->about_transition);
        glEnable(GL_BLEND);
        draw_with_font_xy(g_font_heading, shipview_layout->rect.x + 8, shipview_layout->rect.y + shipview_layout->rect.h - 28, g_teams[scene->current_ship].team_label);
    }

    if (scene->mode == MODE_ABOUT) {
        glEnable(GL_BLEND);
        glColor4f(0.f, 0.f, 0.f, 0.9f * scene->about_transition);
        draw_rect(0, 0, w, h);

        glColor4f(1.f, 1.f, 1.f, 1.f * scene->about_transition);

        int x = 15 + shipview_layout->rect.x;
        int y = 10 + shipview_layout->rect.y;

        for (int i=0; i<sizeof(about_lines)/sizeof(about_lines[0]); ++i) {
            if (!about_lines[i][0]) {
                y += 6;
                continue;
            }

            if (i == 0) {
                draw_with_font_xy(g_font_heading, x, y, about_lines[i]);
                y += 20;
            } else {
                draw_with_font_xy(g_font_gui, x, y, about_lines[i]);
                y += 12;
            }
        }
    }
}

/**
 * This is simply 8-round XTEA encryption.
 *
 * See also:
 *    https://en.wikipedia.org/wiki/XTEA
 *    https://cryptography.fandom.com/wiki/XTEA
 **/
static void
xtea8(uint32_t *genkey, const uint32_t *key)
{
    uint32_t v0 = genkey[0];
    uint32_t v1 = genkey[1];

    const uint32_t k = 0x9e3779b9;
    const uint32_t num_rounds = 8;

    uint32_t sum = 0;

    for (uint32_t i=0; i<num_rounds; ++i) {
        v0 += (((v1 << 4 ^ v1 >> 5) + v1) ^ (key[sum&3] + sum));
        sum += k;
        v1 += (((v0 << 4 ^ v0 >> 5) + v0) ^ (key[(sum>>11)&3] + sum));
    }

    genkey[0] = v0;
    genkey[1] = v1;
}

static bool
meta_png_io(struct Scene *scene, uint32_t *rgba, int w, int h, bool writemode)
{
    struct PackedMeta {
        uint32_t sync;
        char team_name[16];
        float longitude;
        float latitude;
        float zoom;
        float dx;
        float dy;
        uint8_t ortho;
        uint8_t save_slot;
        uint8_t padding[2];
        uint32_t crc;
    } meta;

    static const char *key = "SceneMetaPixels!";
    static const uint32_t crckey = 0x3F706874;

    memset(&meta, 0, sizeof(meta));

    if (writemode) {
        meta.sync = 0xb4ccf00f;
        strncpy(meta.team_name, g_teams[scene->current_ship].team_name, 16);
        meta.longitude = scene->target_longitude;
        meta.latitude = scene->target_latitude;
        meta.zoom = scene->zoom;
        meta.dx = scene->target_dx;
        meta.dy = scene->target_dy;
        meta.ortho = scene->ortho;
        meta.save_slot = scene->save_slot;

        meta.crc = crc32(crckey, (const uint8_t *)&meta, sizeof(meta)-4);
        for (int i=1; i<11; ++i) {
            uint32_t *read = ((uint32_t *)&meta) + i;
            uint32_t genkey[2];
            genkey[0] = meta.crc;
            genkey[1] = i;
            xtea8(genkey, (const uint32_t *)key);
            *read++ ^= genkey[0];
        }
    }

    uint8_t *buf = (uint8_t *)&meta;
    size_t len = sizeof(meta);

    uint8_t value_one = 0x0F;
    uint8_t value_zero = 0xFF;

    int columns = 108;
    for (int i=0; i<len; ++i) {
        for (int j=0; j<8; ++j) {
            int off = (i * 8 + j) * 2;
            int rgba_offset = (128 + 6+12 + 6 + (int)(off/columns)) * w + 128 + 10 + (off % columns);

            if (writemode) {
                uint8_t ch = (buf[i] & (1 << (7-j))) ? value_one : value_zero;
                uint32_t color = 0xFF000000 | ch | (ch << 8) | (ch << 16);
                rgba[rgba_offset] = color;
            } else {
                uint32_t color = rgba[rgba_offset];

                // Read color from sync pattern
                if (i == 0 && j == 0) {
                    value_zero = (color & 0xFF);
                } else if (i == 0 && j == 4) {
                    value_one = (color & 0xFF);
                }

                if ((color & 0xFF) == value_one) {
                    buf[i] |= (1 << (7-j));
                }
            }
        }
    }

    if (!writemode) {
        for (int i=1; i<11; ++i) {
            uint32_t *read = ((uint32_t *)&meta) + i;
            uint32_t genkey[2];
            genkey[0] = meta.crc;
            genkey[1] = i;
            xtea8(genkey, (const uint32_t *)key);
            *read++ ^= genkey[0];
        }

        uint32_t check = crc32(crckey, (const uint8_t *)&meta, sizeof(meta)-4);

        if (check == meta.crc) {
            int team_index = 0;
            for (team_index=0; team_index<g_num_teams; ++team_index) {
                if (match_team_name(meta.team_name, &g_teams[team_index])) {
                    break;
                }
            }
            if (team_index < g_num_teams) {
                scene->current_ship = team_index;
            } else {
                printf("Unknown team: >%s<\n", meta.team_name);
                return false;
            }

            scene->target_longitude = meta.longitude;
            scene->target_latitude = meta.latitude;
            scene->zoom = meta.zoom;
            scene->target_dx = meta.dx;
            scene->target_dy = meta.dy;
            scene->ortho = (meta.ortho != 0);
            scene->save_slot = meta.save_slot;
            return true;
        } else {
            return false;
        }
    }

    return true;
}


bool
load_png(struct Scene *scene, const char *filename)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (png_image_begin_read_from_file(&image, filename)) {
        image.format = PNG_FORMAT_RGBA;

        png_bytep buffer = malloc(PNG_IMAGE_SIZE(image));

        if (image.width == 256 && image.height == 256) {
            if (png_image_finish_read(&image, NULL, buffer, 0, NULL) != 0) {
                // try re-reading scene metadata
                meta_png_io(scene, (uint32_t *)buffer, 256, 256, false);

                struct Material *mat = SHIP_FROM_SCENE(scene)->materials;
                while (mat) {
                    int index = mat->index;

                    if (index != -1) {
                        undo_save_material_pixels(scene->undo, mat);

                        int sx = (index % 2) * 128;
                        int sy = (index / 2) * 128;
                        for (int y=0; y<128; ++y) {
                            memcpy(mat->pixels + ((127-y) * 128 + 0) * 4, buffer + ((sy + y) * image.width + sx) * 4, 128 * 4);
                        }
                        material_upload(mat);
                    }
                    mat = mat->next;
                }

                return true;
            }
        }

        png_image_free(&image);
        free(buffer);
    }

    return false;
}

bool
load_dat(struct Scene *scene, const char *filename)
{
    size_t shipdat_len;
    char *shipdat = read_file(filename, &shipdat_len);

    if (strstr(filename, "16034453") != NULL) {
        if (!saveskin_decrypt(shipdat, &shipdat_len)) {
            printf("Could not decrypt\n");
            free(shipdat);
            return false;
        }
    }

    if (strstr(filename, ".vex") == filename + strlen(filename) - 4) {
        free(shipdat);
        return false;
    }

    if (shipdat_len != 26912 && shipdat_len != 24800) {
        free(shipdat);
        return false;
    }

    int team_index = 0;
    for (team_index=0; team_index<g_num_teams; ++team_index) {
        if (match_team_name(shipdat, &g_teams[team_index])) {
            break;
        }
    }
    if (strcmp(shipdat, "AG Systems") == 0) {
        // Hack
        team_index = 0;
    }
    if (team_index < g_num_teams) {
        g_scene->current_ship = team_index;
    } else {
        printf("Unknown team: >%s<\n", shipdat);
    }

    struct Material *mat = SHIP_FROM_SCENE(scene)->materials;
    while (mat) {
        int index = mat->index;

        if (index != -1) {
            undo_save_material_pixels(scene->undo, mat);

            uint8_t *new_pixels = load_shipdat(shipdat, shipdat_len, index, &mat->width, &mat->height, &mat->channels, 4, &mat->palette);
            if (new_pixels) {
                free(mat->pixels);
                mat->pixels = new_pixels;
            }
            material_upload(mat);
        }
        mat = mat->next;
    }
    free(shipdat);
    return true;
}

bool
encode_image(unsigned char *buf, struct Material *mat)
{
    int index = mat->index;

    printf("material index: %d\n", index);

    size_t palette_offset = 32 + index * (4 * 16 + 128 * 128 / 2);
    size_t image_offset = palette_offset + 4 * 16;

    printf("palette offset: %zu, image offset: %zu\n", palette_offset, image_offset);

    uint32_t *palette = (uint32_t *)(buf + palette_offset);
    uint32_t palette_size = 0;

    uint8_t *image = buf + image_offset;

    uint32_t *pixels = (uint32_t *)(mat->pixels);

    for (int y=0; y<128; ++y) {
        for (int x=0; x<128; ++x) {
            uint32_t pixel = pixels[y*128+x];
            int pixel_index = -1;
            for (int j=0; j<16; ++j) {
                if (palette[j] == pixel) {
                    pixel_index = j;
                    break;
                }
            }

            if (pixel_index == -1) {
                pixel_index = palette_size++;
                palette[pixel_index] = pixel;
                if (palette_size > 16) {
                    printf("Too big of a palette\n");
                    return false;
                }
            }

            uint8_t pixel_value = pixel_index;
            image[(y*128+x) / 2] |= (x % 2 == 0) ? (pixel_value << 4) : pixel_value;
        }
    }

    return true;
}

static void
scene_reset_view(struct Scene *scene)
{
    scene->target_longitude = -1.f;
    scene->longitude_delta = scene->longitude_delta_target = 0.f;
    scene->target_latitude = 0.4f;
    scene->zoom = 36.f;
    scene->target_dx = -1.3f;
    scene->target_dy = 1.1f;
    scene->ortho = false;
}

struct SaveLayoutContext {
    int w;
    int h;
    struct LayoutItem *item;
};

static void
save_layout(const char *filename, void *user_data)
{
    struct SaveLayoutContext *ctx = user_data;
    layout_to_png(ctx->w, ctx->h, ctx->item, filename);
}


static void
plot_here(struct Scene *scene, int w, int h, int x, int y)
{
    if (!scene->picking.inited ||
            scene->picking.longitude != scene->longitude ||
            scene->picking.latitude != scene->latitude ||
            scene->picking.zoom != scene->zoom ||
            scene->picking.dx != scene->dx ||
            scene->picking.dy != scene->dy ||
            scene->picking.ortho != scene->ortho) {
        // The picking pixel buffer is out of date, need to redraw it
        if (!scene->picking.pixels) {
            scene->picking.pixels = malloc(sizeof(uint32_t) * w * h);
        }

        scene_render(scene, w, h, SDL_GetTicks() / 1000.f, true);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, scene->picking.pixels);

        scene->picking.inited = true;
        scene->picking.longitude = scene->longitude;
        scene->picking.latitude = scene->latitude;
        scene->picking.zoom = scene->zoom;
        scene->picking.dx = scene->dx;
        scene->picking.dy = scene->dy;
        scene->picking.ortho = scene->ortho;
    }

    float radius = get_pen_size_factor();
    int grow = 2 * radius;
    for (int dx=-grow; dx<1+grow; ++dx) {
        for (int dy=-grow; dy<1+grow; ++dy) {
            int picking_x = x + dx;
            int picking_y = y + dy;

            if (!rect_contains(&drawing_on_item->rect, picking_x, picking_y)) {
                // Only draw in the item we started drawing on
                // (avoids paint bleeding into textures with big pen sizes and
                // on the right border of the shipview vs. texture view)
                continue;
            }

            if (picking_x < 0 || picking_x >= w || picking_y < 0 || picking_y >= h) {
                // Out of bounds check
                continue;
            }

            // Location of cursor relative to texture preview
            int texture_x = picking_x - texture_layout->rect.x;
            int texture_y = picking_y - texture_layout->rect.y;

            // Vertical flip because of OpenGL bottom-left origin
            picking_y = h - 1 - picking_y;

            uint32_t pixel = scene->picking.pixels[picking_y*w+picking_x];

            uint32_t r = (pixel & 0xFF);
            uint32_t g = ((pixel >> 8) & 0xFF);
            uint32_t b = ((pixel >> 16) & 0xFF);
            uint32_t a = ((pixel >> 24) & 0xFF);

            uint32_t picking_material_index = r>>5;
            uint32_t picking_u = g>>1;
            uint32_t picking_v = b>>1;

            // Do "picking" based on screen space coordinates texture preview
            // (fixes incompatibilities with certain OpenGL drivers,
            // seen for example with Windows 7 Generic GDI in a Boxes VM)
            if (texture_x >= 0 && texture_x < texture_layout->rect.w &&
                    texture_y >= 0 && texture_y < texture_layout->rect.h) {
                int part_w = 128;
                int part_h = 128;
                picking_material_index = 1 + (texture_y / part_h) * 2 + (texture_x / part_w);
                picking_u = part_h - 1 - texture_y % part_h;
                picking_v = texture_x % part_w;
            }

            if (picking_material_index > 0 && picking_material_index < 5) {
                picking_material_index--;
                struct Material *material = SHIP_FROM_SCENE(scene)->materials;
                while (material != NULL) {
                    if (material->index == picking_material_index) {
                        break;
                    }

                    material = material->next;
                }

                if (material && material->index != -1) {
                    float alpha = sqrtf((((float)dx*(float)dx) + ((float)dy*(float)dy))) / radius;
                    if (alpha <= 1.f) {
                        undo_save_material_pixels(scene->undo, material);
                        material_plot(material, picking_u, picking_v, alpha);
                    }
                }
            }
        }
    }

    struct Material *cur = SHIP_FROM_SCENE(scene)->materials;
    while (cur != NULL) {
        if (cur->pixels_dirty) {
            material_upload(cur);
            cur->pixels_dirty = false;
        }

        cur = cur->next;
    }
}

static void
parse_ships_line(const char *line, void *user_data)
{
    if (line[0] == '#' || line[0] == '\0') {
        return;
    }

    // this is leaked as it's used as string storage for the team
    char *tmp = strdup(line);
    char *end = tmp + strlen(tmp);

    int part = 0;
    char *parts[3];

    char *start = tmp;
    while (start < end && part < 3) {
        char *cur = start;

        while (*cur == ' ') {
            ++cur;
        }

        parts[part++] = cur;

        while (*cur && *cur != ',') {
            ++cur;
        }

        *cur = '\0';

        start = cur + 1;
    }

    if (part != 3) {
        fail("Could not parse ships");
    }

    g_num_teams++;
    g_teams = realloc(g_teams, g_num_teams * sizeof(struct TeamToObject));

    struct TeamToObject *team = g_teams + g_num_teams - 1;

    team->team_name = parts[0];
    team->team_label = parts[1];
    team->slug = parts[2];
    team->loaded_model = NULL;
}

static void
parse_wadlist_line(const char *line, void *user_data)
{
    if (line[0] == '#' || line[0] == '\0') {
        return;
    }

    mount_wad(line);
}

void
scene_render_uv_map(struct Scene *scene, int w, int h)
{
    struct ShipModel *model = SHIP_FROM_SCENE(scene);
    struct Material *mat = model->materials;
    while (mat != NULL) {
        int mat_index = mat->index;
        if (mat->pixels && mat_index != -1) {
            undo_save_material_pixels(scene->undo, mat);

            glViewport(0, 0, mat->width, mat->height);
            glScissor(0, 0, mat->width, mat->height);
            glEnable(GL_SCISSOR_TEST);
            glClearColor(
                    (mat_index == 2) ? 1.f : 0.f,
                    (mat_index == 0) ? 1.f : 0.f,
                    (mat_index == 1) ? 1.f : 0.f,
                    1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            // UV coordinates go from (0,0) - (1,1)
            glOrtho(0.f, 1.f, 0.f, 1.f, 0.f, 1.f);

            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            struct Object *obj = model->objects;
            while (obj != NULL) {
                if (obj->material == mat) {
                    glColor4f(1.f, 1.f, 1.f, 1.f);
                    glBegin(GL_LINES);
                    for (int kk=0; kk<obj->vertexdata_size; kk+=3) {
                        struct Vertex *vtx0 = &obj->vertexdata[kk];
                        struct Vertex *vtx1 = &obj->vertexdata[kk+1];
                        struct Vertex *vtx2 = &obj->vertexdata[kk+2];

                        // This is basically emulating GL_TRIANGLES without using glPolygonMode

                        glVertex2f(vtx0->u, vtx0->v);
                        glVertex2f(vtx1->u, vtx1->v);

                        glVertex2f(vtx1->u, vtx1->v);
                        glVertex2f(vtx2->u, vtx2->v);

                        glVertex2f(vtx2->u, vtx2->v);
                        glVertex2f(vtx0->u, vtx0->v);
                    }
                    glEnd();
                }

                obj = obj->next;
            }

            // We could use glCopyTexImage2D here, but we need to read the pixels anyway
            glReadPixels(0, 0, mat->width, mat->height, GL_RGBA, GL_UNSIGNED_BYTE, mat->pixels);
            material_upload(mat);

            glDisable(GL_SCISSOR_TEST);
            glViewport(0, 0, w, h);
        }

        mat = mat->next;
    }
}

bool
scene_load_skin(struct Scene *scene, const char *filename)
{
    if (load_png(scene, filename)) {
        return true;
    } else if (load_dat(scene, filename)) {
        return true;
    }

    printf("Could not load %s\n", filename);
    return false;
}

void
missing_wad_file_info()
{
    nativeui_show_error("Default skins not available",
            "Put fedata.wad, pack1_ui1.edat, pack2_ui1.edat, pack3_ui1.edat "
            "and pack4_ui1.edat from the game/DLCs into the current folder "
            "and restart to load default skins.");
}

void
export_savegame(struct Scene *scene, int w, int h, const char *out_dir)
{
    unsigned char buf[32+3*16*4+3*128*128/2];
    memset(buf, 0, sizeof(buf));
    strcpy(buf, g_teams[scene->current_ship].team_name);

    bool result = true;

    struct Material *cur = SHIP_FROM_SCENE(scene)->materials;
    while (cur != NULL) {
        if (cur->index != -1) {
            if (!encode_image(buf, cur)) {
                result = false;
                break;
            }
        }

        cur = cur->next;
    }

    struct SaveLayoutContext save_icon0_context = {
        w,
        h,
        icon0_preview_layout,
    };

    if (result) {
        saveskin_save(out_dir, buf, sizeof(buf), scene->save_slot,
                save_layout, &save_icon0_context);
    } else {
        nativeui_show_error("Could not save file ", "Try quantizing the images first.");
    }
}

int main(int argc, char *argv[])
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Cursor *arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    SDL_Cursor *hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    SDL_Cursor *crosshair = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

    SDL_Window *window = SDL_CreateWindow("shipedit " VERSION, SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, window_layout->rect.w, window_layout->rect.h, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);

    SDL_GLContext ctx = SDL_GL_CreateContext(window);

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    nativeui_init(&wmInfo, window);

    if (!mount_wad("editor.wad")) {
        nativeui_show_error("Missing file", "The file editor.wad is needed.");
        return 1;
    }

    parse_file_lines("data/editor/wadlist.txt", parse_wadlist_line, NULL);

    size_t font_len;
    char *font = read_file("data/editor/pulse.fontaine", &font_len);
    struct FontaineFontReader *reader = fontaine_font_reader_new(font, font_len);

    g_font_gui = in_memory_font_new_name(reader, "WipeoutPulseGUI", true);
    g_font_heading = in_memory_font_new_name(reader, "WipeoutPulseHeadingBig", true);

    parse_file_lines("data/editor/list.txt", parse_ships_line, NULL);

    struct Scene *scene = malloc(sizeof(struct Scene));
    g_scene = scene;
    memset(scene, 0, sizeof(*scene));
    scene->undo = calloc(1, sizeof(struct Undo));

    scene->mode = MODE_EDITOR;

    scene->magnifier.size = 16;
    glGenTextures(1, &scene->magnifier.texture);
    glBindTexture(GL_TEXTURE_2D, scene->magnifier.texture);
    scene->magnifier.pixels = calloc(scene->magnifier.size * scene->magnifier.size, sizeof(uint32_t));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scene->magnifier.size, scene->magnifier.size, 0, GL_RGBA, GL_UNSIGNED_BYTE, scene->magnifier.pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    scene_reset_view(scene);

    bool running = true;

    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    bool missing_wad_files = true;
    for (int i=0; i<g_num_teams; ++i) {
        char tmp[128];
        sprintf(tmp, "data/ships/%s/%s.shm", g_teams[i].slug, g_teams[i].slug);
        g_teams[i].loaded_model = parse_shm(tmp);
        instantiate_materials(g_teams[i].loaded_model);

        scene->current_ship = i;

        sprintf(tmp, "data/ships/%s/ship.dat", g_teams[i].slug);
        if (scene_load_skin(scene, tmp)) {
            missing_wad_files = false;
        } else {
            scene_render_uv_map(scene, w, h);
        }
    }

    if (missing_wad_files) {
        missing_wad_file_info();
    }

    scene->current_ship = 0;

    struct FPS fps;
    fps_init(&fps, SDL_GetTicks());

    if (argc != 1) {
        int argi = 1;
        bool want_usage = false;
        bool have_png = false;
        int want_slot = -1;
        const char *export_dir = NULL;
        const char *msg = NULL;
        bool want_version = false;
        while (argi < argc) {
            if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
                want_usage = true;
                break;
            } else if (strcmp(argv[argi], "--version") == 0) {
                want_version = true;
                break;
            } else if (strcmp(argv[argi], "--slot") == 0) {
                ++argi;
                if (argi >= argc) {
                    msg = "Missing argument: SLOT";
                    want_usage = true;
                    break;
                }
                want_slot = atoi(argv[argi]);
            } else if (strcmp(argv[argi], "--export") == 0) {
                ++argi;
                if (argi >= argc) {
                    msg = "Missing argument: OUTDIR";
                    want_usage = true;
                    break;
                }
                export_dir = argv[argi];
            } else if (!have_png) {
                if (!scene_load_skin(scene, argv[argi])) {
                    printf("Could not load skin from %s\n", argv[argi]);
                    exit(1);
                }

                have_png = true;
            }

            ++argi;
        }

        if (want_slot >= 0) {
            scene->save_slot = (want_slot < MAX_SLOTS) ? want_slot : MAX_SLOTS;
            printf("Save slot: %d\n", scene->save_slot);
        }

        if (export_dir) {
            printf("Exporting to: %s\n", export_dir);

            g_batch_mode = true;

            scene_render(scene, w, h, 0.f, false);
            export_savegame(scene, w, h, export_dir);

            running = false;
        }

        if (want_version) {
            printf("\n");
            for (int i=0; i<sizeof(about_lines)/sizeof(about_lines[0]); ++i) {
                printf("%s\n", about_lines[i]);
            }
            printf("\n");
            exit(0);
        }

        if (want_usage) {
            printf("\nUsage: %s [PNGFILE] [--slot SLOT] [--export OUTDIR] [--version]\n\n"
                   " PNGFILE ........... Filename of a ship skin (PNG, DAT or 16034453 file) to load\n"
                   " --slot SLOT ....... Set the savegame slot (XXXX in UCES00465DTEAMSKINXXXX)\n"
                   " --export OUTDIR ... Batch mode: Export a savegame to the output folder\n"
                   " --version ......... Show version, user guide and copyright information\n"
                   "\n", argv[0]);

            if (msg) {
                printf("\n%s\n", msg);
            }
            exit(1);
        }
    }

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_q) {
                    running = false;
                    break;
                }
                if (e.key.keysym.sym == SDLK_m) {
                    scene->magnifier.want = !scene->magnifier.want;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                g_mouse.down_location.x = g_mouse.x = e.button.x;
                g_mouse.down_location.y = g_mouse.y = e.button.y;

                struct Rect overview = {
                    scene->overview_x,
                    scene->overview_y,
                    4 * scene->overview_ww,
                    3 * scene->overview_hh,
                };

                if (scene->mode == MODE_OVERVIEW && rect_contains(&overview, g_mouse.x, g_mouse.y)) {
                    float cx = (float)(g_mouse.x - overview.x) / (float)overview.w;
                    float cy = (float)(g_mouse.y - overview.y) / (float)overview.h;

                    int column = cx * 4;
                    int row = cy * 3;

                    scene->overview_transition = scene->overview_transition_target = 0.f;

                    scene->current_ship = (row * 4 + column) % g_num_teams;
                    scene->picking.inited = false;
                }

                if (scene->mode == MODE_EDITOR) {
                    for (int i=sizeof(layout)/sizeof(layout[0])-1; i>=0; --i) {
                        struct LayoutItem *item = layout + i;
                        if (rect_contains(&item->rect, g_mouse.x, g_mouse.y) && item->name[0] != '\0') {
                            layout_hover = layout_active = item;
                            scene_render(scene, w, h, SDL_GetTicks() / 1000.f, false);
                            SDL_GL_SwapWindow(window);

                            if ((ITEM_ID(item) == ITEM_SHIPVIEW || ITEM_ID(item) == ITEM_TEXTURE)) {
                                if (scene->mode == MODE_EDITOR) {
                                    if (e.button.button == SDL_BUTTON_RIGHT || (e.button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_CTRL) != 0)) {
                                        g_mouse.dragging = true;
                                    } else if (e.button.button == SDL_BUTTON_MIDDLE || (e.button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_ALT) != 0)) {
                                        g_mouse.panning = true;
                                    } else if (e.button.button == SDL_BUTTON_LEFT) {
                                        undo_push(scene->undo, "Drawing");
                                        g_mouse.drawing = true;
                                        drawing_on_item = item;

                                        // clear plot area
                                        struct Material *mat = SHIP_FROM_SCENE(scene)->materials;
                                        while (mat != NULL) {
                                            memset(mat->pixels_drawn, 0, sizeof(uint8_t) * mat->width * mat->height);
                                            mat = mat->next;
                                        }

                                        plot_here(scene, w, h, e.button.x, e.button.y);
                                    }
                                }
                            }

                            break;
                        }
                    }
                }

                if (scene->mode == MODE_ABOUT) {
                    scene->about_transition_target = 0.f;
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP) {
                for (int i=sizeof(layout)/sizeof(layout[0])-1; i>=0; --i) {
                    struct LayoutItem *item = layout + i;
                    if (rect_contains(&item->rect, g_mouse.x, g_mouse.y) && item->name[0] != '\0') {
                        if (layout_active == item) {
                            layout_hover = layout_active = NULL;
                            scene_render(scene, w, h, SDL_GetTicks() / 1000.f, false);
                            SDL_GL_SwapWindow(window);

                            //printf("Clicked on %s\n", item->name);

                            if (ITEM_ID(item) == ITEM_TOGGLE_PROJECTION) {
                                scene->ortho = !scene->ortho;
                            }
                            if ((item->item & FLAG_SLIDER) != 0) {
                                slider(item, false);
                            }
                            if (ITEM_ID(item) == ITEM_DEFAULT_SKIN || ITEM_ID(item) == ITEM_ALTERNATIVE_SKIN || ITEM_ID(item) == ITEM_ELIMINATOR_SKIN) {
                                const char *variant = "";

                                if (ITEM_ID(item) == ITEM_ALTERNATIVE_SKIN) {
                                    variant = "_alt";
                                } else if (ITEM_ID(item) == ITEM_ELIMINATOR_SKIN) {
                                    variant = "_eliminator";
                                }

                                char tmp[128];
                                sprintf(tmp, "data/ships/%s/ship%s.dat", g_teams[scene->current_ship].slug, variant);

                                if (!scene_load_skin(scene, tmp)) {
                                    missing_wad_file_info();
                                }

                                // start new undo stack
                                undo_clear(scene->undo);
                            }
                            if (ITEM_ID(item) == ITEM_OPEN_PNG) {
                                char *filename = nativeui_open_file();
                                if (filename) {
                                    undo_push(scene->undo, "Load image file");

                                    if (!scene_load_skin(scene, filename)) {
                                        nativeui_show_error("Invalid file", "File must be a 256x256 PNG or a DAT file.");
                                    }

                                    free(filename);
                                }
                            }
                            if (ITEM_ID(item) == ITEM_BUILD_SAVEFILE) {
                                char *out_dir = nativeui_select_folder();
                                if (out_dir != NULL) {
                                    export_savegame(scene, w, h, out_dir);
                                    free(out_dir);
                                }
                            }
                            if (ITEM_ID(item) == ITEM_CHOOSE_COLOR) {
                                nativeui_choose_color(g_current_color, set_uint32_callback, &g_current_color);
                            }
                            if (ITEM_ID(item) == ITEM_SAVE_PNG) {
                                char *filename = nativeui_save_png();
                                if (filename) {
                                    uint32_t *buffer = malloc(4 * 256 * 256);

                                    glMatrixMode(GL_PROJECTION);
                                    glLoadIdentity();
                                    glOrtho(0.f, w, h, 0.f, -1.f, +1.f);

                                    glMatrixMode(GL_MODELVIEW);
                                    glLoadIdentity();

                                    glClearColor(1.f, 1.f, 1.f, 1.f);
                                    glClear(GL_COLOR_BUFFER_BIT);
                                    glColor4f(0.f, 0.f, 0.f, 1.f);

                                    draw_with_font_xy(g_font_gui, 128+10, 128+6, g_teams[scene->current_ship].team_name);

                                    glColor4f(0.3f, 0.3f, 0.3f, 1.f);
                                    draw_with_font_xy(g_font_gui, 128+7, 256-6-12, "thp.io/2021/shipedit");

                                    {
                                        glViewport(128 + 10, 128 + 64 - 15, 108, 64 - 10);
                                        glScissor(128 + 10, 128 + 64 - 15, 108, 64 - 10);
                                        glEnable(GL_SCISSOR_TEST);
                                        glClearColor(0.f, 0.f, 0.f, 1.f);
                                        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                                        render_shipview(scene, SHIP_FROM_SCENE(scene), 108, 64-10, false, false);
                                        glDisable(GL_SCISSOR_TEST);
                                        glViewport(0, 0, w, h);
                                    }

                                    glReadPixels(0, h-256, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
                                    char *scanline = malloc(4 * 256);
                                    for (int i=0; i<128; ++i) {
                                        memcpy(scanline, buffer + 256 * i, 4 * 256);
                                        memcpy(buffer + 256 * i, buffer + 256 * (255-i), 4 * 256);
                                        memcpy(buffer + 256 * (255-i), scanline, 4 * 256);
                                    }
                                    free(scanline);

                                    // encode scene metadata
                                    meta_png_io(scene, buffer, 256, 256, true);
                                    if (!meta_png_io(scene, buffer, 256, 256, false)) {
                                        nativeui_show_error("Metadata validation failed", "Viewport settings might not be restored.");
                                    }

                                    struct Material *mat = SHIP_FROM_SCENE(scene)->materials;
                                    while (mat != NULL) {
                                        int index = mat->index;
                                        if (index == -1) {
                                            mat = mat->next;
                                            continue;
                                        }

                                        printf("material: %s index=%d\n", mat->name, index);
                                        uint32_t *mat_pixels = (uint32_t *)mat->pixels;

                                        int xoff = (index % 2 == 0) ? 0 : 128;
                                        int yoff = (index / 2 == 0) ? 0 : 128;

                                        for (int y=0; y<128; ++y) {
                                            for (int x=0; x<128; ++x) {
                                                buffer[((((127-y)+yoff))*256)+(x+xoff)] = mat_pixels[(y*128)+x];
                                            }
                                        }

                                        mat = mat->next;
                                    }
                                    png_write_rgba(filename, 256, 256, buffer, false);
                                    free(buffer);
                                    free(filename);
                                }
                            }
                            if (ITEM_ID(item) == ITEM_NEXT_SHIP) {
                                scene->overview_transition_target = 1.f;
                                g_mouse.tooltip = NULL;
                            }
                            if (ITEM_ID(item) == ITEM_ABOUT) {
                                scene->about_transition = 0.f;
                                scene->about_transition_target = 1.f;
                                scene->mode = MODE_ABOUT;
                            }
                            if (ITEM_ID(item) == ITEM_UNDO) {
                                undo_undo(scene->undo);
                            }
                            if (ITEM_ID(item) == ITEM_RENDER_UV_MAP) {
                                undo_push(scene->undo, "Render UV Map");
                                scene_render_uv_map(scene, w, h);
                            }
                            if (ITEM_ID(item) == ITEM_RESET_VIEW) {
                                scene_reset_view(scene);
                            }
                            if (ITEM_ID(item) == ITEM_SAVE_SLOT) {
                                if (e.button.button == SDL_BUTTON_LEFT) {
                                    scene->save_slot = (scene->save_slot + 1) % MAX_SLOTS;
                                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                                    scene->save_slot = (scene->save_slot + MAX_SLOTS - 1) % MAX_SLOTS;
                                }
                            }
                            if (ITEM_ID(item) == ITEM_QUANTIZE_COLORS) {
                                undo_push(scene->undo, "Quantize");
                                struct Material *mat = SHIP_FROM_SCENE(scene)->materials;

                                int done = 0;
                                int count = 3;
                                while (mat != NULL) {
                                    if (mat->index == -1) {
                                        mat = mat->next;
                                        continue;
                                    }

                                    ++done;

                                    g_mouse.tooltip = NULL;
                                    scene_render(scene, w, h, SDL_GetTicks() / 1000.f, false);

                                    glEnable(GL_BLEND);
                                    glColor4f(0.f, 0.f, 0.f, 0.9f);
                                    draw_rect(0, 0, w, h);

                                    glColor4f(1.f, 1.f, 1.f, 1.f);
                                    char msg[32];
                                    sprintf(msg, "Please wait (%d/%d)...", done, count);
                                    draw_with_font_xy(g_font_heading, 13, 10, msg);
                                    draw_with_font_xy(g_font_gui, 13, 30, "Sometimes quantization fails (known bug), just hit 'UNDO' and then retry");
                                    SDL_GL_SwapWindow(window);

                                    undo_save_material_pixels(scene->undo, mat);

                                    printf("Quantizing: image %d\n", mat->index);

                                    char *buf = malloc(3 * 128 * 128);

                                    for (int y=0; y<128; ++y) {
                                        for (int x=0; x<128; ++x) {
                                            buf[3 * (y*128+x) + 0] = mat->pixels[4 * (y*128+x) + 0];
                                            buf[3 * (y*128+x) + 1] = mat->pixels[4 * (y*128+x) + 1];
                                            buf[3 * (y*128+x) + 2] = mat->pixels[4 * (y*128+x) + 2];
                                        }
                                    }

                                    int res = spatial_color_quant_inplace(128, 128, buf, 16);
                                    if (res != 0) {
                                        fail("could not quantize image");
                                    }

                                    for (int y=0; y<128; ++y) {
                                        for (int x=0; x<128; ++x) {
                                            mat->pixels[4 * (y*128+x) + 0] = buf[3 * (y*128+x) + 0];
                                            mat->pixels[4 * (y*128+x) + 1] = buf[3 * (y*128+x) + 1];
                                            mat->pixels[4 * (y*128+x) + 2] = buf[3 * (y*128+x) + 2];
                                        }
                                    }

                                    free(buf);

                                    material_upload(mat);

                                    mat = mat->next;
                                }
                            }
                            if (ITEM_ID(item) == ITEM_AUTO_MOVE) {
                                scene->longitude_delta_target = (scene->longitude_delta_target == 0.f) ? 0.03f : 0.f;
                            }
                            if (ITEM_ID(item) == ITEM_ZOOM) {
                                float now = scene->zoom - ZOOM_MIN_FOV;
                                scene->zoom = ZOOM_MIN_FOV + fmodf(now + (ZOOM_MAX_FOV - ZOOM_MIN_FOV) / 4.f, (ZOOM_MAX_FOV - ZOOM_MIN_FOV));
                            }
                            break;
                        }
                    }
                }
                layout_hover = layout_active = NULL;
            }
            if (e.type == SDL_MOUSEMOTION) {
                scene->magnifier.pos.x = e.motion.x - scene->magnifier.size / 2;
                scene->magnifier.pos.y = e.motion.y - scene->magnifier.size / 2;

                magnifier_layout->rect.x = (scene->magnifier.pos.x < w/2) ? (scene->magnifier.pos.x + 90) : (scene->magnifier.pos.x - 90 - magnifier_layout->rect.w);
                float y_rel = (float)(e.motion.y) / (float)h;
                magnifier_layout->rect.y = e.motion.y - magnifier_layout->rect.h * y_rel;

                struct LayoutItem *hovering = NULL;
                if (scene->mode == MODE_EDITOR) {
                    for (int i=sizeof(layout)/sizeof(layout[0])-1; i>=0; --i) {
                        struct LayoutItem *item = layout + i;
                        if (rect_contains(&item->rect, g_mouse.x, g_mouse.y)) {
                            if (item == layout_active) {
                                layout_hover = item;
                            } else {
                                layout_hover = NULL;
                            }
                            hovering = item;
                            break;
                        }
                    }
                }

                layout_mouseover = hovering;

                if (hovering) {
                    if (g_mouse.tooltip != hovering->tooltip) {
                        g_mouse.tooltip = hovering->tooltip;
                        g_mouse.last_movement = SDL_GetTicks();
                    }
                } else {
                    g_mouse.tooltip = NULL;
                }

                if (layout_active != NULL && (layout_active->item & FLAG_SLIDER) != 0) {
                    slider(layout_active, false);
                }

                scene->magnifier.visible = hovering && (ITEM_ID(hovering) == ITEM_SHIPVIEW || ITEM_ID(hovering) == ITEM_TEXTURE);

                if (g_mouse.dragging) {
                    SDL_SetCursor(hand);
                } else {
                    if (hovering) {
                        if ((ITEM_ID(hovering) == ITEM_SHIPVIEW || ITEM_ID(hovering) == ITEM_TEXTURE) && scene->mode == MODE_EDITOR) {
                            SDL_SetCursor(crosshair);
                        } else {
                            SDL_SetCursor(arrow);
                        }
                    } else {
                        SDL_SetCursor(arrow);
                    }
                }

                if (g_mouse.dragging) {
                    float dx = (e.motion.x - g_mouse.x);
                    scene->target_longitude -= dx * 0.01f;
                    float dy = (e.motion.y - g_mouse.y);
                    scene->latitude += dy * 0.01f;
                    scene->latitude = fminf(1.2f, scene->latitude);
                    scene->target_latitude = fminf(1.2f, fmaxf(0.f, scene->latitude));
                } else if (g_mouse.panning) {
                    float f = 0.1f;
                    scene->target_dx += f * (e.motion.x - g_mouse.x);
                    scene->target_dy -= f * (e.motion.y - g_mouse.y);
                } else if (g_mouse.drawing) {
                    plot_here(scene, w, h, e.motion.x, e.motion.y);
                }

                g_mouse.x = e.motion.x;
                g_mouse.y = e.motion.y;
            }
            if (e.type == SDL_MOUSEBUTTONUP) {
                g_mouse.drawing = false;
                if (g_mouse.dragging) {
                    g_mouse.dragging = false;
                    scene->longitude_delta = scene->longitude_delta_target = 0;
                }
                g_mouse.panning = false;
            }
            if (e.type == SDL_MOUSEWHEEL) {
                scene->zoom = fminf(ZOOM_MAX_FOV, fmaxf(ZOOM_MIN_FOV, scene->zoom + 5.f * e.wheel.y));
            }
        }

        scene_render(scene, w, h, SDL_GetTicks() / 1000.f, false);

        if (!g_mouse.dragging) {
            float alpha = 0.9f;
            scene->longitude_delta = alpha * scene->longitude_delta + (1.f - alpha) * scene->longitude_delta_target;
            scene->target_longitude += scene->longitude_delta;
        }

        {
            float alpha = 0.9f;
            scene->overview_transition = alpha * scene->overview_transition + (1.f - alpha) * scene->overview_transition_target;
            scene->about_transition = alpha * scene->about_transition + (1.f - alpha) * scene->about_transition_target;
        }

        scene->time += 0.1f;

        SDL_GL_SwapWindow(window);
        SDL_Delay(fps_frame(&fps, SDL_GetTicks()));
    }

    free(scene->picking.pixels);

    SDL_GL_DeleteContext(ctx);

    SDL_DestroyWindow(window);
    SDL_Quit();

    in_memory_font_free(g_font_gui);
    in_memory_font_free(g_font_heading);
    fontaine_font_reader_destroy(reader);

    return 0;
}
