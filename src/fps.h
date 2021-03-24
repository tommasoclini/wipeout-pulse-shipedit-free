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

struct FPS {
    uint32_t begin;
    int frames;
    float fps;

    uint32_t last;
    float target;
};

static inline void
fps_init(struct FPS *fps, uint32_t now)
{
    fps->begin = now;
    fps->frames = 0;
    fps->fps = 0.f;

    fps->last = now;
    fps->target = 60.f;
}

static inline int32_t
fps_frame(struct FPS *fps, uint32_t now)
{
    int32_t wait = 0;

    fps->frames++;

    uint32_t diff = now - fps->begin;
    if (diff > 1000) {
        fps->fps = (float)fps->frames * 1000.f / diff;
        fps->begin = now;
        fps->frames = 0;
    }

    if (fps->target != 0.f) {
        uint32_t duration = (now - fps->last);
        wait = (1000.f / fps->target) - duration;
        if (wait < 0) {
            wait = 0;
        }
    }

    fps->last = now + wait;

    return wait;
}
