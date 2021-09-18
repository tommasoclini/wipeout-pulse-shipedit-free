
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

#include "nativeui.h"

#include <SDL.h>
#include <SDL_syswm.h>

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>

#include "nfd.h"

struct ChooseColorContext {
};

static struct {
    SDL_Window *window;

    union {
        uint32_t rgba;
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };
    } color;

    void (*set_color_func)(uint32_t, void *);
    void *set_color_func_user_data;
} g;

void
nativeui_init(const SDL_SysWMinfo *info, SDL_Window *window)
{
    g.window = window;
}

@interface ColorRetriever : NSObject
- (void)show;
- (void)onColorSelected:(NSColorPanel *)colorPanel;
@end

@implementation ColorRetriever
- (void)show
{
    NSColor *color = [NSColor colorWithRed:(float)g.color.r/255.f
                                     green:(float)g.color.g/255.f
                                      blue:(float)g.color.b/255.f
                                     alpha:(float)g.color.a/255.f];

    NSColorPanel *colorPanel = [NSColorPanel sharedColorPanel];

    [colorPanel setColor:color];
    [colorPanel setTitle:[NSString stringWithUTF8String: "Select color"]];

    [colorPanel setTarget:self];
    [colorPanel setAction:@selector(onColorSelected:)];

    [colorPanel setShowsAlpha:NO];
    [colorPanel setIsVisible:YES];
}

- (void)onColorSelected:(NSColorPanel *)colorPanel
{
    NSColor *color = [colorPanel.color colorUsingColorSpace: [NSColorSpace deviceRGBColorSpace]];

    g.color.r = (uint8_t)([color redComponent] * 0xFF);
    g.color.g = (uint8_t)([color greenComponent] * 0xFF);
    g.color.b = (uint8_t)([color blueComponent] * 0xFF);
    g.color.a = (uint8_t)([color alphaComponent] * 0xFF);

    g.set_color_func(g.color.rgba, g.set_color_func_user_data);
}
@end

void
nativeui_choose_color(uint32_t rgba, void (*set_color_func)(uint32_t, void *), void *user_data)
{
    static ColorRetriever *retriever = NULL;

    if (!retriever) {
        // Allocated once, never free'd
        retriever = [[ColorRetriever alloc] init];
    }

    g.color.rgba = rgba;
    g.set_color_func = set_color_func;
    g.set_color_func_user_data = user_data;

    [retriever show];
}

char *
nativeui_select_folder()
{
    static nfdchar_t *last_path = NULL;

    nfdchar_t *out_dir = NULL;
    if (NFD_PickFolder(last_path, &out_dir) == NFD_OKAY) {
        free(last_path);
        last_path = strdup(out_dir);
        return out_dir;
    }

    return NULL;
}

char *
nativeui_save_png()
{
    nfdchar_t *filename = NULL;
    if (NFD_SaveDialog("png", NULL, &filename) == NFD_OKAY) {
        return filename;
    }

    return NULL;
}

char *
nativeui_open_file()
{
    nfdchar_t *filename = NULL;
    if (NFD_OpenDialog(NULL, NULL, &filename) == NFD_OKAY) {
        return filename;
    }

    return NULL;
}

void
nativeui_show_error(const char *title, const char *message)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, g.window);
}

void
nativeui_deinit()
{
}
