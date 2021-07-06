
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

static struct {
    HWND hwnd;
    COLORREF custom_colors[16];
} g;

void
nativeui_init(const SDL_SysWMinfo *info)
{
    g.hwnd = info->info.win.window;

    memset(g.custom_colors, 0, sizeof(g.custom_colors));

    OleInitialize(NULL);
}

uint32_t
nativeui_choose_color(uint32_t rgba)
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

    CHOOSECOLOR cc = {
        sizeof(CHOOSECOLOR),
        g.hwnd, // hwndOwner
        NULL, // hInstance
        RGB(c.r, c.g, c.b), // rgbResult
        g.custom_colors, // lpCustColors
        CC_ANYCOLOR | CC_FULLOPEN | CC_RGBINIT | CC_SOLIDCOLOR, // flags
        0, // lCustData
        NULL, // lpfnHook
        NULL, // lpTemplateName
    };

    if (ChooseColor(&cc)) {
        rgba = cc.rgbResult;
    }

    return rgba;
}

static int __stdcall
SelectFolderProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    switch (uMsg) {
        case BFFM_INITIALIZED:
            SendMessage(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)lpData);
            break;
        default:
            return 0;
    }

    return 0;
}

char *
nativeui_select_folder()
{
    static char *last_path = NULL;

    char displayName[MAX_PATH];
    memset(displayName, 0, sizeof(displayName));

    BROWSEINFO bi = {
        g.hwnd, // hwndOwner
        NULL, // pidlRoot
        displayName, // pszDisplayName
        "Choose output folder", // lpszTitle
        BIF_USENEWUI, // flags
        SelectFolderProc, // lpfn
        (LPARAM)last_path, // lParam
        0, // iImage
    };

    PIDLIST_ABSOLUTE result = SHBrowseForFolder(&bi);
    if (result != NULL) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(result, path)) {
            free(last_path);
            last_path = strdup(path);
            return strdup(path);
        }
    }

    return NULL;
}

char *
nativeui_save_png()
{
    char *result = NULL;

    char cwd[MAX_PATH];
    GetCurrentDirectory(sizeof(cwd), cwd);

    char filename_buf[MAX_PATH];
    memset(filename_buf, 0, sizeof(filename_buf));

    OPENFILENAME ofn = {
        sizeof(OPENFILENAME),
        g.hwnd, // hwndOwner
        NULL, // hInstance
        "PNG files\0*.png\0\0", // lpstrFilter
        NULL, // lpstrCustomFilter
        0, // nMaxCustFilter
        0, // nFilterIndex
        filename_buf, // lpstrFile
        sizeof(filename_buf), // nMaxFile
        NULL, // lpstrFileTitle
        0, // nMaxFileTitle
        NULL, // lpstrInitialDir
        "Save skin as PNG file", // lpstrTitle
        OFN_LONGNAMES | OFN_OVERWRITEPROMPT, // flags
        0, // nFileOffset
        0, // nFileExtension
        "png", // lpstrDefExt
        0, // lCustData
        NULL, // lpfnHook
        NULL, // lpTemplateName
    };

    if (GetSaveFileName(&ofn)) {
        result = strdup(filename_buf);
    }

    SetCurrentDirectory(cwd);

    return result;
}

char *
nativeui_open_file()
{
    char *result = NULL;

    char cwd[MAX_PATH];
    GetCurrentDirectory(sizeof(cwd), cwd);

    char filename_buf[MAX_PATH];
    memset(filename_buf, 0, sizeof(filename_buf));
    OPENFILENAME ofn = {
        sizeof(OPENFILENAME),
        g.hwnd, // hwndOwner
        NULL, // hInstance
        "PNG, DAT or 16034453 Ship Skin\00016034453;*.png;*.dat\0\0", // lpstrFilter
        NULL, // lpstrCustomFilter
        0, // nMaxCustFilter
        0, // nFilterIndex
        filename_buf, // lpstrFile
        sizeof(filename_buf), // nMaxFile
        NULL, // lpstrFileTitle
        0, // nMaxFileTitle
        NULL, // lpstrInitialDir
        "Select a file to open", // lpstrTitle
        OFN_FILEMUSTEXIST | OFN_LONGNAMES, // flags
        0, // nFileOffset
        0, // nFileExtension
        NULL, // lpstrDefExt
        0, // lCustData
        NULL, // lpfnHook
        NULL, // lpTemplateName
    };

    if (GetOpenFileName(&ofn)) {
        result = strdup(filename_buf);
    }

    SetCurrentDirectory(cwd);

    return result;
}

void
nativeui_show_error(const char *title, const char *message)
{
    MessageBox(g.hwnd, message, title, MB_OK | MB_ICONEXCLAMATION);
}

void
nativeui_deinit()
{
}
