#include "qs_game.h"
#include "qs_trace.h"
#include "qs_motion.h"
#include "qs_parallax.h"
#include <SDL.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
    QsParallax parallax = qs_parallax_offsets(game);
    int cam = parallax.front_px;
    SDL_Color sky = game->stage == 0 ? color(133, 186, 212)
                  : game->stage == 1 ? color(216, 178, 139)
                  : color(106, 135, 166);
    SDL_Color tiles = game->stage == 0 ? color(113, 92, 114)
                    : game->stage == 1 ? color(142, 105, 74)
                    : color(83, 100, 128);
    rectangle(renderer, 0, 0, QS_VIEW_W, QS_VIEW_H, sky);
    if (qs_parallax_is_reference_city(game)) {
        /* Original procedural geometry: NO Mega Drive pixels or art.
         * Far plane B at half scroll; middle plane A at full scroll. */
        for (i = 0; i < QS_VIEW_W / 64 + 2; ++i) {
            int x = i * 64 - (parallax.distant_px % 64);
            rectangle(renderer, x, 58, 25, 20, color(83, 126, 150));
            rectangle(renderer, x + 8, 48, 10, 10, color(83, 126, 150));
        }
        for (i = 0; i < QS_VIEW_W / 48 + 2; ++i) {
            int x = i * 48 - (parallax.front_px % 48);
            rectangle(renderer, x, 94, 18, 21, color(103, 98, 150));
            rectangle(renderer, x + 5, 85, 8, 9, color(103, 98, 150));
        }
    } else {
        /* Preserve the legacy 1/3-speed placeholder skyline. */
        for (i = 0; i < 16; ++i) {
            int x = i * 36 - (cam / 3) % 36;
            rectangle(renderer, x, 110 + i % 4 * 12, 24, 130,
                      color(83, 126, 150));
        }
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
    /*
     * On Switch, SDL2 normalizes Nintendo's physical south/east/west/north
     * face buttons to Xbox-style A/B/X/Y positions, not the printed labels:
     * Nintendo B(south)=SDL A, A(east)=SDL B,
     * Y(west)=SDL X, X(north)=SDL Y.
     * Desktop regression pads retain their original Xbox-style bindings.
     */
#ifdef __SWITCH__
    result.jump = pressed(keys, SDL_SCANCODE_SPACE) || pad_button(pad, SDL_CONTROLLER_BUTTON_A);
    result.fire = pressed(keys, SDL_SCANCODE_J) || pad_button(pad, SDL_CONTROLLER_BUTTON_X);
    result.change = pressed(keys, SDL_SCANCODE_K) || pad_button(pad, SDL_CONTROLLER_BUTTON_Y);
    result.select = pressed(keys, SDL_SCANCODE_RETURN) || pad_button(pad, SDL_CONTROLLER_BUTTON_B);
#else
    result.jump = pressed(keys, SDL_SCANCODE_SPACE) || pad_button(pad, SDL_CONTROLLER_BUTTON_B);
    result.fire = pressed(keys, SDL_SCANCODE_J) || pad_button(pad, SDL_CONTROLLER_BUTTON_Y);
    result.change = pressed(keys, SDL_SCANCODE_K) || pad_button(pad, SDL_CONTROLLER_BUTTON_X);
    result.select = pressed(keys, SDL_SCANCODE_RETURN) || pad_button(pad, SDL_CONTROLLER_BUTTON_A);
#endif
    result.dash = pressed(keys, SDL_SCANCODE_LSHIFT) ||
                  pad_button(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    result.back = pressed(keys, SDL_SCANCODE_ESCAPE) || pad_button(pad, SDL_CONTROLLER_BUTTON_START);
    return result;
}


/*
 * Host-only SDL smoke mode: exercise real software rasterization and pixel
 * readback without a display, GPU, controller, copyrighted assets, or ROM.
 * Repeat identical input traces to detect nondeterministic visual outcomes.
 */
static int capture_frame(SDL_Renderer *renderer, const QsGame *game, uint64_t *digest) {
    size_t bytes = (size_t)QS_VIEW_W * QS_VIEW_H * sizeof(uint32_t);
    uint32_t *pixels = (uint32_t *)malloc(bytes);
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    if (!pixels) {
        fprintf(stderr, "SDL smoke: allocation failed\n");
        return 0;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(renderer) != 0) {
        fprintf(stderr, "SDL smoke: render clear failed: %s\n", SDL_GetError());
        free(pixels);
        return 0;
    }
    if (game->mode == QS_MAP) render_map(renderer, game);
    else render_stage(renderer, game);
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             pixels, QS_VIEW_W * (int)sizeof(uint32_t)) != 0) {
        fprintf(stderr, "SDL smoke: render readback failed: %s\n", SDL_GetError());
        free(pixels);
        return 0;
    }
    for (i = 0; i < bytes / sizeof(uint32_t); ++i) {
        hash ^= pixels[i];
        hash *= UINT64_C(1099511628211);
    }
    free(pixels);
    *digest = hash;
    return 1;
}


#if !defined(__SWITCH__) && SDL_VERSION_ATLEAST(2, 0, 14)
/* Desktop-only integration of our controller bindings with SDL virtual pads. */
static int virtual_controller_smoke(void) {
    int joystick_index, ok = 0;
    SDL_GameController *controller = NULL;
    SDL_Joystick *joystick;
    char guid[64], mapping[512];
    QsInput in;
    joystick_index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 16, 0);
    if (joystick_index < 0) {
        fprintf(stderr, "SDL virtual controller attach: %s\n", SDL_GetError());
        return 0;
    }
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(joystick_index), guid, sizeof(guid));
    snprintf(mapping, sizeof(mapping),
             "%s,QuackShot CI Virtual,"
             "a:b0,b:b1,x:b2,y:b3,back:b4,start:b5,leftshoulder:b6,"
             "rightshoulder:b7,dpup:b8,dpdown:b9,dpleft:b10,dpright:b11,"
             "leftx:a0,lefty:a1,", guid);
    if (SDL_GameControllerAddMapping(mapping) < 0 ||
        !SDL_IsGameController(joystick_index) ||
        !(controller = SDL_GameControllerOpen(joystick_index))) {
        fprintf(stderr, "SDL virtual controller open/mapping: %s\n", SDL_GetError());
        goto cleanup;
    }
    joystick = SDL_GameControllerGetJoystick(controller);
    if (SDL_JoystickSetVirtualButton(joystick, 1, SDL_PRESSED) ||
        SDL_JoystickSetVirtualButton(joystick, 3, SDL_PRESSED) ||
        SDL_JoystickSetVirtualButton(joystick, 7, SDL_PRESSED) ||
        SDL_JoystickSetVirtualButton(joystick, 11, SDL_PRESSED)) {
        fprintf(stderr, "SDL virtual controller button injection failed\n");
        goto cleanup;
    }
    /* SDL can throttle joystick polling within a single millisecond. */
    SDL_Delay(30);
    SDL_PumpEvents();
    SDL_JoystickUpdate();
    SDL_GameControllerUpdate();
    in = read_input(controller);
    if (!in.jump || !in.fire || !in.dash || !in.right || in.left) {
        char *resolved = SDL_GameControllerMapping(controller);
        fprintf(stderr, "SDL controller mismatch: jump=%d fire=%d dash=%d right=%d left=%d "
                        "rawB=%d rawY=%d rawR=%d rawDpadRight=%d joyB=%d joyY=%d mapping=%s\n",
                in.jump, in.fire, in.dash, in.right, in.left,
                SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B),
                SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y),
                SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),
                SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT),
                SDL_JoystickGetButton(joystick, 1),
                SDL_JoystickGetButton(joystick, 3),
                resolved ? resolved : "none");
        SDL_free(resolved);
        goto cleanup;
    }
    SDL_JoystickSetVirtualButton(joystick, 1, SDL_RELEASED);
    SDL_JoystickSetVirtualButton(joystick, 3, SDL_RELEASED);
    SDL_JoystickSetVirtualButton(joystick, 7, SDL_RELEASED);
    SDL_JoystickSetVirtualButton(joystick, 11, SDL_RELEASED);
    SDL_JoystickSetVirtualAxis(joystick, 0, -25000);
    /* SDL can throttle joystick polling within a single millisecond. */
    SDL_Delay(30);
    SDL_PumpEvents();
    SDL_JoystickUpdate();
    SDL_GameControllerUpdate();
    in = read_input(controller);
    if (in.jump || in.fire || in.right || in.dash || !in.left) {
        fprintf(stderr, "SDL controller axis mapped unexpectedly\n");
        goto cleanup;
    }
    ok = 1;
cleanup:
    if (controller) SDL_GameControllerClose(controller);
    if (SDL_JoystickDetachVirtual(joystick_index) != 0) {
        fprintf(stderr, "SDL virtual controller detach: %s\n", SDL_GetError());
        ok = 0;
    }
    return ok;
}
#endif

/*
 * Pixels, not merely simulation fields: completing CITY TEST must
 * display a newly marked map, then re-enter at the same raster as a
 * fresh first-city stage with equivalent persistent unlocks.
 * Entirely synthetic shapes; no original game assets.
 */
static int run_progress_map_sdl_smoke(SDL_Renderer *renderer) {
    QsGame game, fresh, expected;
    QsInput select = {0}, none = {0};
    uint64_t map_before, map_after, expected_map, stage_reentry, fresh_stage;
    qs_init(&game);
    game.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    game.red_unlocked = 1;
    game.weapon = QS_RED;
    qs_begin_stage(&game, 0);
    expected = game;
    expected.mode = QS_MAP;
    if (!capture_frame(renderer, &expected, &map_before)) return 0;

    game.camera_x = 820;
    game.render_camera_x = 806;
    game.hero.x = (QS_COLUMNS - 4) * QS_TILE + 0.5f;
    game.hero.y = 188;
    game.shots[0].active = game.shots[0].attached = 1;
    game.shots[0].red = 1;
    game.shots[0].frames = 230;
    game.shots[0].x = 500;
    game.shots[0].y = 160;
    game.pending_reference_shot = game.pending_reference_red = 1;
    qs_step(&game, none);
    if (game.mode != QS_MAP || !game.cleared[0] ||
        game.shots[0].active || game.pending_reference_shot ||
        game.camera_x != 0 || game.render_camera_x != 0) {
        fprintf(stderr, "PROGRESS_MAP_SDL_FAIL: completed stage leaked transient data\n");
        return 0;
    }
    expected.cleared[0] = 1;
    if (!capture_frame(renderer, &game, &map_after) ||
        !capture_frame(renderer, &expected, &expected_map) ||
        map_before == map_after || map_after != expected_map) {
        fprintf(stderr, "PROGRESS_MAP_SDL_FAIL: completed map raster disagrees\n");
        return 0;
    }
    select.select = 1;
    select.dash = 1;
    qs_step(&game, select);
    qs_init(&fresh);
    fresh.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    fresh.red_unlocked = 1;
    fresh.weapon = QS_RED;
    fresh.cleared[0] = 1;
    qs_begin_stage(&fresh, 0);
    if (game.mode != QS_STAGE || game.stage != 0 ||
        game.camera_x != 0 || game.render_camera_x != 0 ||
        game.pending_reference_shot ||
        !capture_frame(renderer, &game, &stage_reentry) ||
        !capture_frame(renderer, &fresh, &fresh_stage) ||
        stage_reentry != fresh_stage) {
        fprintf(stderr, "PROGRESS_MAP_SDL_FAIL: reentry raster differs from fresh stage\n");
        return 0;
    }
    printf("PROGRESS_MAP_SDL_PASS before=%016llx completed=%016llx stage=%016llx\n",
           (unsigned long long)map_before,
           (unsigned long long)map_after,
           (unsigned long long)stage_reentry);
    return 1;
}

/*
 * Multi-stage map proof reads actual 1x1 ARGB8888 SDL pixels at the
 * center of each earned completion token. The token is drawn only
 * when its progress flag is set. We complete THREE independent
 * synthetic test stages and verify that earlier markers never vanish.
 */
static int read_map_progress_pixels(SDL_Renderer *renderer, const QsGame *g,
                                    uint32_t samples[QS_STAGE_COUNT]) {
    int i;
    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) != 0 ||
        SDL_RenderClear(renderer) != 0) return 0;
    render_map(renderer, g);
    for (i = 0; i < QS_STAGE_COUNT; ++i) {
        SDL_Rect one = {329, 45 + i * 53 + 19, 1, 1};
        if (SDL_RenderReadPixels(renderer, &one, SDL_PIXELFORMAT_ARGB8888,
                                 &samples[i], (int)sizeof(uint32_t)) != 0)
            return 0;
    }
    return 1;
}

static int run_multistage_map_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput in = {0};
    uint32_t pixels[QS_STAGE_COUNT];
    const uint32_t green = UINT32_C(0xff5edd8a); /* color(94,221,138) */
    int stage, row;
    qs_init(&g);
    g.red_unlocked = 1;
    g.weapon = QS_RED;
    if (!read_map_progress_pixels(renderer, &g, pixels)) return 0;
    for (row = 0; row < QS_STAGE_COUNT; ++row) {
        if (pixels[row] == green) return 0;
    }
    in.select = 1; in.dash = 1;
    qs_step(&g, in);
    for (stage = 0; stage < QS_STAGE_COUNT; ++stage) {
        if (g.mode != QS_STAGE || g.stage != stage ||
            (stage == 0 && g.physics_profile != QS_PHYSICS_REFERENCE_CITY) ||
            (stage > 0 && g.physics_profile != QS_PHYSICS_PROTOTYPE))
            return 0;
        g.hero.x = (QS_COLUMNS - 4) * QS_TILE + 0.5f;
        in = (QsInput){0};
        qs_step(&g, in);
        if (g.mode != QS_MAP || !g.cleared[stage] ||
            !read_map_progress_pixels(renderer, &g, pixels)) return 0;
        for (row = 0; row < QS_STAGE_COUNT; ++row) {
            if ((pixels[row] == green) != (row <= stage)) {
                fprintf(stderr, "MULTISTAGE_MAP_SDL_FAIL stage=%d row=%d pixel=%08x\n",
                        stage, row, (unsigned)pixels[row]);
                return 0;
            }
        }
        if (stage + 1 < QS_STAGE_COUNT) {
            in.right = 1;
            qs_step(&g, in);
            if (g.mode != QS_MAP || g.selected_stage != stage + 1) return 0;
            in = (QsInput){0};
            qs_step(&g, in);
            in.select = 1;
            qs_step(&g, in);
        }
    }
    /* From stage 2, R+A plus Right on the SAME tick must navigate
     * to stage 0 but NOT enter it. Confirm needs a new button edge. */
    in = (QsInput){0};
    in.right = 1; in.select = 1; in.dash = 1;
    qs_step(&g, in);
    if (g.mode != QS_MAP || g.selected_stage != 0) return 0;
    in = (QsInput){0};
    qs_step(&g, in);
    in.select = 1; in.dash = 1;
    qs_step(&g, in);
    if (g.mode != QS_STAGE || g.stage != 0 ||
        g.physics_profile != QS_PHYSICS_REFERENCE_CITY ||
        !g.cleared[0] || !g.cleared[1] || !g.cleared[2] ||
        !g.red_unlocked || g.weapon != QS_RED ||
        g.camera_x != 0 || g.render_camera_x != 0 ||
        g.pickups[0].active) return 0;
    puts("MULTISTAGE_MAP_SDL_PASS: three SDL completion tokens and atomic map navigation/reentry");
    return 1;
}

/*
 * Pixel-level selection proof for staggered map-button chords.
 * The highlight's left 7px strip is gold at the selected row and
 * grey at every other row. Sample real SDL_ARGB8888 pixels.
 */
static int map_selection_pixels(SDL_Renderer *renderer,
                                const QsGame *g, int selected) {
    int i;
    const uint32_t active = UINT32_C(0xffffd06d);
    const uint32_t inactive = UINT32_C(0xff6b717f);
    if (g->mode != QS_MAP || g->selected_stage != selected) return 0;
    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) != 0 ||
        SDL_RenderClear(renderer) != 0) return 0;
    render_map(renderer, g);
    for (i=0; i<QS_STAGE_COUNT; ++i) {
        SDL_Rect point = {52, 45 + 53*i + 19, 1, 1};
        uint32_t p=0;
        if (SDL_RenderReadPixels(renderer, &point, SDL_PIXELFORMAT_ARGB8888,
                                 &p, (int)sizeof(p)) != 0 ||
            p != (i==selected ? active : inactive)) {
            fprintf(stderr, "MAP_CHORD_SDL_FAIL row=%d pixel=%08x selected=%d\n",
                    i, (unsigned)p, selected);
            return 0;
        }
    }
    return 1;
}
static int run_map_chord_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput in = {0};
    qs_init(&g);
    if (!map_selection_pixels(renderer,&g,0)) return 0;
    in.left=1;
    qs_step(&g,in); /* wrap backwards to stage 2 */
    if (!map_selection_pixels(renderer,&g,2)) return 0;
    in.right=1; in.select=1;
    qs_step(&g,in); /* left still held: no forward navigation/enter */
    if (!map_selection_pixels(renderer,&g,2)) return 0;
    in.left=0;
    qs_step(&g,in); /* held right and select: no new event */
    if (!map_selection_pixels(renderer,&g,2)) return 0;
    in = (QsInput){0};
    qs_step(&g,in);
    in.right=1;
    qs_step(&g,in);
    if (!map_selection_pixels(renderer,&g,0)) return 0;
    in.select=1;
    qs_step(&g,in); /* right is held: no entry */
    if (!map_selection_pixels(renderer,&g,0)) return 0;
    in=(QsInput){0};
    qs_step(&g,in);
    in.select=1; in.dash=1;
    qs_step(&g,in);
    if (g.mode!=QS_STAGE || g.stage!=0 ||
        g.physics_profile!=QS_PHYSICS_REFERENCE_CITY) {
        fprintf(stderr,"MAP_CHORD_SDL_FAIL separate confirm failed\n");
        return 0;
    }
    puts("MAP_CHORD_SDL_PASS: opposing holds, suppressed confirm, three true SDL highlight rows");
    return 1;
}

/*
 * Pixel proof of an already-achieved CITY TEST finish with all stage
 * action buttons held in the same frame. Map markers and first-stage
 * pixels must be identical to an independent legitimate completion;
 * no held select/right button may re-enter or navigate on the next tick.
 */
static int run_finish_chord_sdl_smoke(SDL_Renderer *renderer) {
    QsGame simultaneous, expected, fresh;
    QsInput chord = {0}, released = {0};
    uint64_t completed_map, expected_map, actual_first, expected_first;
    qs_init(&simultaneous);
    simultaneous.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    simultaneous.red_unlocked = 1;
    simultaneous.weapon = QS_RED;
    qs_begin_stage(&simultaneous, 0);
    simultaneous.hero.x = (QS_COLUMNS - 4) * QS_TILE + 0.25f;
    simultaneous.camera_x = 820;
    simultaneous.render_camera_x = 805;
    simultaneous.pending_reference_shot = 1;
    simultaneous.pending_reference_red = 1;
    simultaneous.shots[0].active = 1;
    simultaneous.shots[0].attached = 1;
    simultaneous.shots[0].red = 1;
    simultaneous.shots[0].frames = 100;
    chord.back = chord.right = chord.select = chord.fire = chord.change = 1;
    chord.dash = 1;
    qs_step(&simultaneous, chord);
    if (simultaneous.mode != QS_MAP || !simultaneous.cleared[0] ||
        simultaneous.selected_stage != 0 ||
        simultaneous.camera_x != 0 || simultaneous.render_camera_x != 0 ||
        simultaneous.shots[0].active || simultaneous.pending_reference_shot) {
        fprintf(stderr, "FINISH_CHORD_SDL_FAIL: completion lost to Back chord\n");
        return 0;
    }
    qs_init(&expected);
    expected.red_unlocked = 1;
    expected.weapon = QS_RED;
    expected.cleared[0] = 1;
    if (!capture_frame(renderer,&simultaneous,&completed_map) ||
        !capture_frame(renderer,&expected,&expected_map) ||
        completed_map != expected_map) {
        fprintf(stderr, "FINISH_CHORD_SDL_FAIL: completed map raster differs\n");
        return 0;
    }
    qs_step(&simultaneous, chord); /* all buttons held: do nothing */
    if (simultaneous.mode != QS_MAP || simultaneous.selected_stage != 0)
        return 0;
    qs_step(&simultaneous, released);
    chord = released;
    chord.select = chord.dash = 1;
    qs_step(&simultaneous, chord);
    qs_init(&fresh);
    fresh.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    fresh.red_unlocked = 1;
    fresh.weapon = QS_RED;
    fresh.cleared[0] = 1;
    qs_begin_stage(&fresh, 0);
    if (simultaneous.mode != QS_STAGE ||
        simultaneous.physics_profile != QS_PHYSICS_REFERENCE_CITY ||
        !capture_frame(renderer,&simultaneous,&actual_first) ||
        !capture_frame(renderer,&fresh,&expected_first) ||
        actual_first != expected_first) {
        fprintf(stderr, "FINISH_CHORD_SDL_FAIL: re-entry raster differs\n");
        return 0;
    }
    printf("FINISH_CHORD_SDL_PASS: finish-map=%016llx reentry=%016llx held-input ignored\n",
           (unsigned long long)completed_map,
           (unsigned long long)actual_first);
    return 1;
}

/*
 * Stage-37 asset-free SDL raster test: a hero crossing the city exit
 * while overlapping a lethal enemy and a red unlock must still display
 * the completed-stage marker. The unlocked red state survives reentry.
 */
static int run_exit_interaction_sdl_smoke(SDL_Renderer *renderer) {
    QsGame game, expected, fresh;
    QsInput right = {0}, start = {0}, none = {0};
    uint64_t actual_map, reference_map, actual_reentry, reference_reentry;
    const float exit_x = (QS_COLUMNS - 4) * QS_TILE;
    qs_init(&game);
    game.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&game, 0);
    game.hero.x = exit_x - 0.1f;
    game.hero.y = 188;
    game.hero.vx = game.hero.vy = 0;
    game.hero.hearts = 1;
    game.hero.hurt_frames = 0;
    game.enemies[0].active = 1;
    game.enemies[0].x = exit_x + 0.5f;
    game.enemies[0].y = 188;
    game.enemies[0].min_x = game.enemies[0].max_x =
        game.enemies[0].x;
    game.enemies[0].stunned_frames = 0;
    game.pickups[0].active = 1;
    game.pickups[0].x = exit_x + 1;
    game.pickups[0].y = 189;
    game.pickups[0].kind = QS_PICKUP_RED;
    right.right = 1;
    qs_step(&game,right);
    if (game.mode != QS_MAP || !game.cleared[0] ||
        !game.red_unlocked || game.weapon != QS_RED ||
        game.hero.hearts != 1 || game.pickups[0].active ||
        game.camera_x != 0 || game.render_camera_x != 0) {
        fprintf(stderr, "EXIT_INTERACTION_SDL_FAIL: exit progress/unlock lost\n");
        return 0;
    }
    qs_init(&expected);
    expected.cleared[0] = 1;
    expected.red_unlocked = 1;
    expected.weapon = QS_RED;
    if (!capture_frame(renderer,&game,&actual_map) ||
        !capture_frame(renderer,&expected,&reference_map) ||
        actual_map != reference_map) {
        fprintf(stderr, "EXIT_INTERACTION_SDL_FAIL: map pixels disagree\n");
        return 0;
    }
    qs_step(&game,none);
    start.select = start.dash = 1;
    qs_step(&game,start);
    qs_init(&fresh);
    fresh.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    fresh.cleared[0] = 1;
    fresh.red_unlocked = 1;
    fresh.weapon = QS_RED;
    qs_begin_stage(&fresh,0);
    if (game.mode != QS_STAGE || !game.red_unlocked ||
        game.weapon != QS_RED || game.pickups[0].active ||
        !capture_frame(renderer,&game,&actual_reentry) ||
        !capture_frame(renderer,&fresh,&reference_reentry) ||
        actual_reentry != reference_reentry) {
        fprintf(stderr, "EXIT_INTERACTION_SDL_FAIL: first-stage pixels differ\n");
        return 0;
    }
    printf("EXIT_INTERACTION_SDL_PASS: map=%016llx reentry=%016llx red unlock retained\n",
           (unsigned long long)actual_map,
           (unsigned long long)actual_reentry);
    return 1;
}

/* Read the red spawn item at pixel 37,192: intentionally outside
 * the hero's 12x20 rectangle despite overlapping its hitbox. */
static int retry_spawn_pixel(SDL_Renderer *r,const QsGame *g,uint32_t *p) {
    SDL_Rect at = {37,192,1,1};
    if(SDL_SetRenderDrawColor(r,0,0,0,255)!=0 || SDL_RenderClear(r)!=0)
        return 0;
    render_stage(r,g);
    return SDL_RenderReadPixels(r,&at,SDL_PIXELFORMAT_ARGB8888,
                                p,(int)sizeof(*p))==0;
}
static int run_retry_pickup_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput in = {0};
    uint32_t first=0,next=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.hero.x=100;g.hero.y=188;g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=100;g.enemies[0].y=188;
    g.enemies[0].min_x=g.enemies[0].max_x=100;
    g.pickups[0].active=1;g.pickups[0].kind=QS_PICKUP_RED;
    g.pickups[0].x=36;g.pickups[0].y=189;
    qs_step(&g,in);
    if(g.hero.x!=40 || g.hero.hurt_frames!=89 ||
       !g.pickups[0].active || g.red_unlocked ||
       !retry_spawn_pixel(renderer,&g,&first) ||
       first!=UINT32_C(0xfff44f50)) {
        fprintf(stderr,"RETRY_PICKUP_SDL_FAIL: item vanished on death\n");
        return 0;
    }
    qs_step(&g,in);
    if(g.hero.hurt_frames!=88 || g.pickups[0].active ||
       !g.red_unlocked || g.weapon!=QS_RED ||
       !retry_spawn_pixel(renderer,&g,&next) || first==next) {
        fprintf(stderr,"RETRY_PICKUP_SDL_FAIL: legal next-tick pickup missing\n");
        return 0;
    }
    printf("RETRY_PICKUP_SDL_PASS: death=%08x next=%08x\n",
           (unsigned)first,(unsigned)next);
    return 1;
}

/* Read a rendered red/empty HUD heart, not merely g.hero.hearts.
 * The spawn is overlapped by another active enemy. Its next hit must
 * occur only AFTER the 89 protected post-respawn simulation steps. */
static int read_immunity_heart_pixel(SDL_Renderer *r,const QsGame *g,uint32_t *p) {
    SDL_Rect point={36,5,1,1};
    if(SDL_SetRenderDrawColor(r,0,0,0,255)!=0 || SDL_RenderClear(r)!=0)
        return 0;
    render_stage(r,g);
    return SDL_RenderReadPixels(r,&point,SDL_PIXELFORMAT_ARGB8888,
                                p,(int)sizeof(*p))==0;
}
static int run_multi_enemy_immunity_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t protected_heart=0, exposed_heart=0;
    int i;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.hero.x=100;g.hero.y=188;
    g.hero.hearts=1;g.hero.hurt_frames=0;
    for(i=0;i<3;i++) {
        QsEnemy *e=&g.enemies[i];
        e->active=1;
        e->x=i==2 ? 40 : 100;
        e->y=188;
        e->min_x=e->max_x=e->x;
        e->dir=1;
        e->stunned_frames=0;
    }
    qs_step(&g,idle);
    if(g.hero.x!=40 || g.hero.hearts!=3 || g.hero.hurt_frames!=89)
        return 0;
    for(i=0;i<89;i++) {
        qs_step(&g,idle);
        if(g.hero.hearts!=3) return 0;
    }
    if(g.hero.hurt_frames!=0 ||
       !read_immunity_heart_pixel(renderer,&g,&protected_heart) ||
       protected_heart!=UINT32_C(0xffef606c)) {
        fprintf(stderr,"MULTI_ENEMY_IMMUNITY_SDL_FAIL protected HUD\n");
        return 0;
    }
    qs_step(&g,idle);
    if(g.hero.hearts!=2 || g.hero.hurt_frames!=69 ||
       !read_immunity_heart_pixel(renderer,&g,&exposed_heart) ||
       exposed_heart!=UINT32_C(0xff5a586b)) {
        fprintf(stderr,"MULTI_ENEMY_IMMUNITY_SDL_FAIL exposed HUD\n");
        return 0;
    }
    printf("MULTI_ENEMY_IMMUNITY_SDL_PASS: protected=%08x exposed=%08x\n",
           (unsigned)protected_heart,(unsigned)exposed_heart);
    return 1;
}

/*
 * Stage-40 screen-level proof: two co-planar targets have staggered
 * y ranges, so a synthetic projectile may hit BOTH while distinct
 * SDL pixels exist exclusively inside either target's silhouette.
 */
static int coplanar_sprite_pixel(SDL_Renderer *renderer,
                                 const QsGame *g,int y,uint32_t *p) {
    SDL_Rect one={138,y,1,1};
    if(SDL_SetRenderDrawColor(renderer,0,0,0,255)!=0 ||
       SDL_RenderClear(renderer)!=0) return 0;
    render_stage(renderer,g);
    return SDL_RenderReadPixels(renderer,&one,SDL_PIXELFORMAT_ARGB8888,
                                p,(int)sizeof(*p))==0;
}
static int run_coplanar_impacts_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t upper=0,lower=0;
    int i;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=40;g.hero.y=188;
    for(i=0;i<2;i++) {
        QsEnemy *e=&g.enemies[i];
        e->active=1;e->x=135;e->y=184+i*8;
        e->dir=1;e->min_x=e->max_x=135;
    }
    g.shots[0].active=1;
    g.shots[0].frames=65;
    g.shots[0].x=129;g.shots[0].y=194;
    g.shots[0].vx=1;g.shots[0].red=1;
    qs_step(&g,idle);
    if(g.shots[0].active ||
       g.enemies[0].stunned_frames!=179 ||
       g.enemies[1].stunned_frames!=179 ||
       !coplanar_sprite_pixel(renderer,&g,186,&upper) ||
       !coplanar_sprite_pixel(renderer,&g,205,&lower) ||
       upper!=UINT32_C(0xff70a7dd) ||
       lower!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,
                "COPLANAR_IMPACTS_SDL_FAIL: upper=%08x lower=%08x\n",
                (unsigned)upper,(unsigned)lower);
        return 0;
    }
    printf("COPLANAR_IMPACTS_SDL_PASS: upper=%08x lower=%08x\n",
           (unsigned)upper,(unsigned)lower);
    return 1;
}

/*
 * Two simultaneous synthetic projectiles approach two enemies on the
 * same horizontal line, one near and one behind. Both enemies must
 * appear stunned on the software-rendered SDL image. Position test
 * pixels outside the other enemy and outside the 7x4 shot silhouettes.
 */
static int read_multishot_pixel(SDL_Renderer *renderer,
                                const QsGame *g, int x, int y,
                                uint32_t *pixel) {
    SDL_Rect at = {x,y,1,1};
    if (SDL_SetRenderDrawColor(renderer,0,0,0,255)!=0 ||
        SDL_RenderClear(renderer)!=0) return 0;
    render_stage(renderer,g);
    return SDL_RenderReadPixels(renderer,&at,SDL_PIXELFORMAT_ARGB8888,
                                pixel,(int)sizeof(*pixel))==0;
}
static int run_multishot_priority_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t near_pixel=0, far_pixel=0;
    int i;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=40;g.hero.y=188;
    for (i=0; i<2; ++i) {
        g.enemies[i].active=1;
        g.enemies[i].x=(float)(135 + 30*i);
        g.enemies[i].y=(float)(184 + 8*i);
        g.enemies[i].min_x=g.enemies[i].max_x=g.enemies[i].x;
        g.enemies[i].dir=1;
    }
    /* Intentionally put the trailing projectile in slot ZERO. */
    g.shots[0].active=1;g.shots[0].frames=65;
    g.shots[0].x=110;g.shots[0].y=194;g.shots[0].vx=70;
    g.shots[1].active=1;g.shots[1].frames=65;
    g.shots[1].x=130;g.shots[1].y=194;g.shots[1].vx=65;
    qs_step(&g,idle);
    if (g.enemies[0].stunned_frames!=179 ||
        g.enemies[1].stunned_frames!=179 ||
        g.shots[0].active || g.shots[1].active ||
        !read_multishot_pixel(renderer,&g,138,186,&near_pixel) ||
        !read_multishot_pixel(renderer,&g,168,205,&far_pixel) ||
        near_pixel!=UINT32_C(0xff70a7dd) ||
        far_pixel!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"MULTISHOT_PRIORITY_SDL_FAIL near=%08x far=%08x\n",
                (unsigned)near_pixel,(unsigned)far_pixel);
        return 0;
    }
    printf("MULTISHOT_PRIORITY_SDL_PASS: near=%08x far=%08x\n",
           (unsigned)near_pixel,(unsigned)far_pixel);
    return 1;
}

/*
 * Two opposing shots reach the same synthetic enemy at exactly 9/10
 * of a tick. Both must be consumed, regardless of their array slots.
 * Read an actual stun-color pixel from the rendered enemy.
 */
static int run_opposing_ties_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t enemy_pixel=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=40;g.hero.y=188;
    g.enemies[0].active=1;
    g.enemies[0].x=135;g.enemies[0].y=184;
    g.enemies[0].min_x=g.enemies[0].max_x=135;
    g.enemies[0].dir=1;
    g.shots[0].active=1;g.shots[0].frames=65;
    g.shots[0].x=120;g.shots[0].y=194;g.shots[0].vx=10;
    g.shots[0].red=0;
    g.shots[1].active=1;g.shots[1].frames=65;
    g.shots[1].x=157;g.shots[1].y=194;g.shots[1].vx=-10;
    g.shots[1].red=1;
    qs_step(&g,idle);
    if(g.enemies[0].stunned_frames!=179 ||
       g.shots[0].active || g.shots[1].active ||
       !read_multishot_pixel(renderer,&g,138,186,&enemy_pixel) ||
       enemy_pixel!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"OPPOSING_TIES_SDL_FAIL enemy=%08x shot0=%d shot1=%d\n",
                (unsigned)enemy_pixel,g.shots[0].active,g.shots[1].active);
        return 0;
    }
    printf("OPPOSING_TIES_SDL_PASS: enemy=%08x both projectiles consumed\n",
           (unsigned)enemy_pixel);
    return 1;
}

/*
 * Three shots: one hits the far-side foe early, a returning shot
 * passes through that foe and reaches the near-side foe LATE, while
 * a third shot reaches the near-side foe EARLIER. Both foes must be
 * blue in the actual SDL frame and the late returning shot must remain
 * active beyond the early-hit foe. This distinguishes global temporal
 * ordering from the old first-contact whole-shot ordering.
 */
static int run_global_timeline_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t first=0, second=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=40;g.hero.y=188;
    g.enemies[0].active=g.enemies[1].active=1;
    g.enemies[0].x=135;g.enemies[0].y=184;
    g.enemies[1].x=180;g.enemies[1].y=184;
    for(int i=0;i<2;i++) {
        g.enemies[i].min_x=g.enemies[i].max_x=g.enemies[i].x;
        g.enemies[i].dir=1;
    }
    g.shots[0].active=1;g.shots[0].frames=65;
    g.shots[0].x=196;g.shots[0].y=194;g.shots[0].vx=-40;
    g.shots[1].active=1;g.shots[1].frames=65;
    g.shots[1].x=209;g.shots[1].y=194;g.shots[1].vx=-100;
    g.shots[2].active=1;g.shots[2].frames=65;
    g.shots[2].x=110;g.shots[2].y=194;g.shots[2].vx=70;
    qs_step(&g,idle);
    if (g.enemies[0].stunned_frames!=179 ||
        g.enemies[1].stunned_frames!=179 ||
        g.shots[0].active || g.shots[2].active ||
        !g.shots[1].active || g.shots[1].x!=109 ||
        !read_multishot_pixel(renderer,&g,138,186,&first) ||
        !read_multishot_pixel(renderer,&g,183,186,&second) ||
        first!=UINT32_C(0xff70a7dd) ||
        second!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"GLOBAL_TIMELINE_SDL_FAIL near=%08x far=%08x pass=%d x=%.2f\n",
                (unsigned)first,(unsigned)second,
                g.shots[1].active,(double)g.shots[1].x);
        return 0;
    }
    printf("GLOBAL_TIMELINE_SDL_PASS: near=%08x far=%08x late-shot=%.0f\n",
           (unsigned)first,(unsigned)second,(double)g.shots[1].x);
    return 1;
}

/*
 * Stress all 8 projectile slots at the MAXIMUM supported finite
 * velocity (1280px/tick) in two vertically disjoint lanes. Both
 * independently rendered foes must turn blue; malformed ninth data
 * is not required to exercise the bounded scheduler.
 */
static int run_velocity_guard_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t top=0, bottom=0;
    int i;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=40;g.hero.y=188;
    for(i=0;i<2;i++) {
        QsEnemy *e=&g.enemies[i];
        e->active=1;
        e->x=i==0 ? 135.0f : 235.0f;
        e->y=i==0 ? 170.0f : 192.0f;
        e->min_x=e->max_x=e->x;
        e->dir=1;
    }
    for(i=0;i<QS_MAX_SHOTS;i++) {
        QsShot *s=&g.shots[i];
        s->active=1;s->frames=65;
        s->x=40;s->y=i<4 ? 177.0f : 195.0f;
        s->vx=1280.0f;
        s->red=(i&1);
    }
    qs_step(&g,idle);
    if(g.enemies[0].stunned_frames!=179 ||
       g.enemies[1].stunned_frames!=179 ||
       !read_multishot_pixel(renderer,&g,138,172,&top) ||
       !read_multishot_pixel(renderer,&g,238,203,&bottom) ||
       top!=UINT32_C(0xff70a7dd) ||
       bottom!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"VELOCITY_GUARD_SDL_FAIL top=%08x bottom=%08x\n",
                (unsigned)top,(unsigned)bottom);
        return 0;
    }
    printf("VELOCITY_GUARD_SDL_PASS: 8 full-width projectiles top=%08x bottom=%08x\n",
           (unsigned)top,(unsigned)bottom);
    return 1;
}

/* A non-finite enemy must be removed BEFORE any SDL int conversion.
 * The independent valid enemy should still draw a blue stun pixel. */
static int run_enemy_numeric_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t valid_pixel=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=40;g.hero.y=188;
    g.enemies[0].active=1;
    g.enemies[0].x=NAN;g.enemies[0].y=FLT_MAX;
    g.enemies[0].min_x=0;g.enemies[0].max_x=80;
    g.enemies[1].active=1;
    g.enemies[1].x=135;g.enemies[1].y=184;
    g.enemies[1].min_x=g.enemies[1].max_x=135;
    g.enemies[1].stunned_frames=2;
    qs_step(&g,idle);
    if (g.enemies[0].active ||
        g.enemies[0].x!=0 || g.enemies[0].y!=0 ||
        g.enemies[1].stunned_frames!=1 ||
        !read_multishot_pixel(renderer,&g,138,186,&valid_pixel) ||
        valid_pixel!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"ENEMY_NUMERIC_SDL_FAIL: valid-pixel=%08x active=%d\n",
                (unsigned)valid_pixel,g.enemies[0].active);
        return 0;
    }
    printf("ENEMY_NUMERIC_SDL_PASS: invalid removed, valid pixel=%08x\n",
           (unsigned)valid_pixel);
    return 1;
}

/* The stage-47 guard must retire invalid hero/camera values before
 * render_stage can cast them to pixel coordinates. Also prove a distinct
 * stunned actor is not lost by the player-state recovery. */
static int run_hero_camera_numeric_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t hero_pixel=0, enemy_pixel=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=NAN;g.hero.y=FLT_MAX;
    g.hero.vx=INFINITY;g.hero.vy=-INFINITY;
    g.camera_x=NAN;g.render_camera_x=INFINITY;
    g.enemies[0].active=1;
    g.enemies[0].x=135;g.enemies[0].y=184;
    g.enemies[0].min_x=g.enemies[0].max_x=135;
    g.enemies[0].dir=1;
    g.enemies[0].stunned_frames=2;
    qs_step(&g,idle);
    if (g.mode!=QS_STAGE ||
        g.hero.x!=40 || g.hero.y!=188 ||
        g.camera_x!=0 || g.render_camera_x!=0 ||
        !isfinite(g.hero.vx) || !isfinite(g.hero.vy) ||
        g.enemies[0].stunned_frames!=1 ||
        !read_multishot_pixel(renderer,&g,41,191,&hero_pixel) ||
        !read_multishot_pixel(renderer,&g,138,186,&enemy_pixel) ||
        hero_pixel!=UINT32_C(0xfffaeda7) ||
        enemy_pixel!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"HERO_CAMERA_NUMERIC_SDL_FAIL: hero=%08x enemy=%08x camera=%.2f\n",
                (unsigned)hero_pixel,(unsigned)enemy_pixel,(double)g.camera_x);
        return 0;
    }
    printf("HERO_CAMERA_NUMERIC_SDL_PASS: recovered hero=%08x valid enemy=%08x\n",
           (unsigned)hero_pixel,(unsigned)enemy_pixel);
    return 1;
}

/* Stage 48: prove the real SDL map image is safe immediately after
 * numeric recovery plus Back, then prove a held Select cannot relaunch.
 * Only a release/new edge may enter a freshly initialized CITY TEST. */
static int run_numeric_transition_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,baseline;
    QsInput in={0};
    uint64_t map_before=0,map_after=0,city_after=0;
    uint32_t progress[QS_STAGE_COUNT]={0};
    uint32_t hero_pixel=0;
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.cleared[2]=1;g.red_unlocked=1;g.weapon=QS_RED;
    g.hero.x=NAN;g.hero.y=FLT_MAX;
    g.camera_x=NAN;g.render_camera_x=INFINITY;
    g.pending_reference_shot=1;g.pending_reference_red=1;
    g.shots[0].active=g.shots[0].attached=1;
    g.shots[0].x=150;g.shots[0].y=166;g.shots[0].frames=230;
    qs_init(&baseline);baseline.cleared[2]=1;
    if (!capture_frame(renderer,&baseline,&map_before)) return 0;
    in.back=in.select=in.dash=1;
    qs_step(&g,in);
    if (g.mode!=QS_MAP || g.cleared[0] || !g.cleared[2] ||
        g.camera_x!=0 || g.render_camera_x!=0 ||
        g.pending_reference_shot || g.shots[0].active ||
        !capture_frame(renderer,&g,&map_after) ||
        map_after!=map_before ||
        !read_map_progress_pixels(renderer,&g,progress) ||
        progress[2]!=UINT32_C(0xff5edd8a) ||
        progress[0]==UINT32_C(0xff5edd8a)) {
        fprintf(stderr,"NUMERIC_TRANSITION_SDL_FAIL: corrupt->map before=%016llx after=%016llx\n",
                (unsigned long long)map_before,(unsigned long long)map_after);
        return 0;
    }
    in.back=0;qs_step(&g,in);
    if (g.mode!=QS_MAP) {
        fputs("NUMERIC_TRANSITION_SDL_FAIL: held Select reentered stage\n",stderr);
        return 0;
    }
    memset(&in,0,sizeof(in));qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if (g.mode!=QS_STAGE || g.stage!=0 ||
        g.physics_profile!=QS_PHYSICS_REFERENCE_CITY ||
        g.hero.x!=40 || g.hero.y!=188 ||
        g.camera_x!=0 || g.render_camera_x!=0 ||
        !capture_frame(renderer,&g,&city_after) ||
        city_after==map_after ||
        !read_multishot_pixel(renderer,&g,41,191,&hero_pixel) ||
        hero_pixel!=UINT32_C(0xfffaeda7)) {
        fprintf(stderr,"NUMERIC_TRANSITION_SDL_FAIL: map->city hero=%08x stage=%d\n",
                (unsigned)hero_pixel,g.stage);
        return 0;
    }
    printf("NUMERIC_TRANSITION_SDL_PASS: safe map=%016llx city=%016llx hero=%08x\n",
           (unsigned long long)map_after,
           (unsigned long long)city_after,(unsigned)hero_pixel);
    return 1;
}

/*
 * Stage 49: a NaN/huge pickup is retired before the real SDL raster
 * converts its coordinates, while a valid red item stays visible and can
 * be collected normally. The geometry and color are original placeholders.
 */
static int run_pickup_numeric_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t red_pixel=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.enemies,0,sizeof(g.enemies));
    g.pickups[0].active=1;
    g.pickups[0].x=NAN;g.pickups[0].y=FLT_MAX;
    g.pickups[0].kind=QS_PICKUP_HEART;
    g.pickups[1].active=1;
    g.pickups[1].x=179;g.pickups[1].y=140;
    g.pickups[1].kind=QS_PICKUP_RED;
    qs_step(&g,idle);
    if (g.pickups[0].active || g.pickups[0].x!=0 || g.pickups[0].y!=0 ||
        !g.pickups[1].active || g.red_unlocked ||
        !read_multishot_pixel(renderer,&g,180,142,&red_pixel) ||
        red_pixel!=UINT32_C(0xfff44f50)) {
        fprintf(stderr,"PICKUP_NUMERIC_SDL_FAIL: red=%08x active=%d\n",
                (unsigned)red_pixel,g.pickups[0].active);
        return 0;
    }
    g.hero.x=179;g.hero.y=140;
    g.hero.vx=g.hero.vy=0;
    qs_step(&g,idle);
    if (g.pickups[1].active || !g.red_unlocked || g.weapon!=QS_RED) {
        fputs("PICKUP_NUMERIC_SDL_FAIL: healthy red pickup did not award\n",stderr);
        return 0;
    }
    printf("PICKUP_NUMERIC_SDL_PASS: invalid retired, healthy red=%08x collected\n",
           (unsigned)red_pixel);
    return 1;
}

static int run_sdl_smoke(SDL_Renderer *renderer) {
#if !defined(__SWITCH__) && SDL_VERSION_ATLEAST(2, 0, 14)
    if (!virtual_controller_smoke()) return 1;
#endif
    uint64_t reference[QS_STAGE_COUNT + 1] = {0};
    int replay, stage, frame;
    for (replay = 0; replay < 2; ++replay) {
        QsGame game;
        uint64_t digest;
        qs_init(&game);
        if (!capture_frame(renderer, &game, &digest)) return 1;
        if (replay && digest != reference[0]) {
            fprintf(stderr, "SDL smoke: map image changed between replays\n");
            return 1;
        }
        if (!replay) reference[0] = digest;
        for (stage = 0; stage < QS_STAGE_COUNT; ++stage) {
            qs_begin_stage(&game, stage);
            for (frame = 0; frame < 65; ++frame) {
                QsInput in = {0};
                in.right = frame < 36;
                in.jump = frame == 8 || frame == 42;
                in.fire = frame == 15;
                qs_step(&game, in);
            }
            if (game.mode != QS_STAGE || !capture_frame(renderer, &game, &digest)) {
                fprintf(stderr, "SDL smoke: stage %d unavailable\n", stage);
                return 1;
            }
            if (digest == reference[0] ||
                (replay && digest != reference[stage + 1])) {
                fprintf(stderr, "SDL smoke: stage %d frame mismatch\n", stage);
                return 1;
            }
            if (!replay) reference[stage + 1] = digest;
        }
    }
    for (stage = 1; stage <= QS_STAGE_COUNT; ++stage) {
        for (frame = 1; frame < stage; ++frame) {
            if (reference[stage] == reference[frame]) {
                fprintf(stderr, "SDL smoke: duplicate stage frames\n");
                return 1;
            }
        }
    }
    if (!run_progress_map_sdl_smoke(renderer)) return 1;
    if (!run_multistage_map_sdl_smoke(renderer)) return 1;
    if (!run_map_chord_sdl_smoke(renderer)) return 1;
    if (!run_finish_chord_sdl_smoke(renderer)) return 1;
    if (!run_exit_interaction_sdl_smoke(renderer)) return 1;
    if (!run_retry_pickup_sdl_smoke(renderer)) return 1;
    if (!run_multi_enemy_immunity_sdl_smoke(renderer)) return 1;
    if (!run_coplanar_impacts_sdl_smoke(renderer)) return 1;
    if (!run_multishot_priority_sdl_smoke(renderer)) return 1;
    if (!run_opposing_ties_sdl_smoke(renderer)) return 1;
    if (!run_global_timeline_sdl_smoke(renderer)) return 1;
    if (!run_velocity_guard_sdl_smoke(renderer)) return 1;
    if (!run_enemy_numeric_sdl_smoke(renderer)) return 1;
    if (!run_hero_camera_numeric_sdl_smoke(renderer)) return 1;
    if (!run_numeric_transition_sdl_smoke(renderer)) return 1;
    if (!run_pickup_numeric_sdl_smoke(renderer)) return 1;
    printf("SDL_SMOKE_PASS map=%016llx stage0=%016llx stage1=%016llx stage2=%016llx\n",
           (unsigned long long)reference[0],
           (unsigned long long)reference[1],
           (unsigned long long)reference[2],
           (unsigned long long)reference[3]);
    return 0;
}


/* Verify the REAL SDL raster output of independent synthetic background
 * strips: a +2 px camera shift must shift far B by 1 px and near A by 2.
 * Nothing from the original game is used as reference imagery.
 */
static int parallax_smoke_capture(SDL_Renderer *renderer,
                                  const QsGame *g, int y, uint32_t *row) {
    SDL_Rect scan = {0, y, QS_VIEW_W, 1};
    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) != 0 ||
        SDL_RenderClear(renderer) != 0) return 0;
    render_stage(renderer, g);
    return SDL_RenderReadPixels(renderer, &scan, SDL_PIXELFORMAT_ARGB8888,
                                row, QS_VIEW_W * (int)sizeof(uint32_t)) == 0;
}
static int run_parallax_sdl_smoke(SDL_Renderer *renderer) {
    QsGame game;
    /* Four consecutive camera pixels exercise both rounding parities.
     * Camera:    40  41  42  43
     * Plane A:   40  41  42  43
     * Plane B:   20  20  21  21
     */
    uint32_t far[4][QS_VIEW_W], near[4][QS_VIEW_W];
    uint32_t far_return[QS_VIEW_W], near_return[QS_VIEW_W];
    unsigned far_edges = 0, near_edges = 0;
    int x, row, col, sample;
    qs_init(&game);
    game.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&game, 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            game.terrain[row][col] = '.';
    game.hero.y = 188;
    for (sample = 0; sample < 4; ++sample) {
        QsParallax offset;
        game.camera_x = (float)(40 + sample);
        game.render_camera_x = game.camera_x;
        offset = qs_parallax_offsets(&game);
        if (offset.front_px != 40 + sample ||
            offset.distant_px != (40 + sample) / 2 ||
            !parallax_smoke_capture(renderer, &game, 69, far[sample]) ||
            !parallax_smoke_capture(renderer, &game, 103, near[sample])) {
            fprintf(stderr, "PARALLAX_SDL_FAIL: sample %d missing or incorrect\n", sample);
            return 1;
        }
    }
    /* Scan away from border pixels so clipping never masks a real error.
     * For each +1 camera step the near layer moves 1; the far layer
     * moves either 0 or 1 depending on odd/even floor rounding.
     */
    for (sample = 1; sample < 4; ++sample) {
        int far_delta = ((40 + sample) / 2) - ((39 + sample) / 2);
        for (x = 24; x < QS_VIEW_W - 26; ++x) {
            if (far[sample][x] != far[sample-1][x + far_delta] ||
                near[sample][x] != near[sample-1][x + 1]) {
                fprintf(stderr, "PARALLAX_SDL_FAIL: parity sample=%d pixel=%d\n",
                        sample, x);
                return 1;
            }
        }
    }
    /* Revisit the original offset after moving backwards: no history-
     * dependent drift is permitted in the procedural plane positions.
     */
    game.camera_x = 40;
    game.render_camera_x = game.camera_x;
    if (!parallax_smoke_capture(renderer, &game, 69, far_return) ||
        !parallax_smoke_capture(renderer, &game, 103, near_return))
        return 1;
    for (x = 0; x < QS_VIEW_W; ++x) {
        if (far_return[x] != far[0][x] ||
            near_return[x] != near[0][x]) {
            fprintf(stderr, "PARALLAX_SDL_FAIL: camera reversal drift at x=%d\n", x);
            return 1;
        }
        /* Edge counts exclude clipped margins, unlike reverse equality. */
        if (x >= 24 && x < QS_VIEW_W - 26) {
            if (far[0][x] != far[0][x+1]) ++far_edges;
            if (near[0][x] != near[0][x+1]) ++near_edges;
        }
    }
    if (far_edges < 5 || near_edges < 5) {
        fprintf(stderr, "PARALLAX_SDL_FAIL: layers not visually distinct\n");
        return 1;
    }
    puts("PARALLAX_SDL_PASS: four-pixel parity 0/1/0 shift, no reverse drift, original-free geometry");
    return 0;
}


/*
 * Real SDL pixel-level consistency test for ALL rendered world objects.
 * Stage 21 proved simulated current/render camera pairs in guest CSV;
 * Stage 24 separately tests that actual raster output uses presentation
 * camera for hero, terrain, enemies, pickups, shots and both planes.
 * These synthetic states intentionally exaggerate the current-vs-render
 * difference to reveal accidentally reading the wrong camera field.
 */
#define QS_LATCH_ROWS 8
#define QS_LATCH_STATES 5
static const int camera_latch_scanlines[QS_LATCH_ROWS] = {
    6, 69, 103, 136, 155, 172, 182, 185
};

static int camera_latch_capture(SDL_Renderer *renderer, const QsGame *g,
                                uint32_t rows[QS_LATCH_ROWS][QS_VIEW_W]) {
    int i;
    if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) != 0 ||
        SDL_RenderClear(renderer) != 0) return 0;
    render_stage(renderer, g);
    for (i = 0; i < QS_LATCH_ROWS; ++i) {
        SDL_Rect scan = {0, camera_latch_scanlines[i], QS_VIEW_W, 1};
        if (SDL_RenderReadPixels(renderer, &scan, SDL_PIXELFORMAT_ARGB8888,
                                 rows[i], QS_VIEW_W * (int)sizeof(uint32_t)) != 0)
            return 0;
    }
    return 1;
}

static int run_camera_latch_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    static uint32_t pixels[QS_LATCH_STATES][QS_LATCH_ROWS][QS_VIEW_W];
    static const int current[QS_LATCH_STATES] = {40, 99, 100, 101, 48};
    static const int presented[QS_LATCH_STATES] = {40, 40, 41, 42, 40};
    struct Probe { int row, x; uint32_t rgb; const char *name; };
    static const struct Probe probes[] = {
        {0, 10, 0xef606c, "HUD heart"},
        {3, 164, 0xfaeda7, "hero"},
        {4, 223, 0x4f3c74, "enemy"},
        {5, 255, 0xf078a0, "pickup"},
        {6, 284, 0xffe16c, "shot"},
        {7, 203, 0x715c72, "terrain"}
    };
    int s, row, x;
    size_t n;

    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (x = 0; x < QS_COLUMNS; ++x)
            g.terrain[row][x] = '.';
    g.terrain[11][15] = '#';
    for (s = 0; s < QS_MAX_ENEMIES; ++s) g.enemies[s].active = 0;
    for (s = 0; s < QS_MAX_PICKUPS; ++s) g.pickups[s].active = 0;
    for (s = 0; s < QS_MAX_SHOTS; ++s) g.shots[s].active = 0;
    g.hero.x = 201;
    g.hero.y = 130;
    g.hero.hurt_frames = 0;
    g.enemies[0].active = 1;
    g.enemies[0].x = 260;
    g.enemies[0].y = 151;
    g.pickups[0].active = 1;
    g.pickups[0].kind = QS_PICKUP_HEART;
    g.pickups[0].x = 292;
    g.pickups[0].y = 169;
    g.shots[0].active = 1;
    g.shots[0].x = 322;
    g.shots[0].y = 181;
    g.shots[0].red = 0;

    for (s = 0; s < QS_LATCH_STATES; ++s) {
        g.camera_x = (float)current[s];
        g.render_camera_x = (float)presented[s];
        if (!camera_latch_capture(renderer, &g, pixels[s])) {
            fprintf(stderr, "CAMERA_LATCH_SDL_FAIL: SDL raster readback %d: %s\n",
                    s, SDL_GetError());
            return 1;
        }
    }
    /* Strong object-presence check: shift relations alone can pass on
     * an empty raster, so ensure every class has distinct real pixels. */
    for (n = 0; n < sizeof(probes) / sizeof(probes[0]); ++n) {
        uint32_t rgb = pixels[0][probes[n].row][probes[n].x] & UINT32_C(0x00ffffff);
        if (rgb != probes[n].rgb) {
            fprintf(stderr, "CAMERA_LATCH_SDL_FAIL: absent %s, actual=%06x expected=%06x\n",
                    probes[n].name, (unsigned)rgb, (unsigned)probes[n].rgb);
            return 1;
        }
    }
    for (row = 0; row < QS_LATCH_ROWS; ++row) {
        for (x = 0; x < QS_VIEW_W; ++x) {
            if (pixels[0][row][x] != pixels[1][row][x] ||
                pixels[0][row][x] != pixels[4][row][x]) {
                fprintf(stderr, "CAMERA_LATCH_SDL_FAIL: current camera leaked into render row=%d x=%d\n",
                        camera_latch_scanlines[row], x);
                return 1;
            }
        }
    }
    /* HUD is screen-fixed. Far B moves 0/1 px, near A moves 1/2,
     * all five foreground feature classes move 1/2 for render 41/42.
     * Test actual renderer output, not a second coordinate calculation. */
    for (s = 2; s <= 3; ++s) {
        for (row = 0; row < QS_LATCH_ROWS; ++row) {
            int shift = row == 0 ? 0 : row == 1 ? (presented[s] / 2 - 20)
                                                : presented[s] - 40;
            for (x = 32; x < QS_VIEW_W - 36; ++x) {
                if (pixels[s][row][x] != pixels[0][row][x + shift]) {
                    fprintf(stderr,
                            "CAMERA_LATCH_SDL_FAIL: world-plane sync row=%d x=%d shift=%d state=%d\n",
                            camera_latch_scanlines[row], x, shift, s);
                    return 1;
                }
            }
        }
    }
    puts("CAMERA_LATCH_SDL_PASS: renderer latches world, actor, projectiles, tiles, both planes; HUD fixed; simulated camera isolated");
    return 0;
}

/* Records only numeric state, never file paths, controllers' IDs or ROM data. */
static void runtime_event(QsTrace *trace, const QsGame *g, QsTraceEvent event,
                          int controller_count, int detail) {
    (void)qs_trace_emit(trace, (unsigned)SDL_GetTicks(), (unsigned)g->frames,
                        event, (int)g->mode, g->stage, controller_count, detail);
}

int main(int argc, char **argv) {
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_GameController *pad = NULL;
    QsGame game;
    QsTrace trace = {0};
    QsMotion motion = {0};
    Uint64 previous, frequency;
    double accumulator = 0.0;
    int running = 1, i, shown_first_frame = 0;
    unsigned probe_frames = 0;
    unsigned next_heartbeat = 600;
    /* Bounded 24-tick windows after the first forward/reverse camera move.
     * They let the host verify actual consecutive simulation ticks, not
     * merely two sparse samples from a long gameplay trace. */
    int burst_forward_seen = 0, burst_reverse_seen = 0;
    int burst_forward_left = 0, burst_reverse_left = 0;
    const char *trace_path = getenv("QS_TRACE_PATH");
    const char *motion_path = getenv("QS_MOTION_PATH");
    int smoke = 0, parallax_smoke = 0, camera_latch_smoke = 0;
#ifdef __SWITCH__
    /* Homebrew arguments may include the launch pathname: do not reject them. */
    (void)argc;
    (void)argv;
    if (!trace_path || !*trace_path)
        trace_path = "sdmc:/switch/quackshot-runtime.csv";
    if (!motion_path || !*motion_path)
        motion_path = "sdmc:/switch/quackshot-motion.csv";
#else
    smoke = argc == 2 && strcmp(argv[1], "--smoke") == 0;
    parallax_smoke = argc == 2 && strcmp(argv[1], "--parallax-smoke") == 0;
    camera_latch_smoke = argc == 2 && strcmp(argv[1], "--camera-latch-smoke") == 0;
    if (argc == 3 && strcmp(argv[1], "--probe-frames") == 0) {
        char *end = NULL;
        long count = strtol(argv[2], &end, 10);
        if (argv[2][0] && end && *end == '\0' && count >= 1 && count <= 1200)
            probe_frames = (unsigned)count;
    }
    if ((argc != 1 && !smoke && !parallax_smoke && !camera_latch_smoke && !probe_frames) || (argc == 3 && !probe_frames)) {
        fprintf(stderr, "Usage: %s [--smoke | --parallax-smoke | --camera-latch-smoke | --probe-frames 1..1200]\n", argv[0]);
        return 2;
    }
#endif
    qs_init(&game);
    if (trace_path && *trace_path) {
        if (!qs_trace_open(&trace, trace_path))
            fprintf(stderr, "QuackShot: runtime CSV unavailable (continuing without it)\n");
        else
            runtime_event(&trace, &game, QS_TRACE_BOOT, 0, 0);
    }
    if (motion_path && *motion_path && !qs_motion_open(&motion, motion_path))
        fprintf(stderr, "QuackShot: numeric motion CSV unavailable (non-fatal)\n");
    /* Hidden CI windows cannot receive controller events unless enabled. */
    if (smoke) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        runtime_event(&trace, &game, QS_TRACE_FAILURE, 0, 1);
        qs_motion_close(&motion);
        qs_trace_close(&trace);
        return 1;
    }
    runtime_event(&trace, &game, QS_TRACE_SDL_READY, 0, 0);
    window = SDL_CreateWindow("QuackShot: experimental engine (no original assets)",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              (smoke || parallax_smoke || camera_latch_smoke) ? QS_VIEW_W : 1200,
                              (smoke || parallax_smoke || camera_latch_smoke) ? QS_VIEW_H : 720,
                              (smoke || parallax_smoke || camera_latch_smoke) ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        runtime_event(&trace, &game, QS_TRACE_FAILURE, 0, 2);
        SDL_Quit();
        qs_motion_close(&motion);
        qs_trace_close(&trace);
        return 1;
    }
    runtime_event(&trace, &game, QS_TRACE_WINDOW_READY, 0, 0);
    renderer = (smoke || parallax_smoke || camera_latch_smoke) ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE)
                     : SDL_CreateRenderer(window, -1,
                           SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        runtime_event(&trace, &game, QS_TRACE_FAILURE, 0, 3);
        SDL_DestroyWindow(window);
        SDL_Quit();
        qs_motion_close(&motion);
        qs_trace_close(&trace);
        return 1;
    }
    /* Detail records SDL_RENDERER_SOFTWARE, not a model of the graphics API. */
    {
        SDL_RendererInfo info;
        int software = SDL_GetRendererInfo(renderer, &info) == 0 &&
                       (info.flags & SDL_RENDERER_SOFTWARE) != 0;
        runtime_event(&trace, &game, QS_TRACE_RENDERER_READY, 0, software);
    }
    if (SDL_RenderSetLogicalSize(renderer, QS_VIEW_W, QS_VIEW_H) != 0) {
        fprintf(stderr, "SDL_RenderSetLogicalSize: %s\n", SDL_GetError());
        runtime_event(&trace, &game, QS_TRACE_FAILURE, 0, 4);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        qs_motion_close(&motion);
        qs_trace_close(&trace);
        return 1;
    }
    if (parallax_smoke || camera_latch_smoke) {
        int result = parallax_smoke ? run_parallax_sdl_smoke(renderer)
                                    : run_camera_latch_sdl_smoke(renderer);
        runtime_event(&trace, &game, QS_TRACE_SMOKE, 0, result);
        runtime_event(&trace, &game, QS_TRACE_SHUTDOWN, 0, result);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        qs_motion_close(&motion);
        qs_trace_close(&trace);
        return result;
    }
    if (smoke) {
        int result = run_sdl_smoke(renderer);
        runtime_event(&trace, &game, QS_TRACE_SMOKE, 0, result);
        runtime_event(&trace, &game, QS_TRACE_SHUTDOWN, 0, result);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        qs_motion_close(&motion);
        qs_trace_close(&trace);
        return result;
    }
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) break;
        }
    }
    if (pad) runtime_event(&trace, &game, QS_TRACE_CONTROLLER, 1, 1);
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
            if (event.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(event.cdevice.which);
                if (pad) runtime_event(&trace, &game, QS_TRACE_CONTROLLER, 1, 1);
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && pad &&
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == event.cdevice.which) {
                SDL_GameControllerClose(pad);
                pad = NULL;
                runtime_event(&trace, &game, QS_TRACE_CONTROLLER, 0, 0);
            }
        }
        while (accumulator >= 1.0 / 60.0) {
            QsInput in = read_input(pad);
            QsMode old_mode = game.mode;
            int old_stage = game.stage;
            float camera_before = game.camera_x;
            unsigned input_before = qs_motion_input_bits(game.previous);
            unsigned input_after = qs_motion_input_bits(in);
            qs_step(&game, in);
            accumulator -= 1.0 / 60.0;
            if (game.mode == QS_STAGE &&
                ((game.frames % 5) == 0 || old_mode != game.mode ||
                 input_before != input_after))
                (void)qs_motion_emit(&motion, &game, (unsigned)SDL_GetTicks(), in);
            if (old_mode != game.mode || old_stage != game.stage)
                runtime_event(&trace, &game, QS_TRACE_STAGE_CHANGE, pad != NULL,
                              (int)game.physics_profile);
            if (qs_parallax_is_reference_city(&game)) {
                if (!burst_forward_seen && game.camera_x > camera_before) {
                    burst_forward_seen = 1;
                    burst_forward_left = 24;
                } else if (burst_forward_seen && !burst_reverse_seen &&
                           game.camera_x < camera_before) {
                    burst_reverse_seen = 1;
                    burst_reverse_left = 24;
                }
                if (burst_forward_left > 0 || burst_reverse_left > 0) {
                    /* Both halves are the real guest camera state after
                     * qs_step(). There is no host-generated surrogate. */
                    runtime_event(&trace, &game, QS_TRACE_CAMERA_STEP,
                                  pad != NULL,
                                  (((int)game.camera_x) << 16) |
                                  (int)game.render_camera_x);
                    if (burst_forward_left > 0) --burst_forward_left;
                    else --burst_reverse_left;
                }
            }
            if (qs_parallax_is_reference_city(&game) &&
                ((game.frames % 15u) == 0)) {
                QsParallax p = qs_parallax_offsets(&game);
                /* Guest evidence: simulation position before presentation.
                 * Two 16-bit nonnegative scroll coordinates per record. */
                runtime_event(&trace, &game, QS_TRACE_CAMERA_PHASE, pad != NULL,
                              (((int)game.camera_x) << 16) |
                              (int)game.render_camera_x);
                /* Render offsets: high16 foreground, low16 distant. */
                runtime_event(&trace, &game, QS_TRACE_PARALLAX, pad != NULL,
                              (p.front_px << 16) | p.distant_px);
            }
            if ((unsigned)game.frames >= next_heartbeat) {
                runtime_event(&trace, &game, QS_TRACE_HEARTBEAT, pad != NULL, 0);
                next_heartbeat += 600;
            }
        }
        if (game.mode == QS_MAP) render_map(renderer, &game);
        else render_stage(renderer, &game);
        SDL_RenderPresent(renderer);
        if (!shown_first_frame) {
            shown_first_frame = 1;
            runtime_event(&trace, &game, QS_TRACE_FIRST_FRAME, pad != NULL, 0);
        }
        if (probe_frames && (unsigned)game.frames >= probe_frames)
            running = 0;
        SDL_Delay(1);
    }
    runtime_event(&trace, &game, QS_TRACE_SHUTDOWN, pad != NULL, 0);
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    qs_motion_close(&motion);
    qs_trace_close(&trace);
    return 0;
}
