/* vim:set cin ft=c sw=4 sts=4 ts=8 et ai cino=Ls\:0t0(0 : -*- mode:c;fill-column:80;tab-width:8;c-basic-offset:4;indent-tabs-mode:nil;c-file-style:"k&r" -*-*/
/* Last modified by Alex Smith, 2013-09-29 */
/* Copyright (c) 2013 Alex Smith. */
/* The 'uncursed' rendering library may be distributed under either of the
 * following licenses:
 *  - the NetHack General Public License
 *  - the GNU General Public License v2 or later
 * If you obtained uncursed as part of NetHack 4, you can find these licenses in
 * the files libnethack/dat/license and libnethack/dat/gpl respectively.
 */
/* This is a graphical backend for the uncursed rendering library, that uses SDL
   to do its rendering. */

/* Detect OS. */

#define SDL_MAIN_HANDLED /* don't use SDL's main, use the calling process's */
#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>

#include "uncursed.h"
#include "uncursed_hooks.h"
#include "uncursed_sdl.h"

static int fontwidth = 8;
static int fontheight = 14;
static int winwidth = 120; /* width of the window, in units of fontwidth */
static int winheight = 30; /* height of the window, in units of fontheight */
static int resize_queued = 0;
static int suppress_resize = 0;

static int hangup_mode = 0;

/* force the minimum size as 80x24; many programs don't function properly with
   less than that */
#define MINCHARWIDTH 80
#define MINCHARHEIGHT 24

static SDL_Window *win = NULL;
static SDL_Renderer *render = NULL;
/* static SDL_Texture *font = NULL; */

void sdl_hook_beep(void) {
    /* TODO */
}

void sdl_hook_setcursorsize(int size) {
    /* TODO */
}
void sdl_hook_positioncursor(int y, int x) {
    /* TODO */
}

/* Called whenever the window or font size changes. */
static void update_window_sizes(void) {
    /* We set the window's minimum size to 80x24 times the font size; increase
       the window to the minimum size if necessary; and decrease the window to
       an integer multiple of the font size if possible. */
    int w, h, worig, horig;
    SDL_GetWindowSize(win, &w, &h);
    worig = w; horig = h;
    w /= fontwidth; h /= fontheight;
    if (w < MINCHARWIDTH) w = MINCHARWIDTH;
    if (h < MINCHARHEIGHT) h = MINCHARHEIGHT;
    if (w != worig || h != horig) {
        SDL_SetWindowSize(win, w * fontwidth, h * fontheight);
        SDL_RenderSetLogicalSize(render, w * fontwidth, h * fontheight);
    }
    SDL_SetWindowMinimumSize(
        win, MINCHARWIDTH * fontwidth, MINCHARHEIGHT * fontheight);
    if (w != winwidth || h != winheight) resize_queued = 1;
    winwidth = w; winheight = h;
}

static void exit_handler(void) {
    if (win) {
        SDL_DestroyWindow(win);
        SDL_Quit();
    }
    win = NULL;
}
void sdl_hook_init(int *h, int *w) {
    if (!win) {
        if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
            fprintf(stderr, "Error initializing SDL: %s\n", SDL_GetError());
            exit(EXIT_FAILURE);
        }
        win = SDL_CreateWindow(
            "Uncursed",   /* title */
            SDL_WINDOWPOS_UNDEFINED, /* initial x */
            SDL_WINDOWPOS_UNDEFINED, /* initial y */
            fontwidth * winwidth, fontheight * winheight, /* size */
            SDL_WINDOW_RESIZABLE);
        if (!win) {
            fprintf(stderr, "Error creating an SDL window: %s\n",
                    SDL_GetError());
            exit(EXIT_FAILURE);
        }
        atexit(exit_handler);
        update_window_sizes();
        resize_queued = 0;
        *w = winwidth; *h = winheight;
        render = SDL_CreateRenderer(
            win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!render) {
            fprintf(stderr, "Error creating an SDL renderer: %s\n",
                    SDL_GetError());
            exit(EXIT_FAILURE);
        }
    }
}
void sdl_hook_exit(void) {
    /* Actually tearing down the window (or worse, quitting SDL) would be
       overkill, given that this is used to allow raw writing to the console,
       and it's possible to do that behind the SDL window anyway. So do nothing;
       the atexit wil shut it down at actual system exit if the hook is called
       because the program is exiting, and otherwise we're going to have init
       called in the near future. (Perhaps we should hide the window, in case
       the code takes console input while the window's hidden, but even that
       would look weird.) */
}

void sdl_hook_rawsignals(int raw) {
    /* meaningless to this plugin */
}
    
void sdl_hook_delay(int ms) {
    /* We want to discard keys for the given length of time. (If the window is
       resized during the delay, we keep quiet about the resize until the next
       key request, because otherwise the application wouldn't learn about it
       and might try to draw out of bounds. On a hangup, we end the delay
       early.)

       TODO: handle SDL_GetTicks overflow */
    long tick_target = SDL_GetTicks() + ms;
    suppress_resize = 1;
    while (SDL_GetTicks() < tick_target) {
        int k = sdl_hook_getkeyorcodepoint(tick_target - SDL_GetTicks());
        if (k == KEY_HANGUP) break;
    }
    suppress_resize = 0;
}

int sdl_hook_getkeyorcodepoint(int timeout_ms) {
    long tick_target = SDL_GetTicks() + timeout_ms;
    int kc;

    if (hangup_mode) return KEY_HANGUP + KEY_BIAS;

    do {
        SDL_Event e;
        if (!suppress_resize && resize_queued) {
            update_window_sizes();
            resize_queued = 0;
            uncursed_rhook_setsize(winheight, winwidth);
            return KEY_RESIZE + KEY_BIAS;            
        }
        if ((!timeout_ms ? SDL_WaitEvent(&e) :
             SDL_WaitEventTimeout(&e, tick_target - SDL_GetTicks())) == 0) {
            /* The SDL documentation doesn't say what's returned in the case of
               a timeout. However, because there's no other way to communicate
               the fact, it returns its error code, of 0. */
            return KEY_SILENCE + KEY_BIAS;
        }
        switch (e.type) {
        case SDL_WINDOWEVENT:
            /* The events we're interested in here are closing the window,
               and resizing the window. We also need to redraw, if the
               window manager requests that. */
            if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
                e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                update_window_sizes();
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED)
                sdl_hook_fullredraw();
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
                hangup_mode = 1;
                return KEY_HANGUP + KEY_BIAS;
            }
            break;
        case SDL_KEYDOWN:
            kc = 0;
#define K(x,y) case SDLK_##x: kc = KEY_BIAS + KEY_##y; break
#define KK(x) K(x,x)
            switch (e.key.keysym.sym) {

                /* nonprintables in SDL that correspond to control codes */

            case SDLK_RETURN: kc = '\x0d'; break;
            case SDLK_TAB: kc = '\x09'; break;

                /* nonprintables that exist in both SDL and uncursed */

                KK(F1); KK(F2); KK(F3); KK(F4); KK(F5); KK(F6); KK(F7);
                KK(F8); KK(F9); KK(F10); KK(F11); KK(F12); KK(F13); KK(F14);
                KK(F15); KK(F16); KK(F17); KK(F18); KK(F19); KK(F20);
                    
                KK(ESCAPE); KK(BACKSPACE);
                K(PRINTSCREEN,PRINT); K(PAUSE,BREAK);
                KK(HOME); KK(END); K(INSERT,IC); K(DELETE,DC); 
                K(PAGEUP,PPAGE); K(PAGEDOWN,NPAGE);
                KK(RIGHT); KK(LEFT); KK(UP); KK(DOWN);

                K(KP_DIVIDE,NUMDIVIDE); K(KP_MULTIPLY,NUMTIMES);
                K(KP_MINUS,NUMMINUS); K(KP_PLUS,NUMPLUS); K(KP_ENTER,ENTER);
                K(KP_1,C1); K(KP_2,C2); K(KP_3,C3); K(KP_4,B1); K(KP_5,B2);
                K(KP_6,B3); K(KP_7,A1); K(KP_8,A2); K(KP_9,A3); K(KP_0,D1);
                K(KP_PERIOD,D3);

                /* we intentionally ignore modifier keys */
            case SDLK_CAPSLOCK: case SDLK_SCROLLLOCK:
            case SDLK_NUMLOCKCLEAR:
            case SDLK_LCTRL: case SDLK_LSHIFT: case SDLK_LALT:
            case SDLK_RCTRL: case SDLK_RSHIFT: case SDLK_RALT:
            case SDLK_MODE: case SDLK_LGUI: case SDLK_RGUI:
                break;
                    
            default:
                /* Other keys are either printables, or else keys that
                   uncursed doesn't know about. If they're printables, we
                   just store them as is in kc. Otherwise, we synthesize a
                   number for them via masking off SLK_SCANCODE_MASK and
                   adding KEY_LAST_FUNCTION. If that goes to 512 or higher,
                   we give up. */
                if (e.key.keysym.sym >= ' ' && e.key.keysym.sym <= '~')
                    kc = e.key.keysym.sym;
                else if ((e.key.keysym.sym & ~SDLK_SCANCODE_MASK) +
                         KEY_LAST_FUNCTION < 512)
                    kc = (e.key.keysym.sym & ~SDLK_SCANCODE_MASK) +
                        KEY_LAST_FUNCTION + KEY_BIAS;
                break;
            }
#undef KK
#undef K
            if (kc) {
                if (e.key.keysym.mod & KMOD_ALT) kc |= KEY_ALT;
                if (kc >= KEY_BIAS) {
                    if (e.key.keysym.mod & KMOD_CTRL) kc |= KEY_CTRL;
                    if (e.key.keysym.mod & KMOD_SHIFT) kc |= KEY_SHIFT;
                } else {
                    if (e.key.keysym.mod & KMOD_CTRL) kc &= ~64;
                }
            }
            if (kc) return kc;
            break;
        case SDL_QUIT:
            hangup_mode = 1;
            return KEY_HANGUP + KEY_BIAS;
        }
    } while (timeout_ms == 0 || SDL_GetTicks() < tick_target);
    return KEY_SILENCE + KEY_BIAS;
}

/* copied from tty.c; perhaps this should go in a header somewhere */
Uint8 palette[][3] = {
    {0x00,0x00,0x00}, {0xaf,0x00,0x00}, {0x00,0x87,0x00}, {0xaf,0x5f,0x00},
    {0x00,0x00,0xaf}, {0x87,0x00,0x87}, {0x00,0xaf,0x87}, {0xaf,0xaf,0xaf},
    {0x5f,0x5f,0x5f}, {0xff,0x5f,0x00}, {0x00,0xff,0x00}, {0xff,0xff,0x00},
    {0x87,0x5f,0xff}, {0xff,0x5f,0xaf}, {0x00,0xd7,0xff}, {0xff,0xff,0xff}
};

void sdl_hook_update(int y, int x) {
    int ch = uncursed_rhook_cp437_at(y, x);
    int a = uncursed_rhook_color_at(y, x);

    Uint8 *fgcolor = palette[a & 15];
    Uint8 *bgcolor = palette[(a >> 5) & 15];
    if (a & 16)  fgcolor = palette[7];
    if (a & 512) bgcolor = palette[0];
    /* TODO: Underlining (1024s bit) */

    /* Draw the background. */
    SDL_SetRenderDrawColor(render, bgcolor[0], bgcolor[1], bgcolor[2],
                           SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(render, &(SDL_Rect){
        .x = x * fontwidth, .y = y * fontheight,
        .w = fontwidth, .h = fontheight});
    /* TODO: Draw the foreground. */
    SDL_SetRenderDrawColor(render, fgcolor[0], fgcolor[1], fgcolor[2],
                           SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(render, &(SDL_Rect){
        .x = x * fontwidth + 2, .y = y * fontheight + 2,
        .w = fontwidth - 4, .h = fontheight - 4});

    uncursed_rhook_updated(y, x);
}

void sdl_hook_fullredraw(void) {
    int i, j;

    for (j = 0; j < winheight; j++)
        for (i = 0; i < winwidth; i++)
            sdl_hook_update(j, i);
}

void sdl_hook_flush(void) {
    SDL_RenderPresent(render);
}
