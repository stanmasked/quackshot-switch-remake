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
    QsWeapon weapon;
    char terrain[QS_ROWS][QS_COLUMNS];
    QsPlayer hero;
    QsEnemy enemies[QS_MAX_ENEMIES];
    QsShot shots[QS_MAX_SHOTS];
    QsPickup pickups[QS_MAX_PICKUPS];
    float camera_x;
    QsInput previous;
} QsGame;

void qs_init(QsGame *game);
void qs_begin_stage(QsGame *game, int stage);
void qs_step(QsGame *game, QsInput input);
int qs_solid(const QsGame *game, int col, int row);
const char *qs_stage_name(int stage);

#ifdef __cplusplus
}
#endif
#endif
