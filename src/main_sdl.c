#include "qs_game.h"
#include "qs_trace.h"
#include "qs_motion.h"
#include "qs_parallax.h"
#include "qs_route_probe.h"
#include <SDL.h>
#include <stdio.h>
#include <float.h>
#include <limits.h>
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

static void render_bonus_score_digit(SDL_Renderer *,int,int,int,SDL_Color);

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
    /*
     * Stage 64: the blue session-best number is also available on the
     * city map selector. It survives Back/finish and does not pretend
     * that this record is an original QuackShot game feature.
     */
    if ((game->stage == 0 &&
         game->physics_profile == QS_PHYSICS_REFERENCE_CITY) ||
        qs_city_best_bonus_score(game) > 0) {
        int value=qs_city_best_bonus_score(game),i;
        for(i=3;i>=0;--i) {
            render_bonus_score_digit(renderer,278+8*i,61,value%10,
                                     color(112,202,221));
            value/=10;
        }
    }
    /*
     * Stage 67: session-best completed CITY time is shown alongside
     * the blue score. No clear record means no green digits on map.
     */
    if (qs_city_best_clear_time(game)>0) {
        int time=qs_city_best_clear_time(game),place;
        for(place=3;place>=0;--place) {
            render_bonus_score_digit(renderer,238+8*place,61,time%10,
                                     color(94,221,138));
            time/=10;
        }
    }
    /* Stage 69: session-best split is visible even without a clear.
     * Place it in the separate bottom map strip (not over stage titles). */
    if (qs_city_best_checkpoint_time(game)>0) {
        int split=qs_city_best_checkpoint_time(game),place;
        for(place=3;place>=0;--place) {
            render_bonus_score_digit(renderer,238+8*place,203,split%10,
                                     color(255,180,129));
            split/=10;
        }
    }
    /* Stage 78: lavender best checkpoint-to-finish sector, only
     * after a completed run through the checkpoint. No false zero. */
    if (qs_city_best_sector_time(game)>0) {
        int sector=qs_city_best_sector_time(game),place;
        for(place=3;place>=0;--place) {
            render_bonus_score_digit(renderer,278+8*place,203,sector%10,
                                     color(183,160,255));
            sector/=10;
        }
    }
    rectangle(renderer, 50, 215, 300, 4, color(130, 146, 167));
}

/*
 * Stage 62: tiny original-free seven-segment scoreboard. Each digit is
 * 5x7 pixels, so all four decimal positions fit to the left of the
 * existing checkpoint/bonus/weapon indicators on the 400px HUD.
 */
static void render_bonus_score_digit(SDL_Renderer *renderer,
                                     int x, int y, int digit,
                                     SDL_Color lit) {
    static const unsigned char segments[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f
    };
    unsigned int bits;
    if (digit < 0 || digit > 9) return;
    bits=segments[digit];
    if (bits & 0x01) rectangle(renderer,x+1,y,3,1,lit);
    if (bits & 0x02) rectangle(renderer,x+4,y+1,1,2,lit);
    if (bits & 0x04) rectangle(renderer,x+4,y+4,1,2,lit);
    if (bits & 0x08) rectangle(renderer,x+1,y+6,3,1,lit);
    if (bits & 0x10) rectangle(renderer,x,y+4,1,2,lit);
    if (bits & 0x20) rectangle(renderer,x,y+1,1,2,lit);
    if (bits & 0x40) rectangle(renderer,x+1,y+3,3,1,lit);
}

static void render_city_bonus_score(SDL_Renderer *renderer, const QsGame *game) {
    int score, place;
    if (game->physics_profile != QS_PHYSICS_REFERENCE_CITY ||
        game->stage != 0)
        return;
    score=qs_city_bonus_score(game);
    for (place=3;place>=0;--place) {
        int decimal=score%10;
        render_bonus_score_digit(renderer,245+8*place,4,decimal,
                                 color(255,225,108));
        score/=10;
    }
    /* Stage 63: cyan session-best display is distinct from the
     * gold current-attempt score. Both are pixel-aligned to the HUD. */
    score=qs_city_best_bonus_score(game);
    for(place=3;place>=0;--place) {
        int decimal=score%10;
        render_bonus_score_digit(renderer,205+8*place,4,decimal,
                                 color(112,202,221));
        score/=10;
    }
    rectangle(renderer,239,4,1,7,color(107,113,127));
    /* Current CITY TEST timer in ticks, starting at 0000 and
     * saturating at 9999. The personal fastest clear is shown on MAP. */
    score=qs_city_attempt_time(game);
    for(place=3;place>=0;--place) {
        int decimal=score%10;
        render_bonus_score_digit(renderer,166+8*place,4,decimal,
                                 color(94,221,138));
        score/=10;
    }
    /* First checkpoint split in the current attempt (amber). An
     * unearned flag leaves this part of the dark HUD blank. */
    score=qs_city_checkpoint_split_time(game);
    if(score>0) for(place=3;place>=0;--place) {
        int decimal=score%10;
        render_bonus_score_digit(renderer,126+8*place,4,decimal,
                                 color(255,180,129));
        score/=10;
    }
    /* Live duration of the second sector in a valid attempt.
     * A zero-length sector at checkpoint touch is intentionally
     * shown as 0000; before the checkpoint this slot is empty. */
    if (qs_city_checkpoint_split_time(game)>0) {
        score=qs_city_sector_time(game);
        for(place=3;place>=0;--place) {
            render_bonus_score_digit(renderer,48+8*place,4,score%10,
                                     color(183,160,255));
            score/=10;
        }
    }
    /*
     * Stage 79: independent second-sector budget strip under the
     * lavender 4-digit counter. It starts full green at checkpoint,
     * shrinks as elapsed sector ticks consume the completed PB,
     * turns cyan exactly on tie, and coral once slower than PB.
     * No earned completed-sector PB -> no displayed comparison.
     */
    if (qs_city_sector_budget_valid(game)) {
        int remaining=qs_city_sector_budget_frames(game);
        int best=qs_city_best_sector_time(game);
        if (remaining > 0) {
            int fill=1+(28*remaining)/best;
            rectangle(renderer,48,12,fill,2,color(94,221,138));
        } else if (remaining == 0) {
            rectangle(renderer,48,12,29,2,color(112,202,221));
        } else {
            rectangle(renderer,48,12,29,2,color(239,96,108));
        }
    }
    /* Stage 70: show signed comparison with the PREVIOUS split PB.
     * Amber is the actual split, green is quicker, coral is slower,
     * cyan is a tie. No number on the session's first-ever split. */
    if (qs_city_checkpoint_gap_valid(game)) {
        int delta=qs_city_checkpoint_gap_time(game);
        int magnitude=delta<0?-delta:delta;
        SDL_Color pace=delta<0 ? color(94,221,138) :
                      delta>0 ? color(239,96,108) :
                                color(112,202,221);
        rectangle(renderer,79,7,5,1,pace);
        if (delta>0) rectangle(renderer,81,5,1,5,pace);
        for(place=3;place>=0;--place) {
            int digit=magnitude%10;
            render_bonus_score_digit(renderer,89+8*place,4,digit,pace);
            magnitude/=10;
        }
    }
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
    /* Stage 58: visible ORIGINAL-FREE test checkpoint. The flag head
     * changes from muted blue to gold on genuine contact. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        int marker_x=(int)QS_CITY_CHECKPOINT_X-cam;
        rectangle(renderer, marker_x+5, 183, 3, 25, color(28, 38, 54));
        rectangle(renderer, marker_x, 179, 12, 5,
                  game->city_checkpoint_active
                    ? color(255, 208, 109) : color(112, 167, 221));
    }
    /* Stage 60: two optional gold beacons after the mid-city flag.
     * They disappear when collected. No original game assets used. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        if (!(game->city_bonus_mask & 1)) {
            rectangle(renderer, (int)QS_CITY_BONUS_A_X-cam, 186,
                      10, 10, color(255, 225, 108));
            rectangle(renderer, (int)QS_CITY_BONUS_A_X-cam+3, 189,
                      4, 4, color(255, 242, 190));
        }
        if (!(game->city_bonus_mask & 2)) {
            rectangle(renderer, (int)QS_CITY_BONUS_B_X-cam, 186,
                      10, 10, color(255, 225, 108));
            rectangle(renderer, (int)QS_CITY_BONUS_B_X-cam+3, 189,
                      4, 4, color(255, 242, 190));
        }
    }
    /*
     * Stage 74: short, purely visual fragments from a destroyed box.
     * Three original-free pixel shards travel outward/upward over 18
     * simulation ticks; they never affect tile or projectile physics.
     * Drawing is strictly gated to the opt-in CITY TEST.
     */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0 && game->city_crate_active == 0 &&
        game->city_crate_fx_frames > 0 &&
        game->city_crate_fx_frames <= QS_CITY_CRATE_FX_TICKS) {
        int age=QS_CITY_CRATE_FX_TICKS-game->city_crate_fx_frames;
        int x=(int)QS_CITY_CRATE_X-cam;
        int y=(int)QS_CITY_CRATE_Y;
        rectangle(renderer,x-6-age/2,y-4-age/3,3,2,
                  color(255,214,127));
        rectangle(renderer,x+18+age/2,y-2-age/4,3,2,
                  color(230,157,104));
        rectangle(renderer,x+7,y-9-age/2,2,2,
                  color(255,242,190));
    }
    /*
     * Stage 80: original-free spring pad on the flat approach to
     * CITY TEST's exit. The fixed golden body is always visible,
     * and a short blue highlight communicates a successful bounce.
     * Gameplay physics never depend on whether a frame is rendered.
     */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        int x=(int)QS_CITY_SPRING_X-cam;
        rectangle(renderer,x,(int)QS_CITY_SPRING_TOP-4,
                  (int)QS_CITY_SPRING_WIDTH,4,color(255,201,92));
        rectangle(renderer,x+4,(int)QS_CITY_SPRING_TOP-6,
                  (int)QS_CITY_SPRING_WIDTH-8,2,color(255,239,173));
        if (game->city_spring_flash_frames>0 &&
            game->city_spring_flash_frames<=QS_CITY_SPRING_FLASH_TICKS)
            rectangle(renderer,x+5,(int)QS_CITY_SPRING_TOP-10,
                      (int)QS_CITY_SPRING_WIDTH-10,2,
                      color(112,202,221));
    }
    /* Stage 84: decorative top of the one-way sky ledge. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        int x=(int)QS_CITY_SKY_LEDGE_X-cam;
        int y=(int)QS_CITY_SKY_LEDGE_TOP;
        rectangle(renderer,x,y-4,(int)QS_CITY_SKY_LEDGE_WIDTH,4,
                  color(104,177,205));
        rectangle(renderer,x+4,y-6,(int)QS_CITY_SKY_LEDGE_WIDTH-8,2,
                  color(255,239,173));
    }
    /* Stage 81: reachable spring-route heart, independent from the
     * two ground beacons and their 1000-point scoreboard. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0 && game->city_air_reward_collected == 0) {
        int x=(int)QS_CITY_AIR_REWARD_X-cam;
        int y=(int)QS_CITY_AIR_REWARD_Y;
        rectangle(renderer,x,y,(int)QS_CITY_AIR_REWARD_SIZE,
                  (int)QS_CITY_AIR_REWARD_SIZE,color(95,228,218));
        rectangle(renderer,x+3,y+3,4,4,color(255,240,174));
    }
    /* Stage 82: a temporary two-color earned burst, never collidable.
     * Position remains in world coordinates; the two camera phases
     * affect raster only, never pickup arbitration or timer state. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0 && game->city_air_reward_collected == 1 &&
        game->city_air_reward_fx_frames > 0 &&
        game->city_air_reward_fx_frames <= QS_CITY_AIR_REWARD_FX_TICKS) {
        int age=QS_CITY_AIR_REWARD_FX_TICKS-game->city_air_reward_fx_frames;
        int x=(int)QS_CITY_AIR_REWARD_X-cam;
        int y=(int)QS_CITY_AIR_REWARD_Y;
        rectangle(renderer,x-6-age/3,y+4,4,2,color(183,160,255));
        rectangle(renderer,x+12+age/3,y+4,4,2,color(255,201,92));
        rectangle(renderer,x+4,y-6-age/3,2,4,color(255,239,173));
    }
    /* Stage 72: a small original-free barricade can be shot away,
     * or bypassed by jumping. It is only present in CITY TEST. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0 && game->city_crate_active == 1) {
        int x=(int)QS_CITY_CRATE_X-cam;
        rectangle(renderer,x,(int)QS_CITY_CRATE_Y,16,16,
                  color(182,96,89));
        rectangle(renderer,x+3,(int)QS_CITY_CRATE_Y+3,10,10,
                  color(230,157,104));
        rectangle(renderer,x+7,(int)QS_CITY_CRATE_Y+3,2,10,
                  color(182,96,89));
    }
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
    /*
     * Stage 59: checkpoint status is visible even when the landmark
     * itself has scrolled off screen. This original-free cyan/gold
     * HUD tile belongs ONLY to the opt-in research CITY TEST.
     */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        rectangle(renderer, 331, 4, 14, 8,
                  game->city_checkpoint_active == 1
                    ? color(255, 208, 109) : color(112, 167, 221));
    }
    /* Stage 60: two HUD stamps represent 0/500/1000 optional points.
     * They are persistent across a retry and cleared on map reentry. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        rectangle(renderer, 299, 4, 10, 8,
                  (game->city_bonus_mask & 1)
                     ? color(255, 225, 108) : color(90, 88, 107));
        rectangle(renderer, 315, 4, 10, 8,
                  (game->city_bonus_mask & 2)
                     ? color(255, 225, 108) : color(90, 88, 107));
    }
    /* Stage 61: full bonus set marks the one-heart reward milestone.
     * The icon indicates the set was earned; it does not repeat
     * healing on subsequent frames or after later damage. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        rectangle(renderer, 281, 4, 10, 8,
                  game->city_bonus_mask == 3
                    ? color(94, 221, 138) : color(90, 88, 107));
    }
    render_city_bonus_score(renderer,game);
    /* Stage 83: persistent city-only airborne heart status.
     * Position is fixed in the HUD and never follows parallax. */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0) {
        rectangle(renderer,389,4,8,8,
                  game->city_air_reward_collected == 1
                      ? color(95,228,218) : color(90,88,107));
    }
    /*
     * Stage 76: asset-free paused-state emblem, rendered LAST so it
     * stays visible even when the camera is moving through scenery.
     * The game loop itself owns the freeze; this only draws two bars.
     */
    if (game->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
        game->stage == 0 && game->city_paused == 1) {
        rectangle(renderer,185,99,30,36,color(28,38,54));
        rectangle(renderer,192,108,5,18,color(255,225,108));
        rectangle(renderer,203,108,5,18,color(255,225,108));
    }
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
    /*
     * Stage 68: the real CITY TEST finish now also earns a completed
     * time-trial result. The expected image must include that freshly
     * earned green map record, while retaining the independently
     * constructed progress and unlock baseline.
     */
    if (game.city_best_clear_frames != 1) {
        fprintf(stderr,"PROGRESS_MAP_SDL_FAIL: expected one-tick clear, got %d\n",
                game.city_best_clear_frames);
        return 0;
    }
    expected.city_best_clear_frames = game.city_best_clear_frames;
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
    expected.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    expected.red_unlocked = 1;
    expected.weapon = QS_RED;
    expected.cleared[0] = 1;
    /*
     * The stage is already completed at tick start, so the one-tick
     * successful time trial is part of the independent map oracle.
     */
    if (simultaneous.city_best_clear_frames != 1) {
        fprintf(stderr,"FINISH_CHORD_SDL_FAIL: expected one-tick best, got %d\n",
                simultaneous.city_best_clear_frames);
        return 0;
    }
    expected.city_best_clear_frames = simultaneous.city_best_clear_frames;
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
    expected.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    expected.cleared[0] = 1;
    expected.red_unlocked = 1;
    expected.weapon = QS_RED;
    /* Completion during movement now records the first counted tick.
     * Match that EARNED map outcome, not a pre-time-trial map. */
    if (game.city_best_clear_frames != 1) {
        fprintf(stderr,"EXIT_INTERACTION_SDL_FAIL: expected one-tick best, got %d\n",
                game.city_best_clear_frames);
        return 0;
    }
    expected.city_best_clear_frames = game.city_best_clear_frames;
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
    qs_init(&baseline);
    baseline.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    baseline.cleared[2]=1;
    /* The recovered CITY TEST map still owns its reference-profile
     * zero-valued HUD; a normal-prototype map is not an equivalent
     * pixel reference even when both have identical stage clears. */
    if (!capture_frame(renderer,&baseline,&map_before)) return 0;
    in.back=in.select=1;
    /* Stage 73: keep this corrupted-scene test on the plain Start
     * (map-exit) path. R+Start now intentionally starts a fresh run. */
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

/* Stage 50: numeric retry must leave a real spawn collectible visible
 * for one frame, just like fatal/pit retries. The next simulation tick
 * may collect it. Also verify the HUD after same-frame nonfatal damage
 * and a legal heart pickup. All objects are synthetic placeholders. */
static int run_frame_arbitration_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g, n;
    QsInput in={0};
    uint32_t old_red=0, next_red=0, healed_heart=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=NAN;
    g.pickups[0].active=1;
    g.pickups[0].x=36;g.pickups[0].y=189;
    g.pickups[0].kind=QS_PICKUP_RED;
    qs_step(&g,in);
    if (g.hero.x!=40 || g.hero.hurt_frames!=89 ||
        !g.pickups[0].active || g.red_unlocked ||
        !retry_spawn_pixel(renderer,&g,&old_red) ||
        old_red!=UINT32_C(0xfff44f50)) {
        fprintf(stderr,"FRAME_ARBITRATION_SDL_FAIL: numeric spawn red=%08x\n",
                (unsigned)old_red);
        return 0;
    }
    qs_step(&g,in);
    if (g.pickups[0].active || !g.red_unlocked ||
        !retry_spawn_pixel(renderer,&g,&next_red) ||
        next_red==old_red) {
        fputs("FRAME_ARBITRATION_SDL_FAIL: next tick did not award red\n",stderr);
        return 0;
    }

    qs_init(&n);
    n.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&n,0);
    memset(n.enemies,0,sizeof(n.enemies));
    memset(n.pickups,0,sizeof(n.pickups));
    n.hero.x=100;n.hero.y=188;n.hero.hearts=2;
    n.enemies[0].active=1;
    n.enemies[0].x=100;n.enemies[0].y=188;
    n.enemies[0].min_x=n.enemies[0].max_x=100;
    n.enemies[0].dir=1;
    n.pickups[0].active=1;
    n.pickups[0].x=100;n.pickups[0].y=189;
    n.pickups[0].kind=QS_PICKUP_HEART;
    qs_step(&n,in);
    if (n.hero.hearts!=2 || n.hero.hurt_frames!=69 ||
        n.pickups[0].active ||
        !read_immunity_heart_pixel(renderer,&n,&healed_heart) ||
        healed_heart!=UINT32_C(0xff5a586b)) {
        fprintf(stderr,"FRAME_ARBITRATION_SDL_FAIL: heal=%08x hearts=%d\n",
                (unsigned)healed_heart,n.hero.hearts);
        return 0;
    }
    printf("FRAME_ARBITRATION_SDL_PASS: deferred red=%08x next=%08x heal=%08x\n",
           (unsigned)old_red,(unsigned)next_red,(unsigned)healed_heart);
    return 1;
}

/* Stage 51: real SDL HUD confirms that clamping INT_MAX protection
 * still preserves the last safe tick and exposes the hero on the NEXT
 * one. The bounded enemy stun remains visible as a blue placeholder. */
static int run_timer_boundary_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput idle={0};
    uint32_t safe=0, expired=0, stunned=0;
    int i;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;g.hero.grounded=1;
    g.hero.hurt_frames=INT_MAX;
    g.enemies[0].active=1;
    g.enemies[0].x=100;g.enemies[0].y=188;
    g.enemies[0].min_x=g.enemies[0].max_x=100;
    g.enemies[0].dir=1;
    g.enemies[1].active=1;
    g.enemies[1].x=180;g.enemies[1].y=184;
    g.enemies[1].min_x=g.enemies[1].max_x=180;
    g.enemies[1].dir=1;
    g.enemies[1].stunned_frames=INT_MAX;
    qs_step(&g,idle);
    if (g.hero.hearts!=3 || g.hero.hurt_frames!=89 ||
        g.enemies[1].stunned_frames!=179 ||
        !read_immunity_heart_pixel(renderer,&g,&safe) ||
        !read_multishot_pixel(renderer,&g,181,186,&stunned) ||
        safe!=UINT32_C(0xffef606c) ||
        stunned!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"TIMER_BOUNDARY_SDL_FAIL: safe=%08x stun=%08x frames=%d\n",
                (unsigned)safe,(unsigned)stunned,g.hero.hurt_frames);
        return 0;
    }
    for(i=0;i<89;i++) {
        qs_step(&g,idle);
        if(g.hero.hearts!=3) return 0;
    }
    if(g.hero.hurt_frames!=0) return 0;
    qs_step(&g,idle);
    if(g.hero.hearts!=2 || g.hero.hurt_frames!=69 ||
       !read_immunity_heart_pixel(renderer,&g,&expired) ||
       expired!=UINT32_C(0xff5a586b)) {
        fprintf(stderr,"TIMER_BOUNDARY_SDL_FAIL: exposed=%08x hearts=%d\n",
                (unsigned)expired,g.hero.hearts);
        return 0;
    }
    printf("TIMER_BOUNDARY_SDL_PASS: protected=%08x exposed=%08x stunned=%08x\n",
           (unsigned)safe,(unsigned)expired,(unsigned)stunned);
    return 1;
}

/*
 * Stage 52: corrupt hearts and facing are recovered before either the
 * collision pipeline or SDL rasterization. The old frame must retain
 * its spawn pickup; the following tick may collect it and dispatch a
 * bounded fresh shot. HUD and item pixels are read from real SDL2.
 */
static int run_hero_status_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput in={0};
    uint32_t item_before=0,item_after=0,heart=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.hearts=INT_MIN;
    g.hero.facing=INT_MIN;
    g.hero.grounded=INT_MAX;
    g.reference_jump_steps=INT_MAX;
    g.pickups[0].active=1;
    g.pickups[0].x=36;g.pickups[0].y=189;
    g.pickups[0].kind=QS_PICKUP_RED;
    in.fire=1;
    qs_step(&g,in);
    if (g.mode!=QS_STAGE || g.hero.hearts!=3 ||
        g.hero.hurt_frames!=89 || g.hero.x!=40 ||
        g.hero.facing!=1 || g.reference_jump_steps!=0 ||
        !g.pickups[0].active || g.red_unlocked ||
        !g.pending_reference_shot ||
        !retry_spawn_pixel(renderer,&g,&item_before) ||
        !read_immunity_heart_pixel(renderer,&g,&heart) ||
        item_before!=UINT32_C(0xfff44f50) ||
        heart!=UINT32_C(0xffef606c)) {
        fprintf(stderr,"HERO_STATUS_SDL_FAIL: retry item=%08x heart=%08x\n",
                (unsigned)item_before,(unsigned)heart);
        return 0;
    }
    memset(&in,0,sizeof(in));
    qs_step(&g,in);
    if (g.pickups[0].active || !g.red_unlocked ||
        g.hero.hearts!=3 || g.hero.hurt_frames!=88 ||
        !g.shots[0].active || g.shots[0].vx!=5.5f ||
        !read_multishot_pixel(renderer,&g,41,191,&heart) ||
        !retry_spawn_pixel(renderer,&g,&item_after) ||
        item_before==item_after) {
        fprintf(stderr,"HERO_STATUS_SDL_FAIL: reentry item=%08x shot=%d\n",
                (unsigned)item_after,g.shots[0].active);
        return 0;
    }
    printf("HERO_STATUS_SDL_PASS: delayed item=%08x next=%08x shot_vx=%.1f\n",
           (unsigned)item_before,(unsigned)item_after,(double)g.shots[0].vx);
    return 1;
}

/*
 * Stage 53: a NaN attached red anchor must be eliminated BEFORE the
 * hero's vertical collision step. Compare real SDL pixels against a
 * matching scene with the same healthy support but no damaged slot.
 * The player must land on the healthy support with no NaN coordinates.
 */
static int run_pre_move_shots_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,baseline;
    QsInput in={0};
    uint64_t damaged_frame=0,clean_frame=0;
    uint32_t hero_pixel=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=139.92f;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=0;
    g.shots[1].active=g.shots[1].attached=g.shots[1].red=1;
    g.shots[1].x=100;g.shots[1].y=160.02f;g.shots[1].vx=0;
    g.shots[1].frames=40;
    baseline=g;
    g.shots[0].active=g.shots[0].attached=g.shots[0].red=1;
    g.shots[0].x=100;g.shots[0].y=NAN;
    g.shots[0].frames=40;
    qs_step(&g,in);
    qs_step(&baseline,in);
    if (g.shots[0].active || g.shots[0].attached ||
        !g.shots[1].active || !g.hero.grounded ||
        !isfinite(g.hero.x) || !isfinite(g.hero.y) ||
        fabsf(g.hero.y-140.02f)>0.0001f ||
        g.hero.y!=baseline.hero.y || g.camera_x!=baseline.camera_x ||
        !capture_frame(renderer,&g,&damaged_frame) ||
        !capture_frame(renderer,&baseline,&clean_frame) ||
        damaged_frame!=clean_frame ||
        !read_multishot_pixel(renderer,&g,101,142,&hero_pixel) ||
        hero_pixel!=UINT32_C(0xfffaeda7)) {
        fprintf(stderr,"PRE_MOVE_SHOTS_SDL_FAIL: y=%.4f clean=%.4f frames=%016llx/%016llx px=%08x\n",
                (double)g.hero.y,(double)baseline.hero.y,
                (unsigned long long)damaged_frame,
                (unsigned long long)clean_frame,(unsigned)hero_pixel);
        return 0;
    }
    printf("PRE_MOVE_SHOTS_SDL_PASS: healthy landing %.4f hash=%016llx hero=%08x\n",
           (double)g.hero.y,(unsigned long long)damaged_frame,
           (unsigned)hero_pixel);
    return 1;
}

/*
 * Stage 54: validate the real HUD and the one-tick ammo latch.
 * Bad weapon/unlock/queue values cannot create an immediate red shot;
 * a valid RED fire press remains red if the weapon is switched to
 * yellow during the following (dispatch) frame.
 */
static int run_weapon_queue_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput in={0};
    uint32_t safe_yellow=0,armed_red=0,switched_yellow=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.red_unlocked=INT_MIN;
    g.weapon=QS_RED;
    g.pending_reference_shot=INT_MAX;
    g.pending_reference_red=INT_MAX;
    qs_step(&g,in);
    if (g.red_unlocked || g.weapon!=QS_YELLOW ||
        g.pending_reference_shot || g.pending_reference_red ||
        g.shots[0].active ||
        !read_multishot_pixel(renderer,&g,360,6,&safe_yellow) ||
        safe_yellow!=UINT32_C(0xffe9cf6e)) {
        fprintf(stderr,"WEAPON_QUEUE_SDL_FAIL: damaged HUD=%08x\n",
                (unsigned)safe_yellow);
        return 0;
    }
    g.red_unlocked=INT_MAX;g.weapon=QS_RED;
    in.fire=1;
    qs_step(&g,in);
    if(g.red_unlocked!=1 || g.weapon!=QS_RED ||
       g.pending_reference_shot!=1 || g.pending_reference_red!=1 ||
       g.shots[0].active ||
       !read_multishot_pixel(renderer,&g,360,6,&armed_red) ||
       armed_red!=UINT32_C(0xffe04946)) {
        fprintf(stderr,"WEAPON_QUEUE_SDL_FAIL: red HUD=%08x\n",
                (unsigned)armed_red);
        return 0;
    }
    memset(&in,0,sizeof(in));
    in.change=1;
    qs_step(&g,in);
    if(g.weapon!=QS_YELLOW || g.pending_reference_shot ||
       !g.shots[0].active || !g.shots[0].red ||
       g.shots[0].vx!=5.5f ||
       !read_multishot_pixel(renderer,&g,360,6,&switched_yellow) ||
       switched_yellow!=UINT32_C(0xffe9cf6e)) {
        fprintf(stderr,"WEAPON_QUEUE_SDL_FAIL: switched=%08x shot=%d red=%d\n",
                (unsigned)switched_yellow,g.shots[0].active,g.shots[0].red);
        return 0;
    }
    printf("WEAPON_QUEUE_SDL_PASS: safe=%08x armed=%08x switched=%08x latched_red=%d\n",
           (unsigned)safe_yellow,(unsigned)armed_red,
           (unsigned)switched_yellow,g.shots[0].red);
    return 1;
}

/*
 * Stage 55: after a real CITY TEST -> MAP handoff, only an exactly
 * committed completion flag should paint a green progress indicator.
 * Repair an invalid selected row BEFORE directional arithmetic.
 * Compare complete SDL map raster with a canonical clean reference.
 */
static int run_city_map_integrity_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,reference;
    QsInput in={0};
    uint32_t progress[QS_STAGE_COUNT]={0};
    uint64_t fixed_hash=0,expected_hash=0;
    const uint32_t green=UINT32_C(0xff5edd8a);
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    in.back=1;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || g.physics_profile!=QS_PHYSICS_REFERENCE_CITY)
        return 0;
    memset(&in,0,sizeof(in));
    qs_step(&g,in);
    g.cleared[0]=1;
    reference=g;
    g.selected_stage=INT_MIN;
    g.cleared[1]=INT_MAX;
    g.cleared[2]=-1;
    qs_step(&g,in);
    if(g.selected_stage!=0 ||
       g.cleared[0]!=1 || g.cleared[1] || g.cleared[2] ||
       !capture_frame(renderer,&g,&fixed_hash) ||
       !capture_frame(renderer,&reference,&expected_hash) ||
       fixed_hash!=expected_hash ||
       !read_map_progress_pixels(renderer,&g,progress) ||
       progress[0]!=green || progress[1]==green || progress[2]==green ||
       !map_selection_pixels(renderer,&g,0)) {
        fprintf(stderr,"MAP_INTEGRITY_SDL_FAIL: fixed=%016llx clean=%016llx\n",
                (unsigned long long)fixed_hash,
                (unsigned long long)expected_hash);
        return 0;
    }
    /* Corrupt MAX_INT selection, then press direction + confirmation:
     * the map must move one row and never start a stage on that tick. */
    g.selected_stage=INT_MAX;
    in.right=in.select=in.dash=1;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || g.selected_stage!=1 ||
       !map_selection_pixels(renderer,&g,1)) {
        fputs("MAP_INTEGRITY_SDL_FAIL: chord navigated incorrectly\n",stderr);
        return 0;
    }
    in=(QsInput){0};
    qs_step(&g,in);
    in.select=1;
    qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.stage!=1 ||
       g.physics_profile!=QS_PHYSICS_PROTOTYPE ||
       g.cleared[0]!=1 || g.cleared[1]!=0 || g.cleared[2]!=0) {
        fputs("MAP_INTEGRITY_SDL_FAIL: normal reentry mismatch\n",stderr);
        return 0;
    }
    printf("MAP_INTEGRITY_SDL_PASS: fixed=%016llx green=%08x safe_reentry=%d\n",
           (unsigned long long)fixed_hash,(unsigned)progress[0],g.stage);
    return 1;
}

/*
 * Stage 56: invalid CITY TEST scene metadata must make a clean map
 * frame even if the prior screen was over the finish boundary.
 * No phantom completion or delayed projectile may survive. Compare
 * the exact SDL raster against a known canonical map.
 */
static int run_scene_recovery_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,reference;
    QsInput in={0};
    uint64_t repaired=0,expected=0,stage_frame=0;
    uint32_t progress[QS_STAGE_COUNT]={0};
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.mode=(QsMode)INT_MAX;
    g.stage=INT_MIN;
    g.selected_stage=INT_MAX;
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+3;
    g.camera_x=700;g.render_camera_x=690;
    g.cleared[0]=1;g.cleared[1]=INT_MAX;g.cleared[2]=-1;
    g.pending_reference_shot=g.pending_reference_red=1;
    g.shots[0].active=g.shots[0].attached=g.shots[0].red=1;
    g.shots[0].frames=30;g.shots[0].x=100;g.shots[0].y=160;
    in.select=in.dash=1;
    qs_step(&g,in);
    qs_init(&reference);
    reference.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    reference.cleared[0]=1;
    /* Corruption recovers to the opt-in city map, not the ordinary
     * prototype selector; preserve the identical zero-valued HUD. */
    if(g.mode!=QS_MAP || g.stage!=0 || g.selected_stage!=0 ||
       g.cleared[0]!=1 || g.cleared[1] || g.cleared[2] ||
       g.camera_x!=0 || g.render_camera_x!=0 ||
       g.pending_reference_shot || g.shots[0].active ||
       !capture_frame(renderer,&g,&repaired) ||
       !capture_frame(renderer,&reference,&expected) ||
       repaired!=expected ||
       !read_map_progress_pixels(renderer,&g,progress) ||
       progress[0]!=UINT32_C(0xff5edd8a) ||
       progress[1]==UINT32_C(0xff5edd8a) ||
       progress[2]==UINT32_C(0xff5edd8a)) {
        fprintf(stderr,"SCENE_RECOVERY_SDL_FAIL: restored=%016llx expected=%016llx\n",
                (unsigned long long)repaired,(unsigned long long)expected);
        return 0;
    }
    qs_step(&g,in);
    if(g.mode!=QS_MAP) {
        fputs("SCENE_RECOVERY_SDL_FAIL: held Select relaunched immediately\n",stderr);
        return 0;
    }
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.stage!=0 ||
       g.physics_profile!=QS_PHYSICS_REFERENCE_CITY ||
       g.hero.x!=40 || g.hero.y!=188 ||
       g.camera_x!=0 || g.render_camera_x!=0 ||
       g.pending_reference_shot || g.shots[0].active ||
       !capture_frame(renderer,&g,&stage_frame) ||
       stage_frame==repaired) {
        fputs("SCENE_RECOVERY_SDL_FAIL: valid fresh reentry unavailable\n",stderr);
        return 0;
    }
    printf("SCENE_RECOVERY_SDL_PASS: map=%016llx fresh=%016llx progress=%08x\n",
           (unsigned long long)repaired,
           (unsigned long long)stage_frame,(unsigned)progress[0]);
    return 1;
}

/*
 * Stage 57: two corrupted projectile snapshots (NaN / Infinity)
 * must collapse to the same finite retired metadata. Real SDL frames
 * must match a clean scene exactly, with a healthy support retained.
 */
static int run_shot_canonical_sdl_smoke(SDL_Renderer *renderer) {
    QsGame a,b,clean;
    QsInput idle={0};
    uint64_t a_hash=0,b_hash=0,clean_hash=0;
    uint32_t hero_pixel=0;
    qs_init(&clean);
    clean.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&clean,0);
    memset(clean.enemies,0,sizeof(clean.enemies));
    memset(clean.pickups,0,sizeof(clean.pickups));
    memset(clean.shots,0,sizeof(clean.shots));
    clean.hero.x=100;clean.hero.y=139.92f;
    clean.hero.vx=clean.hero.vy=0;clean.hero.grounded=0;
    clean.shots[1].active=clean.shots[1].attached=clean.shots[1].red=1;
    clean.shots[1].x=100;clean.shots[1].y=160.02f;
    clean.shots[1].frames=40;
    a=b=clean;
    a.shots[0].active=a.shots[0].attached=a.shots[0].red=1;
    a.shots[0].x=NAN;a.shots[0].y=160;a.shots[0].frames=40;
    b.shots[0].active=b.shots[0].attached=1;
    b.shots[0].red=INT_MAX;
    b.shots[0].x=100;b.shots[0].y=INFINITY;
    b.shots[0].vx=INFINITY;b.shots[0].frames=40;
    qs_step(&a,idle);qs_step(&b,idle);qs_step(&clean,idle);
    if(a.shots[0].active || b.shots[0].active ||
       a.shots[0].attached || b.shots[0].attached ||
       a.shots[0].frames!=40 || b.shots[0].frames!=40 ||
       a.shots[0].x!=0 || a.shots[0].y!=0 || a.shots[0].vx!=0 ||
       b.shots[0].x!=0 || b.shots[0].y!=0 || b.shots[0].vx!=0 ||
       !a.hero.grounded || !b.hero.grounded ||
       a.hero.y!=clean.hero.y || b.hero.y!=clean.hero.y ||
       !capture_frame(renderer,&a,&a_hash) ||
       !capture_frame(renderer,&b,&b_hash) ||
       !capture_frame(renderer,&clean,&clean_hash) ||
       a_hash!=clean_hash || b_hash!=clean_hash ||
       !read_multishot_pixel(renderer,&a,101,142,&hero_pixel) ||
       hero_pixel!=UINT32_C(0xfffaeda7)) {
        fprintf(stderr,"SHOT_CANONICAL_SDL_FAIL: A=%016llx B=%016llx C=%016llx px=%08x\n",
                (unsigned long long)a_hash,
                (unsigned long long)b_hash,
                (unsigned long long)clean_hash,(unsigned)hero_pixel);
        return 0;
    }
    printf("SHOT_CANONICAL_SDL_PASS: matching=%016llx finite_idle_slot=0 hero=%08x\n",
           (unsigned long long)clean_hash,(unsigned)hero_pixel);
    return 1;
}

/*
 * Stage 58: an original-free visible flag turns from muted blue to
 * gold on geometric contact. A lethal retry returns to x=640 with a
 * synchronized camera; map exit and a fresh stage reset the marker.
 * Sample the real SDL ARGB8888 raster, not an offline image substitute.
 */
static int run_city_checkpoint_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput in={0};
    uint32_t inactive=0,active=0,after_retry=0,after_reentry=0,hero=0;
    int flag_px;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    g.hero.y=QS_CITY_CHECKPOINT_Y;
    g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    g.camera_x=QS_CITY_CHECKPOINT_X-141;
    g.render_camera_x=g.camera_x;
    flag_px=(int)QS_CITY_CHECKPOINT_X-(int)g.render_camera_x+6;
    if (g.city_checkpoint_active ||
        !read_multishot_pixel(renderer,&g,flag_px,181,&inactive) ||
        inactive!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"CITY_CHECKPOINT_SDL_FAIL: inactive=%08x\n",
                (unsigned)inactive);
        return 0;
    }
    qs_step(&g,in);
    flag_px=(int)QS_CITY_CHECKPOINT_X-(int)g.render_camera_x+6;
    if (!g.city_checkpoint_active ||
        !read_multishot_pixel(renderer,&g,flag_px,181,&active) ||
        active!=UINT32_C(0xffffd06d)) {
        fprintf(stderr,"CITY_CHECKPOINT_SDL_FAIL: activated=%08x\n",
                (unsigned)active);
        return 0;
    }
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.hero.x;g.enemies[0].y=188;
    g.enemies[0].min_x=g.enemies[0].max_x=g.hero.x;
    g.enemies[0].dir=1;
    qs_step(&g,in);
    flag_px=(int)QS_CITY_CHECKPOINT_X-(int)g.render_camera_x+6;
    if (!g.city_checkpoint_active ||
        g.hero.x!=QS_CITY_CHECKPOINT_X ||
        g.hero.hearts!=3 || g.hero.hurt_frames!=89 ||
        g.camera_x!=QS_CITY_CHECKPOINT_X-141 ||
        g.render_camera_x!=g.camera_x ||
        !read_multishot_pixel(renderer,&g,flag_px,181,&after_retry) ||
        after_retry!=active ||
        !read_multishot_pixel(renderer,&g,flag_px-4,191,&hero) ||
        hero!=UINT32_C(0xfffaeda7)) {
        fprintf(stderr,"CITY_CHECKPOINT_SDL_FAIL: retry=%08x hero=%08x\n",
                (unsigned)after_retry,(unsigned)hero);
        return 0;
    }
    in.back=1;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || g.city_checkpoint_active) {
        fputs("CITY_CHECKPOINT_SDL_FAIL: map retained checkpoint\n",stderr);
        return 0;
    }
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.stage!=0 ||
       g.physics_profile!=QS_PHYSICS_REFERENCE_CITY ||
       g.city_checkpoint_active || g.hero.x!=40) {
        fputs("CITY_CHECKPOINT_SDL_FAIL: new attempt retained checkpoint\n",stderr);
        return 0;
    }
    g.camera_x=QS_CITY_CHECKPOINT_X-141;
    g.render_camera_x=g.camera_x;
    flag_px=(int)QS_CITY_CHECKPOINT_X-(int)g.render_camera_x+6;
    if(!read_multishot_pixel(renderer,&g,flag_px,181,&after_reentry) ||
       after_reentry!=inactive) {
        fprintf(stderr,"CITY_CHECKPOINT_SDL_FAIL: new marker=%08x\n",
                (unsigned)after_reentry);
        return 0;
    }
    printf("CITY_CHECKPOINT_SDL_PASS: muted=%08x active=%08x respawn=%08x reset=%08x\n",
           (unsigned)inactive,(unsigned)active,
           (unsigned)after_retry,(unsigned)after_reentry);
    return 1;
}

/*
 * Stage 59: sample the persistent checkpoint HUD tile in real SDL2.
 * A successful mid-level contact changes cyan to gold, and gold stays
 * visible after the world-space flag scrolls off screen and after a
 * fatal retry. A fresh city attempt returns cyan; legacy prototype
 * never renders this UI tile, even with a damaged checkpoint flag.
 */
static int run_checkpoint_hud_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    uint32_t blue=0,gold=0,offscreen=0,retry=0,reset=0,legacy_pixel=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    if (g.city_checkpoint_active ||
        !read_multishot_pixel(renderer,&g,335,8,&blue) ||
        blue!=UINT32_C(0xff70a7dd)) {
        fprintf(stderr,"CHECKPOINT_HUD_SDL_FAIL: initial=%08x\n",
                (unsigned)blue);
        return 0;
    }
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    g.camera_x=QS_CITY_CHECKPOINT_X-141;
    g.render_camera_x=g.camera_x;
    qs_step(&g,in);
    if (!g.city_checkpoint_active ||
        !read_multishot_pixel(renderer,&g,335,8,&gold) ||
        gold!=UINT32_C(0xffffd06d)) {
        fprintf(stderr,"CHECKPOINT_HUD_SDL_FAIL: activation=%08x\n",
                (unsigned)gold);
        return 0;
    }
    /* The landmark is outside the left edge, while the HUD remains. */
    g.hero.x=900;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.camera_x=759;g.render_camera_x=759;
    qs_step(&g,in);
    if (!g.city_checkpoint_active ||
        g.render_camera_x <= QS_CITY_CHECKPOINT_X ||
        !read_multishot_pixel(renderer,&g,335,8,&offscreen) ||
        offscreen!=gold) {
        fprintf(stderr,"CHECKPOINT_HUD_SDL_FAIL: offscreen=%08x\n",
                (unsigned)offscreen);
        return 0;
    }
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=g.hero.x;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    qs_step(&g,in);
    if (g.hero.x!=QS_CITY_CHECKPOINT_X ||
        g.hero.hearts!=3 || g.hero.hurt_frames!=89 ||
        g.render_camera_x!=QS_CITY_CHECKPOINT_X-141 ||
        !read_multishot_pixel(renderer,&g,335,8,&retry) ||
        retry!=gold) {
        fprintf(stderr,"CHECKPOINT_HUD_SDL_FAIL: retry=%08x\n",
                (unsigned)retry);
        return 0;
    }
    in.back=1;qs_step(&g,in);
    if (g.mode!=QS_MAP || g.city_checkpoint_active) {
        fputs("CHECKPOINT_HUD_SDL_FAIL: checkpoint survived Back\n",stderr);
        return 0;
    }
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if (g.mode!=QS_STAGE ||
        g.physics_profile!=QS_PHYSICS_REFERENCE_CITY ||
        g.city_checkpoint_active ||
        !read_multishot_pixel(renderer,&g,335,8,&reset) ||
        reset!=blue) {
        fprintf(stderr,"CHECKPOINT_HUD_SDL_FAIL: reentry=%08x\n",
                (unsigned)reset);
        return 0;
    }
    qs_init(&legacy);
    qs_begin_stage(&legacy,0);
    legacy.city_checkpoint_active=1; /* should have no UI effect */
    if (!read_multishot_pixel(renderer,&legacy,335,8,&legacy_pixel) ||
        legacy_pixel!=UINT32_C(0xff1c2636)) {
        fprintf(stderr,"CHECKPOINT_HUD_SDL_FAIL: legacy=%08x\n",
                (unsigned)legacy_pixel);
        return 0;
    }
    printf("CHECKPOINT_HUD_SDL_PASS: blue=%08x gold=%08x offscreen=%08x retry=%08x reset=%08x legacy=%08x\n",
           (unsigned)blue,(unsigned)gold,(unsigned)offscreen,
           (unsigned)retry,(unsigned)reset,(unsigned)legacy_pixel);
    return 1;
}

/*
 * Stage 60: test real SDL pixels for two collectible city beacons and
 * their persistent 500-point HUD stamps. Gold world beacons disappear
 * on contact, but their stamps survive a checkpoint respawn. Starting
 * another attempt resets both. No original copyrighted sprites used.
 */
static int run_bonus_beacons_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    uint32_t grey_a=0,grey_b=0,token_before=0;
    uint32_t earned_a=0,still_grey_b=0,empty_a=0,token_b=0;
    uint32_t earned_b=0,empty_b=0,retry_a=0,retry_b=0;
    uint32_t reset_a=0,reset_b=0,legacy_ui=0;
    int ax,bx;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.camera_x=759;g.render_camera_x=759;
    ax=(int)QS_CITY_BONUS_A_X-(int)g.render_camera_x+3;
    bx=(int)QS_CITY_BONUS_B_X-(int)g.render_camera_x+3;
    if(g.city_bonus_mask ||
       !read_multishot_pixel(renderer,&g,303,8,&grey_a) ||
       !read_multishot_pixel(renderer,&g,319,8,&grey_b) ||
       grey_a!=UINT32_C(0xff5a586b) || grey_b!=grey_a ||
       !read_multishot_pixel(renderer,&g,ax,186,&token_before) ||
       token_before!=UINT32_C(0xffffe16c)) {
        fprintf(stderr,"BONUS_BEACONS_SDL_FAIL: grey=%08x/%08x token=%08x\n",
                (unsigned)grey_a,(unsigned)grey_b,(unsigned)token_before);
        return 0;
    }
    /* Earn checkpoint normally so the lethal retry is a real one. */
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    g.hero.grounded=1;
    qs_step(&g,in);
    if (!g.city_checkpoint_active || g.city_bonus_mask) return 0;
    /* Collect A while its world sprite is visibly in this viewport. */
    g.hero.x=QS_CITY_BONUS_A_X-5;
    g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    g.camera_x=759;g.render_camera_x=759;
    qs_step(&g,in);
    ax=(int)QS_CITY_BONUS_A_X-(int)g.render_camera_x+3;
    bx=(int)QS_CITY_BONUS_B_X-(int)g.render_camera_x+3;
    if(g.city_bonus_mask!=1 ||
       !read_multishot_pixel(renderer,&g,303,8,&earned_a) ||
       !read_multishot_pixel(renderer,&g,319,8,&still_grey_b) ||
       earned_a!=UINT32_C(0xffffe16c) || still_grey_b!=grey_b ||
       !read_multishot_pixel(renderer,&g,ax,186,&empty_a) ||
       empty_a!=UINT32_C(0xff85bad4) ||
       !read_multishot_pixel(renderer,&g,bx,186,&token_b) ||
       token_b!=token_before) {
        fprintf(stderr,"BONUS_BEACONS_SDL_FAIL: A=%08x B=%08x empty=%08x Btoken=%08x\n",
                (unsigned)earned_a,(unsigned)still_grey_b,
                (unsigned)empty_a,(unsigned)token_b);
        return 0;
    }
    g.hero.x=QS_CITY_BONUS_B_X-5;
    g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    bx=(int)QS_CITY_BONUS_B_X-(int)g.render_camera_x+3;
    if(g.city_bonus_mask!=3 ||
       !read_multishot_pixel(renderer,&g,319,8,&earned_b) ||
       earned_b!=earned_a ||
       !read_multishot_pixel(renderer,&g,bx,186,&empty_b) ||
       empty_b!=empty_a) {
        fprintf(stderr,"BONUS_BEACONS_SDL_FAIL: B=%08x empty=%08x\n",
                (unsigned)earned_b,(unsigned)empty_b);
        return 0;
    }
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=g.hero.x;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    qs_step(&g,in);
    if(g.hero.x!=QS_CITY_CHECKPOINT_X ||
       g.hero.hearts!=3 || g.hero.hurt_frames!=89 ||
       g.city_bonus_mask!=3 ||
       !read_multishot_pixel(renderer,&g,303,8,&retry_a) ||
       !read_multishot_pixel(renderer,&g,319,8,&retry_b) ||
       retry_a!=earned_a || retry_b!=earned_b) {
        fprintf(stderr,"BONUS_BEACONS_SDL_FAIL: retry=%08x/%08x mask=%d\n",
                (unsigned)retry_a,(unsigned)retry_b,g.city_bonus_mask);
        return 0;
    }
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || g.city_bonus_mask) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_bonus_mask ||
       !read_multishot_pixel(renderer,&g,303,8,&reset_a) ||
       !read_multishot_pixel(renderer,&g,319,8,&reset_b) ||
       reset_a!=grey_a || reset_b!=grey_b) return 0;
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    if(!read_multishot_pixel(renderer,&legacy,303,8,&legacy_ui) ||
       legacy_ui!=UINT32_C(0xff1c2636)) return 0;
    printf("BONUS_BEACONS_SDL_PASS: token=%08x earned=%08x/%08x retry=%08x/%08x reset=%08x/%08x\n",
           (unsigned)token_before,(unsigned)earned_a,(unsigned)earned_b,
           (unsigned)retry_a,(unsigned)retry_b,
           (unsigned)reset_a,(unsigned)reset_b);
    return 1;
}

/*
 * Stage 61: real SDL proof of the bonus-set reward. A single beacon
 * changes its 500-point stamp only; collecting the second heals one
 * missing heart and lights the green set-completion tile. Replaying
 * contact cannot heal again. The prototype never draws this tile.
 */
static int run_bonus_set_heal_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,prototype;
    QsInput in={0};
    uint32_t pending=0,partial=0,complete=0,heart_before=0;
    uint32_t heart_after=0,no_repeat=0,reset=0,legacy=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=QS_CITY_BONUS_A_X-5;
    g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    g.hero.hearts=2;
    if (!read_multishot_pixel(renderer,&g,285,8,&pending) ||
        !read_multishot_pixel(renderer,&g,39,8,&heart_before) ||
        pending!=UINT32_C(0xff5a586b) ||
        heart_before!=UINT32_C(0xff5a586b)) {
        fprintf(stderr,"BONUS_SET_HEAL_SDL_FAIL: initial=%08x heart=%08x\n",
                (unsigned)pending,(unsigned)heart_before);
        return 0;
    }
    qs_step(&g,in);
    if(g.city_bonus_mask!=1 || g.hero.hearts!=2 ||
       !read_multishot_pixel(renderer,&g,285,8,&partial) ||
       partial!=pending) {
        fprintf(stderr,"BONUS_SET_HEAL_SDL_FAIL: first stamp=%08x hearts=%d\n",
                (unsigned)partial,g.hero.hearts);
        return 0;
    }
    g.hero.x=QS_CITY_BONUS_B_X-5;
    g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.city_bonus_mask!=3 || g.hero.hearts!=3 ||
       !read_multishot_pixel(renderer,&g,285,8,&complete) ||
       !read_multishot_pixel(renderer,&g,39,8,&heart_after) ||
       complete!=UINT32_C(0xff5edd8a) ||
       heart_after!=UINT32_C(0xffef606c)) {
        fprintf(stderr,"BONUS_SET_HEAL_SDL_FAIL: set=%08x healed=%08x mask=%d hearts=%d\n",
                (unsigned)complete,(unsigned)heart_after,
                g.city_bonus_mask,g.hero.hearts);
        return 0;
    }
    /* The green marker stays earned; the reward itself does not
     * repeat if another life later loses a heart. */
    g.hero.hearts=2;
    g.hero.x=QS_CITY_BONUS_A_X-5;
    g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    g.hero.x=QS_CITY_BONUS_B_X-5;
    g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.hero.hearts!=2 ||
       !read_multishot_pixel(renderer,&g,285,8,&no_repeat) ||
       no_repeat!=complete) {
        fprintf(stderr,"BONUS_SET_HEAL_SDL_FAIL: duplicate reward=%08x hearts=%d\n",
                (unsigned)no_repeat,g.hero.hearts);
        return 0;
    }
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || g.city_bonus_mask!=0) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_bonus_mask!=0 ||
       !read_multishot_pixel(renderer,&g,285,8,&reset) ||
       reset!=pending) {
        fprintf(stderr,"BONUS_SET_HEAL_SDL_FAIL: fresh=%08x\n",
                (unsigned)reset);
        return 0;
    }
    qs_init(&prototype);qs_begin_stage(&prototype,0);
    prototype.city_bonus_mask=3;
    if(!read_multishot_pixel(renderer,&prototype,285,8,&legacy) ||
       legacy!=UINT32_C(0xff1c2636)) {
        fprintf(stderr,"BONUS_SET_HEAL_SDL_FAIL: prototype=%08x\n",
                (unsigned)legacy);
        return 0;
    }
    printf("BONUS_SET_HEAL_SDL_PASS: partial=%08x complete=%08x healed=%08x retry_stamp=%08x reset=%08x\n",
           (unsigned)partial,(unsigned)complete,(unsigned)heart_after,
           (unsigned)no_repeat,(unsigned)reset);
    return 1;
}

/*
 * Stage 62: read the FULL 29x7 scoreboard raster against an independent
 * hand-written glyph oracle, not against the drawing function's masks.
 * This detects wrong digits, misplaced segments, overdraw and gaps.
 * The normal prototype must leave this entire HUD region untouched.
 */
static int verify_city_digits_pixels(SDL_Renderer *renderer,
                                     const QsGame *g,const char *digits,
                                     int origin,uint32_t ink) {
    static const char *zero[7]={
        ".###.","#...#","#...#",".....",
        "#...#","#...#",".###."
    };
    static const char *one[7]={
        ".....","....#","....#",".....",
        "....#","....#","....."
    };
    static const char *five[7]={
        ".###.","#....","#....",".###.",
        "....#","....#",".###."
    };
    uint32_t pixels[7][29];
    SDL_Rect area={origin,4,29,7};
    int row,col;
    if(SDL_SetRenderDrawColor(renderer,0,0,0,255)!=0 ||
       SDL_RenderClear(renderer)!=0) return 0;
    render_stage(renderer,g);
    if(SDL_RenderReadPixels(renderer,&area,SDL_PIXELFORMAT_ARGB8888,
                            pixels,29*(int)sizeof(uint32_t))!=0) return 0;
    for(row=0;row<7;row++)for(col=0;col<29;col++) {
        int which=col/8,local=col%8,light=0;
        if(digits && which<4 && local<5) {
            char d=digits[which];
            const char *shape=d=='0'?zero[row]:
                              d=='1'?one[row]:
                              d=='5'?five[row]:NULL;
            if(!shape) return 0;
            light=shape[local]=='#';
        }
        {
            uint32_t expected=light ? ink :
                                      UINT32_C(0xff1c2636);
            if(pixels[row][col]!=expected) {
                fprintf(stderr,"BONUS_SCORE_SDL_FAIL: row=%d col=%d digit=%d expected=%08x actual=%08x\n",
                        row,col,which,(unsigned)expected,
                        (unsigned)pixels[row][col]);
                return 0;
            }
        }
    }
    return 1;
}

static int verify_bonus_score_pixels(SDL_Renderer *renderer,
                                     const QsGame *g,const char *digits) {
    return verify_city_digits_pixels(renderer,g,digits,245,
                                     UINT32_C(0xffffe16c));
}

static int run_bonus_score_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,prototype;
    QsInput in={0};
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    if(qs_city_bonus_score(&g)!=0 ||
       !verify_bonus_score_pixels(renderer,&g,"0000"))
        return 0;
    /* Reverse collection order also gives 0500 before 1000. */
    g.hero.x=QS_CITY_BONUS_B_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.city_bonus_mask!=2 || qs_city_bonus_score(&g)!=500 ||
       !verify_bonus_score_pixels(renderer,&g,"0500"))
        return 0;
    g.hero.x=QS_CITY_BONUS_A_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.city_bonus_mask!=3 || qs_city_bonus_score(&g)!=1000 ||
       !verify_bonus_score_pixels(renderer,&g,"1000"))
        return 0;
    /* A lethal retry preserves the two-token score without replaying
     * the collection event. The UI is checked after the actual respawn. */
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=g.hero.x;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    qs_step(&g,in);
    if(g.hero.x!=40 || g.hero.hearts!=3 ||
       qs_city_bonus_score(&g)!=1000 ||
       !verify_bonus_score_pixels(renderer,&g,"1000"))
        return 0;
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_bonus_score(&g)!=0) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_bonus_mask!=0 ||
       !verify_bonus_score_pixels(renderer,&g,"0000"))
        return 0;
    g.city_bonus_mask=INT_MAX; /* Fail-closed read before simulation. */
    if(qs_city_bonus_score(&g)!=0 ||
       !verify_bonus_score_pixels(renderer,&g,"0000"))
        return 0;
    qs_init(&prototype);qs_begin_stage(&prototype,0);
    prototype.city_bonus_mask=3;
    if(qs_city_bonus_score(&prototype)!=0 ||
       !verify_bonus_score_pixels(renderer,&prototype,NULL))
        return 0;
    puts("BONUS_SCORE_SDL_PASS: exact seven-segment 0000/0500/1000, fatal retry, fresh attempt and prototype isolation");
    return 1;
}

/*
 * Stage 63: actual SDL score readback for current (gold) vs personal
 * best (cyan). Reentry wipes 0000 current while preserving 0500 PB;
 * later improvement raises PB to 1000, a weaker attempt cannot
 * lower it. Verify the entire independent 29x7 glyph raster.
 */
static int run_personal_best_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,prototype;
    QsInput in={0};
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
#define CHECK_PB(cur,best) do { \
    if (!verify_city_digits_pixels(renderer,&g,(best),205,UINT32_C(0xff70cadd)) || \
        !verify_bonus_score_pixels(renderer,&g,(cur))) return 0; \
} while(0)
    CHECK_PB("0000","0000");
    g.hero.x=QS_CITY_BONUS_A_X-5;
    qs_step(&g,in);
    if(qs_city_bonus_score(&g)!=500 ||
       qs_city_best_bonus_score(&g)!=500) return 0;
    CHECK_PB("0500","0500");
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_bonus_score(&g)!=0 ||
       qs_city_best_bonus_score(&g)!=500) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_bonus_mask ||
       qs_city_best_bonus_score(&g)!=500) return 0;
    memset(g.enemies,0,sizeof(g.enemies));
    CHECK_PB("0000","0500");
    in=(QsInput){0};
    g.hero.x=QS_CITY_BONUS_B_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(qs_city_bonus_score(&g)!=500 ||
       qs_city_best_bonus_score(&g)!=500) return 0;
    CHECK_PB("0500","0500");
    g.hero.x=QS_CITY_BONUS_A_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(qs_city_bonus_score(&g)!=1000 ||
       qs_city_best_bonus_score(&g)!=1000) return 0;
    CHECK_PB("1000","1000");
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || !g.cleared[0] ||
       qs_city_best_bonus_score(&g)!=1000) return 0;
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_bonus_mask!=0 ||
       qs_city_best_bonus_score(&g)!=1000) return 0;
    memset(g.enemies,0,sizeof(g.enemies));
    CHECK_PB("0000","1000");
    in=(QsInput){0};
    g.hero.x=QS_CITY_BONUS_A_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(qs_city_bonus_score(&g)!=500 ||
       qs_city_best_bonus_score(&g)!=1000) return 0;
    CHECK_PB("0500","1000");
    qs_init(&prototype);qs_begin_stage(&prototype,0);
    prototype.city_bonus_mask=3;
    prototype.city_best_bonus_score=1000;
    if(!verify_city_digits_pixels(renderer,&prototype,NULL,205,
                                  UINT32_C(0xff70cadd)) ||
       !verify_bonus_score_pixels(renderer,&prototype,NULL)) return 0;
#undef CHECK_PB
    puts("PERSONAL_BEST_SDL_PASS: cyan PB 0000/0500/1000, gold current 0000/0500/1000, restart and legacy isolation");
    return 1;
}

/*
 * Stage 64: ensure the selected CITY map row shows the best session
 * result, rather than only displaying it during stage gameplay.
 * Compare the actual map raster after 0000, 0500 and 1000 records.
 */
static int run_map_best_score_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    uint64_t zero=0,five=0,ten=0;
    uint32_t pixel=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    g.stage=0;g.mode=QS_MAP;g.selected_stage=0;
    if (!capture_frame(renderer,&g,&zero)) return 0;
    g.city_best_bonus_score=500;
    if (!capture_frame(renderer,&g,&five) ||
        !read_map_progress_pixels(renderer,&g,
            (uint32_t[QS_STAGE_COUNT]){0})) return 0;
    g.city_best_bonus_score=1000;
    if (!capture_frame(renderer,&g,&ten)) return 0;
    {
        SDL_Rect at={278,62,1,1};
        if (SDL_RenderReadPixels(renderer,&at,SDL_PIXELFORMAT_ARGB8888,
                                 &pixel,(int)sizeof(pixel))!=0)
            return 0;
    }
    if(zero==five || five==ten || zero==ten ||
       pixel!=UINT32_C(0xff49748e)) {
        fprintf(stderr,"MAP_BEST_SCORE_SDL_FAIL: 0000=%016llx 0500=%016llx 1000=%016llx pixel=%08x\n",
                (unsigned long long)zero,(unsigned long long)five,
                (unsigned long long)ten,(unsigned)pixel);
        return 0;
    }
    puts("MAP_BEST_SCORE_SDL_PASS: map shows distinct 0000/0500/1000 session records");
    return 1;
}

/*
 * Stage 65: exercise the real level-selector input path. Earn 0500 in
 * optional CITY TEST, visit RUINS and ROOFTOP in legacy physics, then
 * verify the blue record is still visible on the original CITY row.
 * A fresh prototype session remains free of any record display.
 */
static int run_cross_stage_record_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,blank;
    QsInput in={0};
    uint64_t saved=0,without_record=0,rooftop=0,thousand=0;
    uint32_t pixel=0;
    SDL_Rect top_segment={279,61,1,1};
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=QS_CITY_BONUS_A_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.city_best_bonus_score!=500 || qs_city_best_bonus_score(&g)!=500)
        return 0;
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_bonus_score(&g)!=500) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.right=1;qs_step(&g,in);
    in=(QsInput){0};qs_step(&g,in);
    in.select=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.stage!=1 ||
       g.physics_profile!=QS_PHYSICS_PROTOTYPE ||
       qs_city_best_bonus_score(&g)!=0) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || g.stage!=1 ||
       g.physics_profile!=QS_PHYSICS_PROTOTYPE ||
       qs_city_best_bonus_score(&g)!=500 ||
       !capture_frame(renderer,&g,&saved) ||
       SDL_RenderReadPixels(renderer,&top_segment,SDL_PIXELFORMAT_ARGB8888,
                            &pixel,(int)sizeof(pixel))!=0 ||
       pixel!=UINT32_C(0xff70cadd)) {
        fprintf(stderr,"CROSS_STAGE_RECORD_SDL_FAIL: after ruins pixel=%08x best=%d\n",
                (unsigned)pixel,qs_city_best_bonus_score(&g));
        return 0;
    }
    blank=g;
    blank.city_best_bonus_score=0;
    if(!capture_frame(renderer,&blank,&without_record) ||
       saved==without_record) {
        fputs("CROSS_STAGE_RECORD_SDL_FAIL: map pixel hash independent of best\n",stderr);
        return 0;
    }
    /* A second normal stage must not wipe the session record. */
    in=(QsInput){0};qs_step(&g,in);
    in.right=1;qs_step(&g,in);
    in=(QsInput){0};qs_step(&g,in);
    in.select=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.stage!=2 ||
       g.physics_profile!=QS_PHYSICS_PROTOTYPE ||
       qs_city_best_bonus_score(&g)!=0) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || g.stage!=2 ||
       qs_city_best_bonus_score(&g)!=500 ||
       !capture_frame(renderer,&g,&rooftop) ||
       SDL_RenderReadPixels(renderer,&top_segment,SDL_PIXELFORMAT_ARGB8888,
                            &pixel,(int)sizeof(pixel))!=0 ||
       pixel!=UINT32_C(0xff70cadd)) {
        fputs("CROSS_STAGE_RECORD_SDL_FAIL: after rooftop\n",stderr);
        return 0;
    }
    /* Return to the measured city, improve the record, exit again. */
    in=(QsInput){0};qs_step(&g,in);
    in.right=1;qs_step(&g,in);
    if(g.selected_stage!=0 || g.mode!=QS_MAP) return 0;
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE ||
       g.physics_profile!=QS_PHYSICS_REFERENCE_CITY ||
       qs_city_best_bonus_score(&g)!=500) return 0;
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    in=(QsInput){0};
    /*
     * Stage 68: the personal best is for the highest SINGLE attempt.
     * Collect A and B within this new attempt before expecting 1000.
     * One lone B in a new attempt is 500, even when A was earned
     * on an earlier failed/abandoned run.
     */
    g.hero.x=QS_CITY_BONUS_A_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.city_bonus_mask!=1 ||
       qs_city_bonus_score(&g)!=500 ||
       qs_city_best_bonus_score(&g)!=500) {
        fprintf(stderr,"CROSS_STAGE_RECORD_SDL_FAIL: first token after reentry mask=%d best=%d\n",
                g.city_bonus_mask,qs_city_best_bonus_score(&g));
        return 0;
    }
    g.hero.x=QS_CITY_BONUS_B_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.city_bonus_mask!=3 ||
       qs_city_bonus_score(&g)!=1000 ||
       qs_city_best_bonus_score(&g)!=1000) {
        fprintf(stderr,"CROSS_STAGE_RECORD_SDL_FAIL: complete set mask=%d best=%d\n",
                g.city_bonus_mask,qs_city_best_bonus_score(&g));
        return 0;
    }
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_bonus_score(&g)!=1000 ||
       !capture_frame(renderer,&g,&thousand) ||
       SDL_RenderReadPixels(renderer,&top_segment,SDL_PIXELFORMAT_ARGB8888,
                            &pixel,(int)sizeof(pixel))!=0 ||
       pixel!=UINT32_C(0xff49748e)) {
        fprintf(stderr,"CROSS_STAGE_RECORD_SDL_FAIL: 1000 on map pixel=%08x\n",
                (unsigned)pixel);
        return 0;
    }
    qs_init(&blank);
    if(qs_city_best_bonus_score(&blank)!=0 ||
       !capture_frame(renderer,&blank,&without_record) ||
       SDL_RenderReadPixels(renderer,&top_segment,SDL_PIXELFORMAT_ARGB8888,
                            &pixel,(int)sizeof(pixel))!=0 ||
       pixel!=UINT32_C(0xff49748e)) {
        fputs("CROSS_STAGE_RECORD_SDL_FAIL: default prototype map altered\n",stderr);
        return 0;
    }
    printf("CROSS_STAGE_RECORD_SDL_PASS: ruins=%016llx rooftop=%016llx improved=%016llx fresh=%016llx\n",
           (unsigned long long)saved,(unsigned long long)rooftop,
           (unsigned long long)thousand,(unsigned long long)without_record);
    return 1;
}

/*
 * Stage 67: verify a real SDL timer with 0000 -> 0003, a completed
 * four-tick run, a slower replay, a faster new record, and the legacy
 * map with no synthetic timer. Exact colors and frame hashes ensure
 * that the new digits reach the display rather than just core state.
 */
static int run_city_time_trial_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g, blank;
    QsInput in={0};
    uint64_t first=0,elapsed=0,medal=0,none=0,fast=0,legacy=0;
    uint32_t green=0,old_left=0,new_left=0,map_green=0,plain=0;
    SDL_Rect mark={239,61,1,1};
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    if(qs_city_attempt_time(&g)!=0 ||
       !capture_frame(renderer,&g,&first) ||
       !read_multishot_pixel(renderer,&g,167,4,&green) ||
       green!=UINT32_C(0xff5edd8a) ||
       !read_multishot_pixel(renderer,&g,190,5,&old_left) ||
       old_left!=green)
        return 0;
    qs_step(&g,in);qs_step(&g,in);qs_step(&g,in);
    if(qs_city_attempt_time(&g)!=3 ||
       !capture_frame(renderer,&g,&elapsed) ||
       elapsed==first ||
       !read_multishot_pixel(renderer,&g,190,5,&new_left) ||
       new_left!=UINT32_C(0xff1c2636))
        return 0;
    /* Crossing an already-met goal records the fourth counted tick. */
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_clear_time(&g)!=4 ||
       g.city_attempt_frames!=0 ||
       !capture_frame(renderer,&g,&medal) ||
       SDL_RenderReadPixels(renderer,&mark,SDL_PIXELFORMAT_ARGB8888,
                            &map_green,(int)sizeof(map_green))!=0 ||
       map_green!=green) {
        fprintf(stderr,"CITY_TIME_TRIAL_SDL_FAIL: best=%d green=%08x\n",
                qs_city_best_clear_time(&g),(unsigned)map_green);
        return 0;
    }
    blank=g;
    blank.city_best_clear_frames=0;
    if(!capture_frame(renderer,&blank,&none) || none==medal)
        return 0;
    /* Slower clear must not overwrite the personal best. */
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    for(int i=0;i<12;i++)qs_step(&g,in);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,in);
    if(qs_city_best_clear_time(&g)!=4)return 0;
    /* Fresh faster clear must replace the old record. */
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,in);
    if(qs_city_best_clear_time(&g)!=1 ||
       !capture_frame(renderer,&g,&fast) || fast==medal)
        return 0;
    /* Regular second stage hides the clock in gameplay, yet the map
     * still displays the record after leaving that stage. */
    qs_begin_stage(&g,1);
    if(g.physics_profile!=QS_PHYSICS_PROTOTYPE ||
       qs_city_best_clear_time(&g)!=0) return 0;
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_clear_time(&g)!=1 ||
       !capture_frame(renderer,&g,&legacy) ||
       SDL_RenderReadPixels(renderer,&mark,SDL_PIXELFORMAT_ARGB8888,
                            &map_green,(int)sizeof(map_green))!=0 ||
       map_green!=green) return 0;
    qs_init(&blank);
    if(qs_city_best_clear_time(&blank)!=0 ||
       !capture_frame(renderer,&blank,&legacy) ||
       SDL_RenderReadPixels(renderer,&mark,SDL_PIXELFORMAT_ARGB8888,
                            &plain,(int)sizeof(plain))!=0 ||
       plain==green)
        return 0;
    printf("CITY_TIME_TRIAL_SDL_PASS: initial=%016llx tick3=%016llx clear4=%016llx clear1=%016llx\n",
           (unsigned long long)first,(unsigned long long)elapsed,
           (unsigned long long)medal,(unsigned long long)fast);
    return 1;
}

/*
 * Stage 69: SDL proof that the current checkpoint split appears only
 * after a real survived touch, remains frozen through a lethal retry,
 * and that the fastest split survives Back on the map. This reads real
 * SDL pixels and compares the entire map image against a no-record map.
 */
static int run_city_checkpoint_split_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,blank,legacy;
    QsInput in={0};
    uint64_t earned_map=0,unearned_map=0,improved_map=0;
    uint32_t map_pixel=0,empty_pixel=0;
    SDL_Rect mark={239,203,1,1};
    const uint32_t amber=UINT32_C(0xffffb481);
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;

    if(qs_city_checkpoint_split_time(&g)!=0 ||
       qs_city_best_checkpoint_time(&g)!=0 ||
       !verify_city_digits_pixels(renderer,&g,NULL,126,amber))
        return 0;
    for(int t=0;t<9;t++) qs_step(&g,in);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(!g.city_checkpoint_active ||
       qs_city_checkpoint_split_time(&g)!=10 ||
       qs_city_best_checkpoint_time(&g)!=10 ||
       qs_city_attempt_time(&g)!=10 ||
       !verify_city_digits_pixels(renderer,&g,"0010",126,amber))
        return 0;
    /* Death after valid touch retains first split but advances clock. */
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=g.hero.x;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    qs_step(&g,in);
    if(g.hero.x!=QS_CITY_CHECKPOINT_X ||
       g.hero.hearts!=3 ||
       qs_city_checkpoint_split_time(&g)!=10 ||
       qs_city_attempt_time(&g)!=11 ||
       !verify_city_digits_pixels(renderer,&g,"0010",126,amber))
        return 0;
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || g.city_checkpoint_split_frames!=0 ||
       qs_city_checkpoint_split_time(&g)!=0 ||
       qs_city_best_checkpoint_time(&g)!=10 ||
       !capture_frame(renderer,&g,&earned_map) ||
       SDL_RenderReadPixels(renderer,&mark,SDL_PIXELFORMAT_ARGB8888,
                            &map_pixel,(int)sizeof(map_pixel))!=0 ||
       map_pixel!=amber) {
        fprintf(stderr,"CITY_SPLIT_SDL_FAIL: map value=%d color=%08x\n",
                qs_city_best_checkpoint_time(&g),(unsigned)map_pixel);
        return 0;
    }
    blank=g;
    blank.city_best_checkpoint_frames=0;
    if(!capture_frame(renderer,&blank,&unearned_map) ||
       earned_map==unearned_map ||
       SDL_RenderReadPixels(renderer,&mark,SDL_PIXELFORMAT_ARGB8888,
                            &empty_pixel,(int)sizeof(empty_pixel))!=0 ||
       empty_pixel!=UINT32_C(0xff1f2a40))
        return 0;

    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE ||
       g.physics_profile!=QS_PHYSICS_REFERENCE_CITY ||
       qs_city_checkpoint_split_time(&g)!=0 ||
       qs_city_best_checkpoint_time(&g)!=10 ||
       !verify_city_digits_pixels(renderer,&g,NULL,126,amber))
        return 0;
    in=(QsInput){0};
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(qs_city_checkpoint_split_time(&g)!=1 ||
       qs_city_best_checkpoint_time(&g)!=1 ||
       !verify_city_digits_pixels(renderer,&g,"0001",126,amber))
        return 0;
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_checkpoint_time(&g)!=1 ||
       !capture_frame(renderer,&g,&improved_map) ||
       improved_map==earned_map)
        return 0;
    qs_init(&legacy);
    qs_begin_stage(&legacy,0);
    if(qs_city_checkpoint_split_time(&legacy)!=0 ||
       !verify_city_digits_pixels(renderer,&legacy,NULL,126,amber))
        return 0;
    qs_init(&blank);
    if(qs_city_best_checkpoint_time(&blank)!=0)
        return 0;
    printf("CITY_SPLIT_SDL_PASS: split10=%016llx empty=%016llx best1=%016llx amber=%08x\n",
           (unsigned long long)earned_map,
           (unsigned long long)unearned_map,
           (unsigned long long)improved_map,(unsigned)amber);
    return 1;
}

/*
 * Stage 70: exact SDL pixels for a first split with no comparison,
 * a green -0005 improvement, a red +0005 slower run and a cyan
 * 0000 tie. The previous PB, not a just-written record, is the
 * reference; this test detects changing the order of those steps.
 */
static int run_checkpoint_pace_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    uint32_t signed_color=0,top=0;
    const uint32_t green=UINT32_C(0xff5edd8a);
    const uint32_t red=UINT32_C(0xffef606c);
    const uint32_t cyan=UINT32_C(0xff70cadd);
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    for(int i=0;i<9;i++)qs_step(&g,in);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(qs_city_best_checkpoint_time(&g)!=10 ||
       qs_city_checkpoint_gap_valid(&g) ||
       !verify_city_digits_pixels(renderer,&g,NULL,89,green))
        return 0;
    /* Restart through the real map path and beat the 10-tick record
     * in 5 ticks: -0005 green with a horizontal minus sign only. */
    in.back=1;qs_step(&g,in);
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE)return 0;
    in=(QsInput){0};
    memset(g.enemies,0,sizeof(g.enemies));
    for(int i=0;i<4;i++)qs_step(&g,in);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(!qs_city_checkpoint_gap_valid(&g) ||
       qs_city_checkpoint_gap_time(&g)!=-5 ||
       qs_city_best_checkpoint_time(&g)!=5 ||
       !verify_city_digits_pixels(renderer,&g,"0005",89,green) ||
       !read_multishot_pixel(renderer,&g,81,7,&signed_color) ||
       signed_color!=green ||
       !read_multishot_pixel(renderer,&g,81,5,&top) ||
       top!=UINT32_C(0xff1c2636)) {
        fprintf(stderr,"CHECKPOINT_PACE_SDL_FAIL: faster=%08x vertical=%08x\n",
                (unsigned)signed_color,(unsigned)top);
        return 0;
    }
    /* Another real attempt takes 10 ticks, five slower than PB. */
    in.back=1;qs_step(&g,in);
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE)return 0;
    in=(QsInput){0};
    memset(g.enemies,0,sizeof(g.enemies));
    for(int i=0;i<9;i++)qs_step(&g,in);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(!qs_city_checkpoint_gap_valid(&g) ||
       qs_city_checkpoint_gap_time(&g)!=5 ||
       qs_city_best_checkpoint_time(&g)!=5 ||
       !verify_city_digits_pixels(renderer,&g,"0005",89,red) ||
       !read_multishot_pixel(renderer,&g,81,7,&signed_color) ||
       signed_color!=red ||
       !read_multishot_pixel(renderer,&g,81,5,&top) ||
       top!=red) {
        fprintf(stderr,"CHECKPOINT_PACE_SDL_FAIL: slower=%08x vertical=%08x\n",
                (unsigned)signed_color,(unsigned)top);
        return 0;
    }
    /* Exactly matching PB is a valid cyan zero, not 'no comparison'. */
    in.back=1;qs_step(&g,in);
    in=(QsInput){0};qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE)return 0;
    in=(QsInput){0};
    memset(g.enemies,0,sizeof(g.enemies));
    for(int i=0;i<4;i++)qs_step(&g,in);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(!qs_city_checkpoint_gap_valid(&g) ||
       qs_city_checkpoint_gap_time(&g)!=0 ||
       !verify_city_digits_pixels(renderer,&g,"0000",89,cyan) ||
       !read_multishot_pixel(renderer,&g,81,7,&signed_color) ||
       signed_color!=cyan)
        return 0;
    /* Legacy profile has no pace pixels, even if its state is forged. */
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.city_checkpoint_active=1;
    legacy.city_checkpoint_split_frames=2;
    legacy.city_checkpoint_gap_valid=1;
    legacy.city_checkpoint_gap_frames=-1;
    if(qs_city_checkpoint_gap_valid(&legacy) ||
       !verify_city_digits_pixels(renderer,&legacy,NULL,89,green))
        return 0;
    puts("CHECKPOINT_PACE_SDL_PASS: faster -0005 green, slower +0005 red, tied 0000 cyan, first split absent");
    return 1;
}

/*
 * Stage 71: an interactive R + Start practice retry changes the
 * rendered CITY TEST scene immediately, without visiting the map.
 * Snapshot comparisons are performed against a clean stage and
 * independent digits/original-free token positions in SDL ARGB8888.
 */
static int run_city_quick_retry_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,expected;
    QsInput in={0};
    uint64_t before=0,after=0,fresh=0,held=0;
    const uint32_t green=UINT32_C(0xff5edd8a);
    const uint32_t gold=UINT32_C(0xffffe16c);
    uint32_t flag=0,checkpoint=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(!g.city_checkpoint_active ||
       qs_city_best_checkpoint_time(&g)!=1)return 0;
    g.hero.x=QS_CITY_BONUS_A_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(qs_city_bonus_score(&g)!=500 ||
       qs_city_best_bonus_score(&g)!=500)return 0;
    g.city_best_clear_frames=17; /* previous session result */
    g.pending_reference_shot=1;
    g.pending_reference_red=1;
    g.shots[0].active=g.shots[0].attached=1;
    g.shots[0].x=101;g.shots[0].y=160;
    g.shots[0].frames=40;
    if(!capture_frame(renderer,&g,&before))return 0;

    /* Fresh baseline includes identical session PBs but no local
     * stage objects, split, coins or elapsed time. */
    expected=g;
    qs_begin_stage(&expected,0);
    in.back=in.dash=in.fire=in.right=in.jump=1;
    qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.stage!=0 ||
       g.hero.x!=40 || g.hero.y!=188 ||
       g.city_attempt_frames!=0 ||
       g.city_checkpoint_active || g.city_bonus_mask ||
       g.city_checkpoint_split_frames || g.city_checkpoint_gap_valid ||
       g.pending_reference_shot || g.shots[0].active ||
       g.city_best_bonus_score!=500 ||
       g.city_best_checkpoint_frames!=1 ||
       g.city_best_clear_frames!=17 ||
       !capture_frame(renderer,&g,&after) ||
       !capture_frame(renderer,&expected,&fresh) ||
       before==after || after!=fresh ||
       !verify_city_digits_pixels(renderer,&g,"0000",166,green) ||
       !verify_city_digits_pixels(renderer,&g,"0000",245,gold) ||
       !verify_city_digits_pixels(renderer,&g,"0500",205,
                                  UINT32_C(0xff70cadd))) {
        fprintf(stderr,"CITY_QUICK_RETRY_SDL_FAIL: before=%016llx after=%016llx fresh=%016llx\n",
                (unsigned long long)before,(unsigned long long)after,
                (unsigned long long)fresh);
        return 0;
    }
    /* Holding R + Start cannot repeatedly reset the clock and the
     * same held Fire input cannot trigger an unexpected new edge. */
    qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_attempt_frames!=1 ||
       g.pending_reference_shot ||
       !capture_frame(renderer,&g,&held) || held==after)
        return 0;
    in=(QsInput){0};qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_attempt_frames!=2)return 0;
    in.back=1;qs_step(&g,in);
    if(g.mode!=QS_MAP ||
       qs_city_best_bonus_score(&g)!=500 ||
       qs_city_best_clear_time(&g)!=17 ||
       qs_city_best_checkpoint_time(&g)!=1 ||
       g.city_attempt_frames!=0 ||
       !read_map_progress_pixels(renderer,&g,
            (uint32_t[QS_STAGE_COUNT]){0}))
        return 0;
    printf("CITY_QUICK_RETRY_SDL_PASS: old=%016llx restart=%016llx fresh=%016llx held=%016llx\n",
           (unsigned long long)before,(unsigned long long)after,
           (unsigned long long)fresh,(unsigned long long)held);
    (void)checkpoint;(void)flag;
    return 1;
}

/*
 * Stage 72: actual SDL pixels for a new, breakable original-free
 * obstacle. Its exterior and inner highlights must disappear after
 * a yellow projectile. Respawn keeps the destruction, R+Start
 * rebuilds it, and ordinary CITY prototype never draws it.
 */
static int run_breakable_crate_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,prototype;
    QsInput in={0};
    uint32_t before=0,inner=0,after=0,after_inner=0;
    uint32_t life=0,retry=0,legacy=0;
    const int cam=840;
    const int pixel_x=(int)QS_CITY_CRATE_X-cam;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.grounded=1;
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.city_crate_active!=1 ||
       !read_multishot_pixel(renderer,&g,pixel_x,193,&before) ||
       before!=UINT32_C(0xffb66059) ||
       !read_multishot_pixel(renderer,&g,pixel_x+4,196,&inner) ||
       inner!=UINT32_C(0xffe69d68)) {
        fprintf(stderr,"CRATE_SDL_FAIL: initial=%08x inner=%08x\n",
                (unsigned)before,(unsigned)inner);
        return 0;
    }
    g.shots[0].active=1;
    g.shots[0].x=QS_CITY_CRATE_X-11;
    g.shots[0].y=197;
    g.shots[0].vx=5.5f;
    g.shots[0].frames=65;
    for(int t=0;t<5 && g.city_crate_active;t++) qs_step(&g,in);
    if(g.city_crate_active!=0 || g.shots[0].active)return 0;
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,pixel_x,193,&after) ||
       !read_multishot_pixel(renderer,&g,pixel_x+4,196,&after_inner) ||
       after!=UINT32_C(0xff85bad4) || after_inner!=after) {
        fprintf(stderr,"CRATE_SDL_FAIL: removed=%08x interior=%08x\n",
                (unsigned)after,(unsigned)after_inner);
        return 0;
    }
    /* Respawn from a pit does NOT rebuild this attempt's crate. */
    g.hero.x=21*QS_TILE;g.hero.y=261;
    g.hero.grounded=0;g.hero.vy=7.5f;
    qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.hero.x!=40 || g.city_crate_active ||
       !read_multishot_pixel(renderer,&g,pixel_x,193,&life) ||
       life!=after)return 0;
    /* R+Start creates a fresh attempt without leaving the scene. */
    in.back=in.dash=1;
    qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.mode!=QS_STAGE || g.city_crate_active!=1 ||
       g.hero.x!=40 ||
       !read_multishot_pixel(renderer,&g,pixel_x,193,&retry) ||
       retry!=before) {
        fprintf(stderr,"CRATE_SDL_FAIL: retry=%08x active=%d\n",
                (unsigned)retry,g.city_crate_active);
        return 0;
    }
    qs_init(&prototype);qs_begin_stage(&prototype,0);
    prototype.camera_x=prototype.render_camera_x=(float)cam;
    if(prototype.city_crate_active ||
       !read_multishot_pixel(renderer,&prototype,pixel_x,193,&legacy) ||
       legacy==before) {
        fprintf(stderr,"CRATE_SDL_FAIL: legacy=%08x\n",(unsigned)legacy);
        return 0;
    }
    /* The old prototype has its own skyline (not the experimental
     * city sky). Forging a city-only flag must never render the crate. */
    prototype.city_crate_active=1;
    {
        uint32_t with_forged_flag=0;
        if(!read_multishot_pixel(renderer,&prototype,pixel_x,193,&with_forged_flag) ||
           with_forged_flag!=legacy)
            return 0;
    }
    printf("CRATE_SDL_PASS: intact=%08x inner=%08x destroyed=%08x life=%08x retry=%08x legacy=%08x\n",
           (unsigned)before,(unsigned)inner,(unsigned)after,
           (unsigned)life,(unsigned)retry,(unsigned)legacy);
    return 1;
}

/*
 * Stage 74: read REAL SDL pixels for the transient crate fragments.
 * 18-tick countdown is simulation-driven: the initial flash stays
 * in the impact frame, fragments drift, then pixels restore exactly
 * to the no-crate background. A quick retry has no lingering debris.
 */
static int run_city_crate_debris_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    uint32_t base_left=0,base_top=0,base_right=0;
    uint32_t first_left=0,first_top=0,first_right=0;
    uint32_t moved_left=0,gone_left=0,gone_top=0,restored=0;
    uint32_t forged=0;
    const int cam=840;
    const int x=(int)QS_CITY_CRATE_X-cam;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    /* Baseline has no FX and the crate cannot occupy the pixels
     * outside its own 16x16 body. */
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,x-6,188,&base_left) ||
       !read_multishot_pixel(renderer,&g,x+7,183,&base_top) ||
       !read_multishot_pixel(renderer,&g,x+18,190,&base_right))
        return 0;
    g.shots[0].active=1;
    g.shots[0].frames=65;
    g.shots[0].x=QS_CITY_CRATE_X-11;
    g.shots[0].y=197;
    g.shots[0].vx=5.5f;
    for(int i=0;i<4 && g.city_crate_active;i++)qs_step(&g,in);
    if(g.city_crate_active || g.city_crate_fx_frames!=QS_CITY_CRATE_FX_TICKS)
        return 0;
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,x-6,188,&first_left) ||
       !read_multishot_pixel(renderer,&g,x+7,183,&first_top) ||
       !read_multishot_pixel(renderer,&g,x+18,190,&first_right) ||
       first_left!=UINT32_C(0xffffd67f) ||
       first_top!=UINT32_C(0xfffff2be) ||
       first_right!=UINT32_C(0xffe69d68) ||
       first_left==base_left || first_top==base_top ||
       first_right==base_right) {
        fprintf(stderr,"CRATE_DEBRIS_SDL_FAIL: impact=%08x/%08x/%08x base=%08x/%08x/%08x\n",
                (unsigned)first_left,(unsigned)first_top,
                (unsigned)first_right,(unsigned)base_left,
                (unsigned)base_top,(unsigned)base_right);
        return 0;
    }
    for(int t=0;t<6;t++)qs_step(&g,in);
    if(g.city_crate_fx_frames!=QS_CITY_CRATE_FX_TICKS-6)return 0;
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,x-9,186,&moved_left) ||
       !read_multishot_pixel(renderer,&g,x-6,188,&gone_left) ||
       moved_left!=first_left || gone_left!=base_left)
        return 0;
    for(int t=6;t<QS_CITY_CRATE_FX_TICKS;t++)qs_step(&g,in);
    if(g.city_crate_fx_frames!=0 || g.city_crate_active)return 0;
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,x-6,188,&gone_left) ||
       !read_multishot_pixel(renderer,&g,x+7,183,&gone_top) ||
       gone_left!=base_left || gone_top!=base_top)
        return 0;

    /* A fresh crate must have no fragments from the prior attempt. */
    in.back=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || !g.city_crate_active ||
       g.city_crate_fx_frames!=0)return 0;
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,x-6,188,&restored) ||
       restored!=base_left)return 0;

    /* Even an injected FX flag may not render in legacy gameplay. */
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.camera_x=legacy.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&legacy,x-6,188,&restored))
        return 0;
    legacy.city_crate_active=0;
    legacy.city_crate_fx_frames=QS_CITY_CRATE_FX_TICKS;
    if(!read_multishot_pixel(renderer,&legacy,x-6,188,&forged) ||
       forged!=restored)
        return 0;
    printf("CRATE_DEBRIS_SDL_PASS: impact=%08x/%08x/%08x moved=%08x fade=%08x legacy=%08x\n",
           (unsigned)first_left,(unsigned)first_top,
           (unsigned)first_right,(unsigned)moved_left,
           (unsigned)gone_left,(unsigned)forged);
    return 1;
}

/*
 * Stage 75: prove that overlapping a box is not enough when a real
 * solid or enemy face is closer. The actual SDL crate pixel remains
 * intact after blocked shots, then disappears after an unblocked hit.
 */
static int run_city_crate_priority_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g;
    QsInput in={0};
    uint32_t shielded=0,enemy_shielded=0,destroyed=0;
    const int cam=840;
    /* Probe right-hand exposed body pixel: an enemy in front of the
     * box can visually cover its LEFT pixels while the box is intact.
     * Enemy 963..977, sampled crate pixel world 980 lies beyond it. */
    const int x=(int)QS_CITY_CRATE_X-cam+12;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    g.terrain[12][60]='#';
    g.shots[0].active=1;g.shots[0].red=1;
    g.shots[0].x=QS_CITY_CRATE_X-7;
    g.shots[0].y=197;g.shots[0].vx=1;g.shots[0].frames=65;
    qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(!g.city_crate_active || g.city_crate_fx_frames ||
       !g.shots[0].active || !g.shots[0].attached ||
       g.shots[0].x!=952 ||
       !read_multishot_pixel(renderer,&g,x,193,&shielded) ||
       shielded!=UINT32_C(0xffb66059)) {
        fprintf(stderr,"CRATE_PRIORITY_SDL_FAIL: wall active=%d x=%f pixel=%08x\n",
                g.city_crate_active,g.shots[0].x,(unsigned)shielded);
        return 0;
    }
    /* A closer foe wins the same overlapping substep. */
    g.terrain[12][60]='.';
    memset(g.shots,0,sizeof(g.shots));
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=
        g.enemies[0].max_x=QS_CITY_CRATE_X-5;
    g.enemies[0].y=192;g.enemies[0].dir=1;
    g.shots[0].active=1;
    g.shots[0].x=QS_CITY_CRATE_X-7;
    g.shots[0].y=197;g.shots[0].vx=1;g.shots[0].frames=65;
    qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(!g.city_crate_active || g.city_crate_fx_frames ||
       g.enemies[0].stunned_frames!=179 ||
       !read_multishot_pixel(renderer,&g,x,193,&enemy_shielded) ||
       enemy_shielded!=shielded) {
        fprintf(stderr,"CRATE_PRIORITY_SDL_FAIL: enemy active=%d stunned=%d pixel=%08x\n",
                g.city_crate_active,g.enemies[0].stunned_frames,
                (unsigned)enemy_shielded);
        return 0;
    }
    /* No preceding wall/foe: the exact same bullet destroys crate. */
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    g.shots[0].active=1;g.shots[0].x=QS_CITY_CRATE_X-7;
    g.shots[0].y=197;g.shots[0].vx=1;g.shots[0].frames=65;
    qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.city_crate_active ||
       g.city_crate_fx_frames!=QS_CITY_CRATE_FX_TICKS ||
       !read_multishot_pixel(renderer,&g,x,193,&destroyed) ||
       destroyed!=UINT32_C(0xff85bad4)) {
        fprintf(stderr,"CRATE_PRIORITY_SDL_FAIL: clear active=%d fx=%d pixel=%08x\n",
                g.city_crate_active,g.city_crate_fx_frames,
                (unsigned)destroyed);
        return 0;
    }
    printf("CRATE_PRIORITY_SDL_PASS: wall=%08x foe=%08x destroyed=%08x\n",
           (unsigned)shielded,(unsigned)enemy_shielded,
           (unsigned)destroyed);
    return 1;
}

/*
 * Stage 76: verify the exact pause-emblem raster and unchanged game
 * frame across simulated input and 60 skipped ticks. The display
 * returns byte-for-byte to its original view on resume; R+Start
 * from paused redraws a clean new stage. No copyrighted bitmaps.
 */
static int run_city_training_pause_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,baseline,legacy;
    QsInput in={0};
    uint64_t before=0,paused=0,still_paused=0,resumed=0,retry=0,fresh=0;
    uint64_t legacy_normal=0,legacy_forged=0;
    uint32_t bar=0,gap=0,unpaused_bar=0;
    const uint32_t yellow=UINT32_C(0xffffe16c);
    const uint32_t dark=UINT32_C(0xff1c2636);
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.grounded=1;
    g.hero.vx=g.hero.vy=0;
    g.city_best_bonus_score=500;
    g.city_best_checkpoint_frames=7;
    g.city_best_clear_frames=49;
    if(!capture_frame(renderer,&g,&before))return 0;
    in.back=in.change=1;
    qs_step(&g,in);
    if(!g.city_paused || g.city_attempt_frames!=0 ||
       !capture_frame(renderer,&g,&paused) || paused==before ||
       !read_multishot_pixel(renderer,&g,193,110,&bar) || bar!=yellow ||
       !read_multishot_pixel(renderer,&g,200,111,&gap) || gap!=dark) {
        fprintf(stderr,"CITY_PAUSE_SDL_FAIL: initial before=%016llx paused=%016llx left=%08x gap=%08x\n",
                (unsigned long long)before,(unsigned long long)paused,
                (unsigned)bar,(unsigned)gap);
        return 0;
    }
    in=(QsInput){0};in.right=in.jump=in.fire=in.dash=1;
    for(int i=0;i<60;i++)qs_step(&g,in);
    if(!g.city_paused || g.city_attempt_frames!=0 ||
       g.hero.x!=100 || g.city_crate_fx_frames!=0 ||
       !capture_frame(renderer,&g,&still_paused) ||
       still_paused!=paused) {
        fprintf(stderr,"CITY_PAUSE_SDL_FAIL: frozen=%016llx initial=%016llx\n",
                (unsigned long long)still_paused,(unsigned long long)paused);
        return 0;
    }
    in=(QsInput){0};in.change=in.back=1;
    qs_step(&g,in);
    if(g.city_paused || g.city_attempt_frames!=0 ||
       !capture_frame(renderer,&g,&resumed) || resumed!=before ||
       !read_multishot_pixel(renderer,&g,193,110,&unpaused_bar) ||
       unpaused_bar==yellow) {
        fprintf(stderr,"CITY_PAUSE_SDL_FAIL: resumed=%016llx before=%016llx\n",
                (unsigned long long)resumed,(unsigned long long)before);
        return 0;
    }
    /* Stop again, then use R+Start to restart without a menu frame. */
    in=(QsInput){0};qs_step(&g,in);
    in.back=in.change=1;qs_step(&g,in);
    if(!g.city_paused || g.city_attempt_frames!=1)return 0;
    baseline=g;
    qs_begin_stage(&baseline,0);
    in=(QsInput){0};qs_step(&g,in);
    in.back=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_paused || g.hero.x!=40 ||
       g.city_attempt_frames!=0 || g.city_best_bonus_score!=500 ||
       g.city_best_checkpoint_frames!=7 || g.city_best_clear_frames!=49 ||
       !capture_frame(renderer,&g,&retry) ||
       !capture_frame(renderer,&baseline,&fresh) || retry!=fresh) {
        fprintf(stderr,"CITY_PAUSE_SDL_FAIL: retry=%016llx fresh=%016llx\n",
                (unsigned long long)retry,(unsigned long long)fresh);
        return 0;
    }
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    if(!capture_frame(renderer,&legacy,&legacy_normal))return 0;
    legacy.city_paused=1; /* forged state in the ordinary profile */
    if(!capture_frame(renderer,&legacy,&legacy_forged) ||
       legacy_forged!=legacy_normal)
        return 0;
    /*
     * Stage 77: regression for the CI failures in older safety chords.
     * Back+X+Fire/Confirm is NOT the standalone X+Start pause; the
     * legacy map exit must still render the actual map immediately.
     */
    {
        QsGame mixed;
        QsInput keys={0};
        uint64_t exit_map=0;
        qs_init(&mixed);
        mixed.physics_profile=QS_PHYSICS_REFERENCE_CITY;
        qs_begin_stage(&mixed,0);
        keys.back=keys.change=keys.fire=keys.select=1;
        qs_step(&mixed,keys);
        if(mixed.mode!=QS_MAP || mixed.city_paused ||
           !capture_frame(renderer,&mixed,&exit_map) ||
           exit_map==paused) {
            fprintf(stderr,"CITY_PAUSE_SDL_FAIL: mixed safety chord triggered pause\n");
            return 0;
        }
    }
    printf("CITY_PAUSE_SDL_PASS: before=%016llx paused=%016llx resume=%016llx retry=%016llx\n",
           (unsigned long long)before,(unsigned long long)paused,
           (unsigned long long)resumed,(unsigned long long)retry);
    return 1;
}

/*
 * Stage 78: verify the live lavender second-sector digits, and
 * the map's session best ONLY after a legitimate checkpoint+finish.
 * A shorter second sector can become PB despite a slower full run.
 */
static int run_city_sector_time_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,blank,legacy;
    QsInput in={0};
    uint32_t dark=0,live=0,map_sector=0,empty=0;
    uint64_t with_pb=0,without_pb=0,faster=0;
    SDL_Rect probe={279,203,1,1};
    const uint32_t violet=UINT32_C(0xffb7a0ff);
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    if(qs_city_sector_time(&g)!=0 || qs_city_best_sector_time(&g)!=0 ||
       !verify_city_digits_pixels(renderer,&g,NULL,48,violet) ||
       !read_multishot_pixel(renderer,&g,49,4,&dark) ||
       dark!=UINT32_C(0xff1c2636))
        return 0;
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(g.city_checkpoint_split_frames!=1 ||
       qs_city_sector_time(&g)!=0 ||
       !verify_city_digits_pixels(renderer,&g,"0000",48,violet))
        return 0;
    for(int t=0;t<5;t++)qs_step(&g,in);
    if(qs_city_sector_time(&g)!=5 ||
       !verify_city_digits_pixels(renderer,&g,"0005",48,violet) ||
       !read_multishot_pixel(renderer,&g,49,4,&live) || live!=violet)
        return 0;
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;g.hero.y=188;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_clear_time(&g)!=7 ||
       qs_city_best_sector_time(&g)!=6 ||
       qs_city_sector_time(&g)!=0 ||
       !capture_frame(renderer,&g,&with_pb) ||
       SDL_RenderReadPixels(renderer,&probe,SDL_PIXELFORMAT_ARGB8888,
                            &map_sector,(int)sizeof(map_sector))!=0 ||
       map_sector!=violet) {
        fprintf(stderr,"CITY_SECTOR_SDL_FAIL: finish best=%d pixel=%08x\n",
                qs_city_best_sector_time(&g),(unsigned)map_sector);
        return 0;
    }
    blank=g;blank.city_best_sector_frames=0;
    if(!capture_frame(renderer,&blank,&without_pb) ||
       with_pb==without_pb ||
       SDL_RenderReadPixels(renderer,&probe,SDL_PIXELFORMAT_ARGB8888,
                            &empty,(int)sizeof(empty))!=0 ||
       empty!=UINT32_C(0xff1f2a40))
        return 0;
    /* Faster completed sector from a slower total attempt: first
     * 20 non-checkpoint frames followed by immediate checkpoint+exit. */
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    for(int t=0;t<20;t++)qs_step(&g,in);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;g.hero.y=188;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_clear_time(&g)!=7 ||
       qs_city_best_sector_time(&g)!=1 ||
       !capture_frame(renderer,&g,&faster) ||
       faster==with_pb)
        return 0;
    /* Legacy level renders no live sector, even if state is forged. */
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.city_checkpoint_active=1;
    legacy.city_checkpoint_split_frames=1;
    legacy.city_attempt_frames=6;
    legacy.city_best_sector_frames=1;
    if(qs_city_sector_time(&legacy)!=0 ||
       qs_city_best_sector_time(&legacy)!=0 ||
       !verify_city_digits_pixels(renderer,&legacy,NULL,48,violet))
        return 0;
    printf("CITY_SECTOR_SDL_PASS: first=%016llx no-record=%016llx improved=%016llx live=%08x\n",
           (unsigned long long)with_pb,(unsigned long long)without_pb,
           (unsigned long long)faster,(unsigned)live);
    return 1;
}

/*
 * Stage 79: read real ARGB8888 pixel bands under the lavender sector
 * timer. A finished PB of six frames yields full green at checkpoint,
 * a 15px green strip after three ticks, cyan on equal time, and red
 * once over PB. Pauses keep identical pixels and quick retries clear
 * only the LIVE strip without deleting the finished PB.
 */
static int run_city_sector_budget_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    uint32_t first=0,last=0,edge=0,outside=0,tie=0,over=0;
    uint64_t full=0,half=0,matched=0,slower=0,paused=0,unpaused=0;
    const uint32_t green=UINT32_C(0xff5edd8a);
    const uint32_t cyan=UINT32_C(0xff70cadd);
    const uint32_t red=UINT32_C(0xffef606c);
    const uint32_t dark=UINT32_C(0xff1c2636);
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.grounded=1;
    g.hero.vx=g.hero.vy=0;

    /* A legitimate clear after checkpoint creates the PB of 6. */
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    for(int t=0;t<5;t++)qs_step(&g,in);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;g.hero.y=188;
    qs_step(&g,in);
    if(g.mode!=QS_MAP || qs_city_best_sector_time(&g)!=6)
        return 0;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    if(qs_city_sector_budget_valid(&g) ||
       !read_multishot_pixel(renderer,&g,48,12,&first) ||
       first!=dark)return 0;

    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    if(!qs_city_sector_budget_valid(&g) ||
       qs_city_sector_budget_frames(&g)!=6 ||
       !capture_frame(renderer,&g,&full) ||
       !read_multishot_pixel(renderer,&g,48,12,&first) ||
       !read_multishot_pixel(renderer,&g,76,12,&last) ||
       first!=green || last!=green)
        return 0;
    for(int t=0;t<3;t++)qs_step(&g,in);
    if(qs_city_sector_budget_frames(&g)!=3 ||
       !capture_frame(renderer,&g,&half) || half==full ||
       !read_multishot_pixel(renderer,&g,62,12,&edge) ||
       !read_multishot_pixel(renderer,&g,63,12,&outside) ||
       edge!=green || outside!=dark) {
        fprintf(stderr,"CITY_SECTOR_BUDGET_SDL_FAIL: half edge=%08x outside=%08x\n",
                (unsigned)edge,(unsigned)outside);
        return 0;
    }
    /* Pause freezes both budget and all visible HUD pixels. */
    in.back=in.change=1;
    qs_step(&g,in);
    if(!g.city_paused || qs_city_sector_budget_frames(&g)!=3 ||
       !capture_frame(renderer,&g,&paused))
        return 0;
    in=(QsInput){0};
    for(int t=0;t<15;t++)qs_step(&g,in);
    if(qs_city_sector_budget_frames(&g)!=3 ||
       !capture_frame(renderer,&g,&unpaused) || paused!=unpaused)
        return 0;
    in.back=in.change=1;qs_step(&g,in);
    if(g.city_paused || qs_city_sector_budget_frames(&g)!=3)
        return 0;
    in=(QsInput){0};
    for(int t=0;t<3;t++)qs_step(&g,in);
    if(qs_city_sector_budget_frames(&g)!=0 ||
       !capture_frame(renderer,&g,&matched) ||
       !read_multishot_pixel(renderer,&g,48,12,&tie) || tie!=cyan)
        return 0;
    qs_step(&g,in);
    if(qs_city_sector_budget_frames(&g)!=-1 ||
       !capture_frame(renderer,&g,&slower) ||
       !read_multishot_pixel(renderer,&g,76,12,&over) ||
       over!=red || slower==matched)
        return 0;
    in.back=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || qs_city_sector_budget_valid(&g) ||
       qs_city_best_sector_time(&g)!=6 ||
       !read_multishot_pixel(renderer,&g,48,12,&first) ||
       first!=dark)
        return 0;

    /* Forged counters must not render the strip in the base profile. */
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.city_best_sector_frames=6;
    legacy.city_checkpoint_active=1;
    legacy.city_checkpoint_split_frames=1;
    legacy.city_attempt_frames=4;
    if(qs_city_sector_budget_valid(&legacy) ||
       !read_multishot_pixel(renderer,&legacy,48,12,&first) ||
       first!=dark)
        return 0;
    printf("CITY_SECTOR_BUDGET_SDL_PASS: green=%016llx half=%016llx tie=%016llx red=%016llx\n",
           (unsigned long long)full,(unsigned long long)half,
           (unsigned long long)matched,(unsigned long long)slower);
    return 1;
}

/*
 * Stage 80: actual SDL ARGB8888 pad pixels prove drawing, landing
 * impulse, short bounce glow, paused/fading raster and stage retry.
 * Probe left-edge pixels outside the hero to avoid sprite occlusion.
 */
/*
 * Stage 81: real SDL pixels prove airborne pickup appears only in
 * the opt-in city, disappears on collection and returns on retry.
 * The render path consumes state but never changes health or pickups.
 */
/*
 * Stage 82: camera-phase-independent airborne heart collection burst
 * must appear for its full first frame, freeze under pause, age to zero,
 * and clear on retry. All pixel readings are from the real SDL raster.
 */
static int run_city_air_reward_fx_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    const int cam=QS_COLUMNS*QS_TILE-QS_VIEW_W;
    const int x=(int)QS_CITY_AIR_REWARD_X-cam;
    const int y=(int)QS_CITY_AIR_REWARD_Y;
    uint32_t unearned=0,lit=0,paused=0,gone=0,retry=0,prototype=0;
    const uint32_t cream=UINT32_C(0xffffefad);
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.camera_x=g.render_camera_x=(float)cam;
    g.hero.x=100;g.hero.y=188;
    if(!read_multishot_pixel(renderer,&g,x+4,y-5,&unearned) ||
       unearned==cream) return 0;
    g.hero.x=QS_CITY_AIR_REWARD_X;
    g.hero.y=QS_CITY_AIR_REWARD_Y;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=0;g.hero.hearts=2;
    qs_step(&g,in);
    if(!g.city_air_reward_collected || g.hero.hearts!=3 ||
       g.city_air_reward_fx_frames!=QS_CITY_AIR_REWARD_FX_TICKS ||
       !read_multishot_pixel(renderer,&g,x+4,y-5,&lit) ||
       lit!=cream) {
        fprintf(stderr,"CITY_AIR_FX_SDL_FAIL: earned=%08x frames=%d\n",
                (unsigned)lit,g.city_air_reward_fx_frames);
        return 0;
    }
    in.back=in.change=1;
    qs_step(&g,in);
    for(int t=0;t<40;t++)qs_step(&g,(QsInput){0});
    if(!g.city_paused || g.city_air_reward_fx_frames!=QS_CITY_AIR_REWARD_FX_TICKS ||
       !read_multishot_pixel(renderer,&g,x+4,y-5,&paused) ||
       paused!=lit) return 0;
    qs_step(&g,in);
    for(int t=0;t<QS_CITY_AIR_REWARD_FX_TICKS;t++)
        qs_step(&g,(QsInput){0});
    if(g.city_air_reward_fx_frames ||
       !read_multishot_pixel(renderer,&g,x+4,y-5,&gone) ||
       gone==cream) return 0;
    in=(QsInput){0};in.back=in.dash=1;qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.city_air_reward_fx_frames || g.city_air_reward_collected ||
       !read_multishot_pixel(renderer,&g,x+4,y-5,&retry) ||
       retry==cream) return 0;
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.camera_x=legacy.render_camera_x=(float)cam;
    legacy.city_air_reward_collected=1;
    legacy.city_air_reward_fx_frames=QS_CITY_AIR_REWARD_FX_TICKS;
    if(!read_multishot_pixel(renderer,&legacy,x+4,y-5,&prototype) ||
       prototype==cream) return 0;
    printf("CITY_AIR_FX_SDL_PASS: new=%08x paused=%08x gone=%08x\n",
           (unsigned)lit,(unsigned)paused,(unsigned)gone);
    return 1;
}


/* Stage 83: precise SDL HUD pixels for earned, pause, retry and both
 * camera extremes. A forged flag in the legacy profile stays invisible. */
static int run_city_air_reward_hud_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    uint32_t idle_pixel=0,earned=0,front=0,back=0;
    uint32_t respawn=0,expired=0,paused=0,retry=0,old=0;
    const uint32_t grey=UINT32_C(0xff5a586b);
    const uint32_t mint=UINT32_C(0xff5fe4da);
    const uint32_t dark=UINT32_C(0xff1c2636);
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    if(!read_multishot_pixel(renderer,&g,391,8,&idle_pixel) ||
       idle_pixel!=grey) return 0;
    g.hero.x=QS_CITY_AIR_REWARD_X;
    g.hero.y=QS_CITY_AIR_REWARD_Y;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=0;g.hero.hearts=2;
    qs_step(&g,in);
    if(g.city_air_reward_collected!=1 || g.hero.hearts!=3 ||
       !read_multishot_pixel(renderer,&g,391,8,&earned) ||
       earned!=mint) return 0;
    g.camera_x=g.render_camera_x=0;
    if(!read_multishot_pixel(renderer,&g,391,8,&front) ||
       front!=mint) return 0;
    g.camera_x=g.render_camera_x=(float)(QS_COLUMNS*QS_TILE-QS_VIEW_W);
    if(!read_multishot_pixel(renderer,&g,391,8,&back) ||
       back!=mint) return 0;
    g.hero.y=NAN;
    qs_step(&g,in);
    if(g.hero.x!=40 || !g.city_air_reward_collected ||
       !read_multishot_pixel(renderer,&g,391,8,&respawn) ||
       respawn!=mint) return 0;
    for(int i=0;i<QS_CITY_AIR_REWARD_FX_TICKS+2;i++)
        qs_step(&g,in);
    if(g.city_air_reward_fx_frames ||
       !read_multishot_pixel(renderer,&g,391,8,&expired) ||
       expired!=mint) return 0;
    in.back=in.change=1;qs_step(&g,in);
    if(!g.city_paused) return 0;
    for(int i=0;i<20;i++)qs_step(&g,(QsInput){0});
    if(!read_multishot_pixel(renderer,&g,391,8,&paused) ||
       paused!=mint) return 0;
    in.back=in.dash=1;qs_step(&g,in);
    if(g.mode!=QS_STAGE || g.city_paused || g.city_air_reward_collected ||
       !read_multishot_pixel(renderer,&g,391,8,&retry) ||
       retry!=grey) return 0;
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.city_air_reward_collected=1;
    if(!read_multishot_pixel(renderer,&legacy,391,8,&old) ||
       old!=dark) return 0;
    printf("CITY_AIR_HUD_SDL_PASS idle=%08x earned=%08x retry=%08x\n",
           (unsigned)idle_pixel,(unsigned)earned,(unsigned)retry);
    return 1;
}

static int run_city_air_reward_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    const int cam=880;
    const int x=(int)QS_CITY_AIR_REWARD_X-cam;
    const int y=(int)QS_CITY_AIR_REWARD_Y;
    const uint32_t shine=UINT32_C(0xfffff0ae);
    uint32_t before=0,after=0,restored=0,prototype=0;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.camera_x=g.render_camera_x=(float)cam;
    g.hero.x=100;g.hero.y=188;
    if(!read_multishot_pixel(renderer,&g,x+4,y+4,&before) ||
       before!=shine) {
        fprintf(stderr,"CITY_AIR_REWARD_SDL_FAIL: before=%08x\n",
                (unsigned)before);
        return 0;
    }
    g.hero.x=QS_CITY_AIR_REWARD_X;
    g.hero.y=QS_CITY_AIR_REWARD_Y;
    g.hero.vx=g.hero.vy=0;
    g.hero.grounded=0;
    g.hero.hearts=2;
    qs_step(&g,in);
    if(g.city_air_reward_collected!=1 || g.hero.hearts!=3 ||
       !read_multishot_pixel(renderer,&g,x+4,y+4,&after) ||
       after==shine) {
        fprintf(stderr,"CITY_AIR_REWARD_SDL_FAIL: after=%08x state=%d\n",
                (unsigned)after,g.city_air_reward_collected);
        return 0;
    }
    in.back=in.dash=1;
    qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.mode!=QS_STAGE || g.city_air_reward_collected ||
       !read_multishot_pixel(renderer,&g,x+4,y+4,&restored) ||
       restored!=shine) {
        fprintf(stderr,"CITY_AIR_REWARD_SDL_FAIL: retry=%08x\n",
                (unsigned)restored);
        return 0;
    }
    qs_init(&legacy);
    qs_begin_stage(&legacy,0);
    legacy.camera_x=legacy.render_camera_x=(float)cam;
    legacy.hero.x=100;
    if(!read_multishot_pixel(renderer,&legacy,x+4,y+4,&prototype) ||
       prototype==shine) {
        fprintf(stderr,"CITY_AIR_REWARD_SDL_FAIL: prototype=%08x\n",
                (unsigned)prototype);
        return 0;
    }
    printf("CITY_AIR_REWARD_SDL_PASS: before=%08x collected=%08x retry=%08x\n",
           (unsigned)before,(unsigned)after,(unsigned)restored);
    return 1;
}


/* Stage 84: real SDL pixel readback for the city-only sky ledge. */
static int run_city_sky_ledge_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    const int cam=QS_COLUMNS*QS_TILE-QS_VIEW_W;
    const int x=(int)QS_CITY_SKY_LEDGE_X-cam;
    const int y=(int)QS_CITY_SKY_LEDGE_TOP;
    uint32_t body=0,trim=0,shifted=0,old=0;
    const uint32_t cyan=UINT32_C(0xff68b1cd);
    const uint32_t cream=UINT32_C(0xffffefad);
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=40;g.hero.y=188;
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,x+2,y-2,&body) ||
       !read_multishot_pixel(renderer,&g,x+5,y-5,&trim) ||
       body!=cyan || trim!=cream) {
        fprintf(stderr,"CITY_SKY_LEDGE_SDL_FAIL body=%08x trim=%08x\n",
                (unsigned)body,(unsigned)trim);
        return 0;
    }
    g.render_camera_x=(float)cam-20;
    if(!read_multishot_pixel(renderer,&g,x+22,y-2,&shifted) ||
       shifted!=cyan) return 0;
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.camera_x=legacy.render_camera_x=(float)cam;
    legacy.hero.x=40;legacy.hero.y=188;
    if(!read_multishot_pixel(renderer,&legacy,x+2,y-2,&old) ||
       old==cyan) return 0;
    printf("CITY_SKY_LEDGE_SDL_PASS body=%08x trim=%08x\n",
           (unsigned)body,(unsigned)trim);
    return 1;
}

static int run_city_spring_sdl_smoke(SDL_Renderer *renderer) {
    QsGame g,legacy;
    QsInput in={0};
    const int cam=960;
    const int x=(int)QS_CITY_SPRING_X-cam;
    uint32_t metal=0,accent=0,idle_glow=0,active_glow=0;
    uint32_t aged=0,faded=0,reset=0,legacy_plain=0,legacy_forged=0;
    uint64_t frozen_first=0,frozen_later=0;
    const uint32_t orange=UINT32_C(0xffffc95c);
    const uint32_t cream=UINT32_C(0xffffefad);
    const uint32_t cyan=UINT32_C(0xff70cadd);
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    g.camera_x=g.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&g,x+1,205,&metal) ||
       !read_multishot_pixel(renderer,&g,x+4,202,&accent) ||
       !read_multishot_pixel(renderer,&g,x+5,198,&idle_glow) ||
       metal!=orange || accent!=cream || idle_glow==cyan) {
        fprintf(stderr,"CITY_SPRING_SDL_FAIL: idle=%08x/%08x/%08x\n",
                (unsigned)metal,(unsigned)accent,(unsigned)idle_glow);
        return 0;
    }
    g.hero.x=QS_CITY_SPRING_X+6;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.hero.vy!=QS_CITY_SPRING_BOUNCE || g.hero.grounded ||
       g.city_spring_flash_frames!=QS_CITY_SPRING_FLASH_TICKS ||
       !read_multishot_pixel(renderer,&g,x+5,198,&active_glow) ||
       active_glow!=cyan ||
       !read_multishot_pixel(renderer,&g,x+1,205,&metal) ||
       metal!=orange) {
        fprintf(stderr,"CITY_SPRING_SDL_FAIL: launch vy=%f flash=%d pixel=%08x\n",
                g.hero.vy,g.city_spring_flash_frames,(unsigned)active_glow);
        return 0;
    }
    /* Pause holds both sprite and short-lived spring highlight. */
    in.back=in.change=1;qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(!g.city_paused ||
       g.city_spring_flash_frames!=QS_CITY_SPRING_FLASH_TICKS ||
       !capture_frame(renderer,&g,&frozen_first))
        return 0;
    in=(QsInput){0};
    for(int t=0;t<20;t++)qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.city_spring_flash_frames!=QS_CITY_SPRING_FLASH_TICKS ||
       !capture_frame(renderer,&g,&frozen_later) ||
       frozen_first!=frozen_later)
        return 0;
    in.back=in.change=1;qs_step(&g,in);
    if(g.city_paused)return 0;
    in=(QsInput){0};
    for(int t=0;t<6;t++)qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.city_spring_flash_frames!=QS_CITY_SPRING_FLASH_TICKS-6 ||
       !read_multishot_pixel(renderer,&g,x+5,198,&aged) ||
       aged!=cyan)
        return 0;
    for(int t=6;t<QS_CITY_SPRING_FLASH_TICKS;t++)qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.city_spring_flash_frames!=0 ||
       !read_multishot_pixel(renderer,&g,x+5,198,&faded) ||
       faded!=idle_glow)
        return 0;
    /* R+Start restores a normal ready pad without a ghost highlight. */
    in.back=in.dash=1;qs_step(&g,in);
    g.camera_x=g.render_camera_x=(float)cam;
    if(g.mode!=QS_STAGE || g.city_spring_flash_frames!=0 ||
       g.hero.x!=40 ||
       !read_multishot_pixel(renderer,&g,x+5,198,&reset) ||
       reset!=idle_glow)
        return 0;
    qs_init(&legacy);qs_begin_stage(&legacy,0);
    legacy.camera_x=legacy.render_camera_x=(float)cam;
    if(!read_multishot_pixel(renderer,&legacy,x+1,205,&legacy_plain))
        return 0;
    legacy.city_spring_flash_frames=QS_CITY_SPRING_FLASH_TICKS;
    if(!read_multishot_pixel(renderer,&legacy,x+1,205,&legacy_forged) ||
       legacy_plain!=legacy_forged || legacy_plain==orange)
        return 0;
    printf("CITY_SPRING_SDL_PASS: metal=%08x accent=%08x glow=%08x faded=%08x paused=%016llx\n",
           (unsigned)metal,(unsigned)accent,(unsigned)active_glow,
           (unsigned)faded,(unsigned long long)frozen_first);
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
    if (!run_frame_arbitration_sdl_smoke(renderer)) return 1;
    if (!run_timer_boundary_sdl_smoke(renderer)) return 1;
    if (!run_hero_status_sdl_smoke(renderer)) return 1;
    if (!run_pre_move_shots_sdl_smoke(renderer)) return 1;
    if (!run_weapon_queue_sdl_smoke(renderer)) return 1;
    if (!run_city_map_integrity_sdl_smoke(renderer)) return 1;
    if (!run_scene_recovery_sdl_smoke(renderer)) return 1;
    if (!run_shot_canonical_sdl_smoke(renderer)) return 1;
    if (!run_city_checkpoint_sdl_smoke(renderer)) return 1;
    if (!run_checkpoint_hud_sdl_smoke(renderer)) return 1;
    if (!run_bonus_beacons_sdl_smoke(renderer)) return 1;
    if (!run_bonus_set_heal_sdl_smoke(renderer)) return 1;
    if (!run_bonus_score_sdl_smoke(renderer)) return 1;
    if (!run_map_best_score_sdl_smoke(renderer)) return 1;
    if (!run_cross_stage_record_sdl_smoke(renderer)) return 1;
    if (!run_city_time_trial_sdl_smoke(renderer)) return 1;
    if (!run_city_checkpoint_split_sdl_smoke(renderer)) return 1;
    if (!run_checkpoint_pace_sdl_smoke(renderer)) return 1;
    if (!run_city_quick_retry_sdl_smoke(renderer)) return 1;
    if (!run_breakable_crate_sdl_smoke(renderer)) return 1;
    if (!run_city_crate_debris_sdl_smoke(renderer)) return 1;
    if (!run_city_crate_priority_sdl_smoke(renderer)) return 1;
    if (!run_city_training_pause_sdl_smoke(renderer)) return 1;
    if (!run_city_sector_time_sdl_smoke(renderer)) return 1;
    if (!run_city_sector_budget_sdl_smoke(renderer)) return 1;
    if (!run_city_spring_sdl_smoke(renderer)) return 1;
    if (!run_city_sky_ledge_sdl_smoke(renderer)) return 1;
    if (!run_city_air_reward_sdl_smoke(renderer)) return 1;
    if (!run_city_air_reward_fx_sdl_smoke(renderer)) return 1;
    if (!run_city_air_reward_hud_sdl_smoke(renderer)) return 1;
    if (!run_personal_best_sdl_smoke(renderer)) return 1;
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
#ifdef __SWITCH__
    /*
     * Stage 88: a separate, clearly labelled deterministic GAME-CORE
     * self-test runs once in the guest NRO. It does not inject pad
     * events into the visible session; those are still measured using
     * quackshot-motion.csv. NeXium demands the route CSV as evidence.
     */
    {
        const char *path = getenv("QS_ROUTE_PATH");
        FILE *route_stream;
        if (!path || !*path) path="sdmc:/switch/quackshot-route.csv";
        route_stream=fopen(path,"w");
        if (!route_stream) {
            fprintf(stderr,"CITY_ROUTE_PROBE_UNAVAILABLE: cannot write guest route CSV\n");
        } else {
            int ok=qs_route_probe_write(route_stream);
            if (fclose(route_stream)!=0)ok=0;
            fprintf(stderr,"CITY_ROUTE_PROBE_%s\n",ok?"PASS":"FAIL");
        }
    }
#endif
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
