#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Artificial TEST COURSE: a 32-px ledge and a 16x16 blocking tile.
 * No tiles, level layout, sprites or collision code copied from QuackShot.
 * The independent original observation merely proves that the original
 * first-city level had a 32-px floor drop and jumpable horizontal obstacle.
 */
#define VERIFY(expr) do { if (!(expr)) { \
    fprintf(stderr,"city obstacle FAIL line %d: %s\n",__LINE__,#expr); \
    exit(1); \
} } while (0)

static QsInput no_input(void) { QsInput v={0}; return v; }

static QsGame course(QsPhysicsProfile profile) {
    QsGame g;
    int col, row, i;
    qs_init(&g);
    g.physics_profile=profile;
    qs_begin_stage(&g,0);
    for (row=0;row<QS_ROWS;row++)
        for (col=0;col<QS_COLUMNS;col++) g.terrain[row][col]='.';
    for (col=0;col<QS_COLUMNS;col++) {
        g.terrain[13][col]='#'; /* lower surface y=208 */
        g.terrain[14][col]='#';
        if (col<25) {           /* upper surface y=176, ends at x=400 */
            g.terrain[11][col]='#';
            g.terrain[12][col]='#';
        }
    }
    g.terrain[12][32]='#';      /* isolated x=512 wall top y=192 */
    for (i=0;i<QS_MAX_ENEMIES;i++) g.enemies[i].active=0;
    for (i=0;i<QS_MAX_PICKUPS;i++) g.pickups[i].active=0;
    g.hero.x=61.0f; g.hero.y=156.0f;
    g.hero.vx=0; g.hero.vy=0; g.hero.grounded=1;
    g.camera_x=0;
    memset(&g.previous,0,sizeof g.previous);
    return g;
}

static void run(QsGame *g,int first,int last,int jump_start,int jump_end,
                int reverse_at) {
    int frame;
    for(frame=first;frame<last;frame++) {
        QsInput in=no_input();
        in.right=frame<reverse_at;
        in.left=frame>=reverse_at;
        in.jump=frame>=jump_start && frame<jump_end;
        qs_step(g,in);
    }
}

static void floor_drop_and_wall_stop(void) {
    QsGame g=course(QS_PHYSICS_REFERENCE_CITY);
    int frame,first_drop=-1,first_low=-1,first_wall=-1;
    for(frame=0;frame<345;frame++) {
        QsInput in=no_input(); in.right=1;
        qs_step(&g,in);
        if(first_drop<0 && g.hero.y>156.01f) first_drop=frame;
        if(first_low<0 && fabsf(g.hero.y-188.0f)<0.001f &&
           g.hero.x>390) first_low=frame;
        if(first_wall<0 && fabsf(g.hero.vx)<0.00001f && g.hero.x>480)
            first_wall=frame;
    }
    VERIFY(first_drop>=190 && first_drop<230);
    VERIFY(first_low>first_drop && first_low<250);
    VERIFY(first_wall>first_low && first_wall<310);
    VERIFY(fabsf(g.hero.y-188.0f)<0.01f);
    VERIFY(g.hero.x>=498 && g.hero.x<=502);
    VERIFY(g.hero.vx==0.0f);
    VERIFY(g.hero.grounded);
}

static void jump_timing_matters(void) {
    QsGame late=course(QS_PHYSICS_REFERENCE_CITY);
    QsGame early=course(QS_PHYSICS_REFERENCE_CITY);
    run(&late,0,345,240,275,10000);
    run(&early,0,345,195,230,10000);
    VERIFY(late.hero.x>545);
    VERIFY(early.hero.x>=498 && early.hero.x<=502);
    VERIFY(late.hero.x>early.hero.x+40);
    VERIFY(late.hero.y==188.0f);
    VERIFY(early.hero.y==188.0f);
}

static void reverse_clears_collision(void) {
    QsGame g=course(QS_PHYSICS_REFERENCE_CITY);
    run(&g,0,300,10000,10001,10000);
    VERIFY(g.hero.x>=498 && g.hero.x<=502 && g.hero.vx==0);
    run(&g,300,360,10000,10001,300);
    VERIFY(g.hero.x<435 && g.hero.vx<0);
}

static void camera_uses_provisional_city_anchor(void) {
    QsGame city=course(QS_PHYSICS_REFERENCE_CITY);
    QsGame normal=course(QS_PHYSICS_PROTOTYPE);
    QsInput right=no_input(); right.right=1;
    int f;
    for(f=0;f<80;f++) {
        qs_step(&city,right);
        qs_step(&normal,right);
    }
    VERIFY(city.camera_x>0);
    VERIFY(normal.camera_x>0);
    VERIFY(fabsf((city.hero.x-city.camera_x)-141.0f)<0.001f);
    VERIFY(fabsf((normal.hero.x-normal.camera_x)-150.0f)<0.001f);
    VERIFY(city.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    VERIFY(normal.physics_profile==QS_PHYSICS_PROTOTYPE);
}

static void camera_reverse_has_measured_deadzone(void) {
    QsGame g=course(QS_PHYSICS_REFERENCE_CITY);
    QsInput right=no_input(),left=no_input();
    float at_wall;
    unsigned i;
    right.right=1;
    left.left=1;
    for (i=0;i<305;i++) qs_step(&g,right);
    VERIFY(g.hero.vx==0.0f);
    at_wall=g.camera_x;
    VERIFY(g.hero.x-g.camera_x>=140.0f &&
           g.hero.x-g.camera_x<=142.0f);
    for (i=0;i<12;i++) qs_step(&g,left);
    /* Direction reversal must move the hero through the viewport
     * deadzone before the background begins tracking in reverse. */
    VERIFY(g.camera_x==at_wall);
    VERIFY(g.hero.x<500.0f);
    for (i=0;i<24;i++) qs_step(&g,left);
    VERIFY(g.camera_x<at_wall);
    VERIFY(g.hero.x-g.camera_x>=122.0f &&
           g.hero.x-g.camera_x<=124.0f);
    VERIFY(g.hero.vx<0);
}

static void camera_and_collision_replays_identical(void) {
    QsGame first=course(QS_PHYSICS_REFERENCE_CITY);
    QsGame again=course(QS_PHYSICS_REFERENCE_CITY);
    int f;
    for(f=0;f<350;f++) {
        QsInput in=no_input();
        in.right=1;
        in.jump=f>=240 && f<275;
        qs_step(&first,in);
        qs_step(&again,in);
        VERIFY(first.hero.x==again.hero.x);
        VERIFY(first.hero.y==again.hero.y);
        VERIFY(first.hero.vx==again.hero.vx);
        VERIFY(first.camera_x==again.camera_x);
    }
}

int main(void) {
    floor_drop_and_wall_stop();
    jump_timing_matters();
    reverse_clears_collision();
    camera_uses_provisional_city_anchor();
    camera_reverse_has_measured_deadzone();
    camera_and_collision_replays_identical();
    puts("PASS: 6 synthetic ledge/wall/camera hysteresis regression checks");
    return 0;
}
