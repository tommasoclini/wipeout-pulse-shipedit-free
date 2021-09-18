
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

#include <SDL_syswm.h>

#include <gtk/gtk.h>
#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#endif /* defined(GDK_WINDOWING_X11) */

#include "nfd.h"

void
nativeui_init(const SDL_SysWMinfo *info, SDL_Window *window)
{
}

void
nativeui_choose_color(uint32_t rgba, void (*set_color_func)(uint32_t, void *), void *user_data)
{
    union {
        uint32_t rgba;
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };
    } c = { .rgba = rgba };

    GtkWidget *dialog;

    if (!gtk_init_check(NULL, NULL))
    {
        return rgba;
    }

    dialog = gtk_color_chooser_dialog_new("Select color", NULL);

    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dialog), FALSE);

    GdkRGBA color = {
        .red = c.r / 255.,
        .green = c.g / 255.,
        .blue = c.b / 255.,
        .alpha = c.a / 255.,
    };

    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &color);

#if defined(GDK_WINDOWING_X11)
    /* Work around focus issue on X11 (https://github.com/mlabbe/nativefiledialog/issues/79) */
    gtk_widget_show_all(dialog);
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        GdkWindow *window = gtk_widget_get_window(dialog);
        gdk_window_set_events(window, gdk_window_get_events(window) | GDK_PROPERTY_CHANGE_MASK);
        gtk_window_present_with_time(GTK_WINDOW(dialog), gdk_x11_get_server_time(window));
    }
#endif /* defined(GDK_WINDOWING_X11) */

    if ( gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK )
    {
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &color);

        c.r = (uint8_t)(color.red * 0xFF);
        c.g = (uint8_t)(color.green * 0xFF);
        c.b = (uint8_t)(color.blue * 0xFF);
        c.a = (uint8_t)(color.alpha * 0xFF);

        rgba = c.rgba;
    }

    gtk_widget_destroy(dialog);

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    set_color_func(rgba, user_data);
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
    GtkWidget *dialog;

    if (!gtk_init_check(NULL, NULL)) {
        return;
    }

    dialog = gtk_message_dialog_new_with_markup(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "<big>%s</big>\n\n%s", title, message);

    gtk_dialog_run(GTK_DIALOG(dialog));

    gtk_widget_destroy(dialog);

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

void
nativeui_deinit()
{
}
