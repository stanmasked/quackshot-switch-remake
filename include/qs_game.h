#ifndef QS_GAME_H
#define QS_GAME_H

/* Asset-free gameplay prototype. No original QuackShot data embedded. */
#define QS_COLUMNS 80
#define QS_ROWS 15
#define QS_TILE 16
#define QS_STAGE_COUNT 3
#define QS_MAX_ENEMIES 6
#define QS_MAX_SHOTS 8
#define QS_MAX_PICKUPS 5
#define QS_VIEW_W 400
#define QS_VIEW_H 240
/* Original-free checkpoint authored for opt-in CITY TEST only. */
#define QS_CITY_CHECKPOINT_X 640.0f
#define QS_CITY_CHECKPOINT_Y 188.0f
/* Two synthetic optional collectibles on safe city floor segments. */
#define QS_CITY_BONUS_A_X 900.0f
#define QS_CITY_BONUS_B_X 1040.0f
#define QS_CITY_BONUS_Y 188.0f
#define QS_CITY_BONUS_VALUE 500
/* Original-free obstacle between the two CITY TEST bonus beacons. */
#define QS_CITY_CRATE_X 968.0f
#define QS_CITY_CRATE_Y 192.0f
#define QS_CITY_CRATE_SIZE 16.0f
#define QS_CITY_CRATE_FX_TICKS 18 /* transient procedural debris only */
/* Stage 80: independently authored optional bounce pad on city floor. */
#define QS_CITY_SPRING_X 1144.0f
#define QS_CITY_SPRING_TOP 208.0f
#define QS_CITY_SPRING_WIDTH 24.0f
#define QS_CITY_SPRING_BOUNCE -8.0f
#define QS_CITY_SPRING_FLASH_TICKS 12
/* Stage 81: optional airborne heart on the bounce trajectory, not an original asset. */
#define QS_CITY_AIR_REWARD_X 1152.0f
#define QS_CITY_AIR_REWARD_Y 96.0f
#define QS_CITY_AIR_REWARD_SIZE 10.0f
#define QS_CITY_TIMER_MAX 9999 /* HUD-safe capped city practice frames */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { QS_MAP, QS_STAGE } QsMode;
/* Reference-city envelope is an opt-in measured approximation, not original code. */
typedef enum { QS_PHYSICS_PROTOTYPE = 0, QS_PHYSICS_REFERENCE_CITY = 1 } QsPhysicsProfile;
typedef enum { QS_YELLOW, QS_RED } QsWeapon;
typedef enum { QS_PICKUP_RED, QS_PICKUP_HEART } QsPickupType;

typedef struct {
    int left, right, up, down, jump, fire, dash, change, select, back;
} QsInput;

typedef struct { float x, y, vx, vy; int facing, grounded, hearts, hurt_frames; } QsPlayer;
typedef struct { float x, y, min_x, max_x; int dir, stunned_frames, active; } QsEnemy;
typedef struct { float x, y, vx; int active, attached, frames, red; } QsShot;
typedef struct { float x, y; int kind, active; } QsPickup;

typedef struct {
    QsMode mode;
    QsPhysicsProfile physics_profile;
    int selected_stage, stage, cleared[QS_STAGE_COUNT], frames;
    int red_unlocked;
    int city_checkpoint_active; /* opt-in, per-stage mid-level restart */
    int city_bonus_mask; /* 0..3; collected per-attempt bonus beacons */
    int city_crate_active; /* city-only breakable box, reset on new attempt */
    int city_crate_fx_frames; /* remaining visible debris ticks, never collidable */
    int city_spring_flash_frames; /* bounded flash after successful landing */
    int city_air_reward_collected; /* per-attempt single-use airborne heart */
    int city_paused; /* opt-in X+Start training pause, not gameplay time */
    int city_best_bonus_score; /* session best: 0, 500, 1000 */
    int city_attempt_frames; /* reset at every stage/map handoff */
    int city_best_clear_frames; /* 0 means no finish; lowest wins */
    int city_checkpoint_split_frames; /* first valid flag touch in attempt, 0 if not earned */
    int city_best_checkpoint_frames; /* fastest flag split of session, 0 if none */
    int city_best_sector_frames; /* fastest finish minus split, earned only after flag+exit */
    int city_checkpoint_gap_frames; /* signed frames vs PREVIOUS best split, -9998..9998 */
    int city_checkpoint_gap_valid; /* false on first split in session */
    int pending_reference_shot; /* delayed one step only in opt-in city profile */
    int pending_reference_red; /* type latched at fire press, not dispatch */
    int reference_jump_steps; /* held jump duration; 15-frame first-city plateau */
    QsWeapon weapon;
    char terrain[QS_ROWS][QS_COLUMNS];
    QsPlayer hero;
    QsEnemy enemies[QS_MAX_ENEMIES];
    QsShot shots[QS_MAX_SHOTS];
    QsPickup pickups[QS_MAX_PICKUPS];
    float camera_x;        /* current simulation camera (city RAM 0x8524 proxy) */
    float render_camera_x; /* previous simulation-step camera, city VDP proxy */
    QsInput previous;
} QsGame;

void qs_init(QsGame *game);
void qs_begin_stage(QsGame *game, int stage);
void qs_step(QsGame *game, QsInput input);
int qs_solid(const QsGame *game, int col, int row);
const char *qs_stage_name(int stage);
int qs_city_bonus_score(const QsGame *game); /* optional practice score, max 1000 */
int qs_city_best_bonus_score(const QsGame *game); /* session PB, 0/500/1000 */
int qs_city_attempt_time(const QsGame *game); /* 0..9999 frames */
int qs_city_best_clear_time(const QsGame *game); /* 0 or 1..9999 frames */
int qs_city_checkpoint_split_time(const QsGame *game); /* 0 or 1..9999 frames */
int qs_city_best_checkpoint_time(const QsGame *game); /* 0 or 1..9999 frames */
int qs_city_sector_time(const QsGame *game); /* live post-checkpoint sector, 0..9998 */
int qs_city_best_sector_time(const QsGame *game); /* 0 or 1..9998 frames */
int qs_city_sector_budget_valid(const QsGame *game); /* has earned split AND completed-sector PB */
int qs_city_sector_budget_frames(const QsGame *game); /* signed best minus current sector: negative is over PB */
int qs_city_checkpoint_gap_valid(const QsGame *game); /* measured comparison available */
int qs_city_checkpoint_gap_time(const QsGame *game); /* signed, negative=faster */

#ifdef __cplusplus
}
#endif
#endif
