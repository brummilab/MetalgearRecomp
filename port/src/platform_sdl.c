/*
 * SDL2 platform layer: window, presentation of the PS1 display area,
 * keyboard/gamepad input and audio output.
 */
#include "psx/psx.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static SDL_AudioDeviceID audio;
static SDL_GameController *pad;
static uint32_t      frame[640 * 512];
static uint16_t      buttons;

extern uint8_t psx_pad_axes[2][4];

/* PS1 button bits */
enum {
    B_SELECT = 1 << 0, B_L3 = 1 << 1, B_R3 = 1 << 2, B_START = 1 << 3,
    B_UP = 1 << 4, B_RIGHT = 1 << 5, B_DOWN = 1 << 6, B_LEFT = 1 << 7,
    B_L2 = 1 << 8, B_R2 = 1 << 9, B_L1 = 1 << 10, B_R1 = 1 << 11,
    B_TRIANGLE = 1 << 12, B_CIRCLE = 1 << 13, B_CROSS = 1 << 14, B_SQUARE = 1 << 15,
};

static const struct { SDL_Scancode key; uint16_t bit; } keymap[] = {
    { SDL_SCANCODE_UP, B_UP }, { SDL_SCANCODE_DOWN, B_DOWN },
    { SDL_SCANCODE_LEFT, B_LEFT }, { SDL_SCANCODE_RIGHT, B_RIGHT },
    { SDL_SCANCODE_RETURN, B_START }, { SDL_SCANCODE_BACKSPACE, B_SELECT },
    { SDL_SCANCODE_X, B_CROSS }, { SDL_SCANCODE_C, B_CIRCLE },
    { SDL_SCANCODE_Z, B_SQUARE }, { SDL_SCANCODE_V, B_TRIANGLE },
    { SDL_SCANCODE_A, B_L1 }, { SDL_SCANCODE_S, B_R1 },
    { SDL_SCANCODE_Q, B_L2 }, { SDL_SCANCODE_W, B_R2 },
};

static const struct { SDL_GameControllerButton b; uint16_t bit; } padmap[] = {
    { SDL_CONTROLLER_BUTTON_DPAD_UP, B_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, B_DOWN },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, B_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, B_RIGHT },
    { SDL_CONTROLLER_BUTTON_START, B_START }, { SDL_CONTROLLER_BUTTON_BACK, B_SELECT },
    { SDL_CONTROLLER_BUTTON_A, B_CROSS }, { SDL_CONTROLLER_BUTTON_B, B_CIRCLE },
    { SDL_CONTROLLER_BUTTON_X, B_SQUARE }, { SDL_CONTROLLER_BUTTON_Y, B_TRIANGLE },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, B_L1 }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, B_R1 },
    { SDL_CONTROLLER_BUTTON_LEFTSTICK, B_L3 }, { SDL_CONTROLLER_BUTTON_RIGHTSTICK, B_R3 },
};

int plat_init(int scale, int fullscreen)
{
    SDL_AudioSpec want, have;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        psx_log("SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    window = SDL_CreateWindow("Metal Gear Solid", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              320 * scale, 240 * scale,
                              SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!window) {
        psx_log("SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        psx_log("SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_RenderSetLogicalSize(renderer, 640, 480);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 512);

    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio)
        SDL_PauseAudioDevice(audio, 0);
    else
        psx_log("audio disabled: %s\n", SDL_GetError());
    return 0;
}

void plat_shutdown(void)
{
    if (audio)
        SDL_CloseAudioDevice(audio);
    SDL_Quit();
}

static void open_pad(void)
{
    int i;
    if (pad)
        return;
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i) && (pad = SDL_GameControllerOpen(i)) != NULL) {
            psx_log("gamepad: %s\n", SDL_GameControllerName(pad));
            return;
        }
    }
}

static uint8_t axis8(Sint16 v) { return (uint8_t)((v + 32768) >> 8); }

int plat_pump_events(void)
{
    SDL_Event e;
    const Uint8 *keys;
    size_t i;
    uint16_t b = 0;

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            return 0;
        case SDL_KEYDOWN:
            if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                return 0;
            if (e.key.keysym.scancode == SDL_SCANCODE_F11) {
                Uint32 fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_pad();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (pad) {
                SDL_GameControllerClose(pad);
                pad = NULL;
            }
            break;
        default:
            break;
        }
    }

    keys = SDL_GetKeyboardState(NULL);
    for (i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++)
        if (keys[keymap[i].key])
            b |= keymap[i].bit;

    open_pad();
    if (pad) {
        for (i = 0; i < sizeof(padmap) / sizeof(padmap[0]); i++)
            if (SDL_GameControllerGetButton(pad, padmap[i].b))
                b |= padmap[i].bit;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)
            b |= B_L2;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000)
            b |= B_R2;
        psx_pad_axes[0][0] = axis8(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
        psx_pad_axes[0][1] = axis8(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
        psx_pad_axes[0][2] = axis8(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
        psx_pad_axes[0][3] = axis8(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
    }
    buttons = b;
    return 1;
}

uint16_t plat_pad_buttons(int port) { return port == 0 ? buttons : 0; }

static uint32_t rgb555_to_argb(uint16_t c)
{
    uint32_t r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
    r |= r >> 5; g |= g >> 5; b |= b >> 5;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void plat_present(const uint16_t *vram, int x, int y, int w, int h, int rgb24, int enabled)
{
    int i, j;
    SDL_Rect src;

    if (w <= 0 || h <= 0)
        return;
    if (w > 640) w = 640;
    if (h > 512) h = 512;

    for (j = 0; j < h; j++) {
        const uint16_t *row = &vram[((y + j) & 511) * 1024];
        uint32_t *out = &frame[j * 640];
        if (!enabled) {
            memset(out, 0, (size_t)w * 4);
        } else if (rgb24) {
            /* 24-bit mode (movies): 3 bytes per pixel packed into VRAM */
            const uint8_t *bytes = (const uint8_t *)row;
            for (i = 0; i < w; i++) {
                int o = x * 2 + i * 3;
                out[i] = 0xFF000000u | ((uint32_t)bytes[(o) & 2047] << 16)
                       | ((uint32_t)bytes[(o + 1) & 2047] << 8) | bytes[(o + 2) & 2047];
            }
        } else {
            for (i = 0; i < w; i++)
                out[i] = rgb555_to_argb(row[(x + i) & 1023]);
        }
    }
    SDL_UpdateTexture(texture, NULL, frame, 640 * 4);
    src.x = 0;
    src.y = 0;
    src.w = w;
    src.h = h;
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &src, NULL);
    SDL_RenderPresent(renderer);
}

uint64_t plat_ticks_us(void)
{
    static uint64_t freq;
    if (!freq)
        freq = SDL_GetPerformanceFrequency();
    return SDL_GetPerformanceCounter() * 1000000ull / freq;
}

void plat_sleep_us(uint64_t us)
{
    uint64_t end = plat_ticks_us() + us;
    if (us > 2000)
        SDL_Delay((Uint32)((us - 1000) / 1000));
    while (plat_ticks_us() < end)
        ;
}

void plat_audio_queue(const int16_t *samples, int frames)
{
    if (audio)
        SDL_QueueAudio(audio, samples, (Uint32)frames * 4);
}

int plat_audio_queued_frames(void)
{
    return audio ? (int)(SDL_GetQueuedAudioSize(audio) / 4) : 0;
}
