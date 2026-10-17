#include "qs_game.h"
#include <SDL.h>
#include <stdio.h>

/*
 * Intentionally original placeholder geometry. This viewer neither reads nor
 * embeds a Mega Drive ROM, sprite sheet, music track, or extracted level.
 */
static SDL_Color color(Uint8 r, Uint8 g, Uint8 b) {
    SDL_Color c = {r, g, b, 255};
    return c;
}

static void rectangle(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void render_map(SDL_Renderer *renderer, const QsGame *game) {
    int i;
    rectangle(renderer, 0, 0, QS_VIEW_W, QS_VIEW_H, color(31, 42, 64));
    rectangle(renderer, 0, 0, QS_VIEW_W, 12, color(209, 153, 91));
    for (i = 0; i < QS_STAGE_COUNT; i++) {
        int y = 45 + i * 53;
        int active = game->selected_stage == i;
        rectangle(renderer, 50, y, 300, 42,
                  active ? color(73, 116, 142) : color(50, 69, 91));
        rectangle(renderer, 50, y, 7, 42,
                  active ? color(255, 208, 109) : color(107, 113, 127));
        if (game->cleared[i])
            rectangle(renderer, 322, y + 12, 16, 16, color(94, 221, 138));
        rectangle(renderer, 70, y + 12, 150 + 30*i, 14,
                  active ? color(239, 227, 177) : color(144, 170, 184));
    }
    rectangle(renderer, 50, 215, 300, 4, color(130, 146, 167));
}

static void render_stage(SDL_Renderer *renderer, const QsGame *game) {
    int row, col, i;
    int cam = (int)game->camera_x;
    SDL_Color sky = game->stage == 0 ? color(133, 186, 212)
                  : game->stage == 1 ? color(216, 178, 139)
                  : color(106, 135, 166);
    SDL_Color tiles = game->stage == 0 ? color(113, 92, 114)
                    : game->stage == 1 ? color(142, 105, 74)
                    : color(83, 100, 128);
    rectangle(renderer, 0, 0, QS_VIEW_W, QS_VIEW_H, sky);
    for (i = 0; i < 16; ++i) {
        int x = i * 36 - (cam / 3) % 36;
        rectangle(renderer, x, 110 + i % 4 * 12, 24, 130,
                  color(83, 126, 150));
    }
    for (row = 0; row < QS_ROWS; ++row) {
        int first = cam / QS_TILE;
        int last = (cam + QS_VIEW_W) / QS_TILE + 1;
        for (col = first; col <= last && col < QS_COLUMNS; ++col) {
            if (col >= 0 && qs_solid(game, col, row)) {
                rectangle(renderer, col * QS_TILE - cam, row * QS_TILE,
                          QS_TILE, QS_TILE, tiles);
                rectangle(renderer, col * QS_TILE - cam, row * QS_TILE, QS_TILE, 3,
                          color(220, 190, 135));
            }
        }
    }
    /* Stage exit marker. */
    rectangle(renderer, (QS_COLUMNS - 4) * QS_TILE - cam, 161, 7, 47,
              color(255, 228, 134));
    for (i = 0; i < QS_MAX_ENEMIES; ++i) {
        const QsEnemy *e = &game->enemies[i];
        if (e->active)
            rectangle(renderer, (int)e->x - cam, (int)e->y, 14, 16,
                      e->stunned_frames ? color(112, 167, 221) : color(79, 60, 116));
    }
    for (i = 0; i < QS_MAX_PICKUPS; ++i) {
        const QsPickup *p = &game->pickups[i];
        if (p->active)
            rectangle(renderer, (int)p->x - cam, (int)p->y, 10, 10,
                      p->kind == QS_PICKUP_RED ? color(244, 79, 80) : color(240, 120, 160));
    }
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        const QsShot *s = &game->shots[i];
        if (s->active)
            rectangle(renderer, (int)s->x - cam, (int)s->y, s->attached ? 16 : 7, 4,
                      s->red ? color(230, 74, 78) : color(255, 225, 108));
    }
    if ((game->hero.hurt_frames % 8) < 4)
        rectangle(renderer, (int)game->hero.x - cam, (int)game->hero.y,
                  12, 20, color(250, 237, 167));
    rectangle(renderer, 0, 0, QS_VIEW_W, 15, color(28, 38, 54));
    for (i = 0; i < 3; i++)
        rectangle(renderer, 8 + i * 14, 4, 10, 8,
                  i < game->hero.hearts ? color(239, 96, 108) : color(90, 88, 107));
    rectangle(renderer, 355, 4, 30, 8, game->weapon == QS_RED
              ? color(224, 73, 70) : color(233, 207, 110));
}

static int pressed(const Uint8 *keys, SDL_Scancode key) { return keys[key] != 0; }
static int pad_button(SDL_GameController *pad, SDL_GameControllerButton button) {
    return pad && SDL_GameControllerGetButton(pad, button);
}

static QsInput read_input(SDL_GameController *pad) {
    QsInput result = {0};
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    Sint16 axis = pad ? SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) : 0;
    result.left = pressed(keys, SDL_SCANCODE_LEFT) || pressed(keys, SDL_SCANCODE_A) ||
                  pad_button(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || axis < -10000;
    result.right = pressed(keys, SDL_SCANCODE_RIGHT) || pressed(keys, SDL_SCANCODE_D) ||
                   pad_button(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || axis > 10000;
    result.up = pressed(keys, SDL_SCANCODE_UP) || pad_button(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
    result.down = pressed(keys, SDL_SCANCODE_DOWN) || pad_button(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    result.jump = pressed(keys, SDL_SCANCODE_SPACE) || pad_button(pad, SDL_CONTROLLER_BUTTON_B);
    result.fire = pressed(keys, SDL_SCANCODE_J) || pad_button(pad, SDL_CONTROLLER_BUTTON_Y);
    result.change = pressed(keys, SDL_SCANCODE_K) || pad_button(pad, SDL_CONTROLLER_BUTTON_X);
    result.dash = pressed(keys, SDL_SCANCODE_LSHIFT) ||
                  pad_button(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    result.select = pressed(keys, SDL_SCANCODE_RETURN) || pad_button(pad, SDL_CONTROLLER_BUTTON_A);
    result.back = pressed(keys, SDL_SCANCODE_ESCAPE) || pad_button(pad, SDL_CONTROLLER_BUTTON_START);
    return result;
}

int main(int argc, char **argv) {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_GameController *pad = NULL;
    QsGame game;
    Uint64 previous, frequency;
    double accumulator = 0.0;
    int running = 1, i;
    (void)argc;
    (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("QuackShot: experimental engine (no original assets)",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1200, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, QS_VIEW_W, QS_VIEW_H);
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) break;
        }
    }
    qs_init(&game);
    previous = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    while (running) {
        SDL_Event event;
        Uint64 now = SDL_GetPerformanceCounter();
        double seconds = (double)(now - previous) / (double)frequency;
        previous = now;
        accumulator += seconds > 0.25 ? 0.25 : seconds;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q))
                running = 0;
            if (event.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(event.cdevice.which);
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && pad &&
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == event.cdevice.which) {
                SDL_GameControllerClose(pad);
                pad = NULL;
            }
        }
        while (accumulator >= 1.0 / 60.0) {
            qs_step(&game, read_input(pad));
            accumulator -= 1.0 / 60.0;
        }
        if (game.mode == QS_MAP) render_map(renderer, &game);
        else render_stage(renderer, &game);
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
