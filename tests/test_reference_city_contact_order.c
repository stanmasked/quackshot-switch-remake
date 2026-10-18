#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"contact order FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while (0)

/* Geometry explicitly invented for regression testing, never copied. */
static QsInput no_buttons(void) { QsInput x={0}; return x; }
static QsGame scene(QsPhysicsProfile profile) {
    QsGame g; int row,col,k;
    qs_init(&g);g.physics_profile=profile;qs_begin_stage(&g,0);
    for(row=0;row<QS_ROWS;++row)
        for(col=0;col<QS_COLUMNS;++col)g.terrain[row][col]='.';
    for(k=0;k<QS_MAX_ENEMIES;++k)g.enemies[k].active=0;
    for(k=0;k<QS_MAX_PICKUPS;++k)g.pickups[k].active=0;
    g.hero.x=40;g.hero.y=140;g.hero.vx=g.hero.vy=0;
    return g;
}
static void wall(QsGame *g) {g->terrain[11][10]='#';} /* x=160..175 */
static void foe(QsGame *g,int slot,float x,float y) {
    QsEnemy *e=&g->enemies[slot];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=y;e->dir=1;
    e->min_x=x;e->max_x=x;
}
static void shot(QsGame *g,int slot,float x,float y,float vx,int red) {
    QsShot *s=&g->shots[slot];
    memset(s,0,sizeof(*s));
    s->active=1;s->red=red;s->frames=65;
    s->x=x;s->y=y;s->vx=vx;
}
static void enemy_face_before_wall_in_same_substep(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    wall(&g);
    /* At x 153.25 shot right face=160.24, and both the enemy's
     * 159.75 and wall's 160 faces were crossed in this same step. */
    foe(&g,0,159.75f,180);
    shot(&g,0,152.25f,185,1,1);
    qs_step(&g,no_buttons());
    CHECK(!g.shots[0].active && !g.shots[0].attached);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void wall_wins_coplanar_enemy_face(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    wall(&g);foe(&g,0,160,180);
    shot(&g,0,152.25f,185,1,1);
    qs_step(&g,no_buttons());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==152);
    CHECK(g.enemies[0].stunned_frames==0);
}
static void wall_wins_enemy_behind_contact(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    wall(&g);foe(&g,0,160.25f,180);
    shot(&g,0,152.25f,185,1,0);
    qs_step(&g,no_buttons());
    CHECK(!g.shots[0].active);
    CHECK(g.enemies[0].stunned_frames==0);
}
static void leftward_enemy_face_before_wall(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    wall(&g);
    /* Enemy right face 176.25 precedes tile right face 176. */
    foe(&g,0,162.25f,180);
    shot(&g,0,176.75f,185,-1,1);
    qs_step(&g,no_buttons());
    CHECK(!g.shots[0].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void leftward_wall_wins_tie(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    wall(&g);foe(&g,0,162,180);
    shot(&g,0,176.75f,185,-1,1);
    qs_step(&g,no_buttons());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==176);
    CHECK(g.enemies[0].stunned_frames==0);
}
static void hero_protected_by_simultaneous_impact(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,180);
    g.hero.x=135;g.hero.y=180;g.hero.hurt_frames=0;
    shot(&g,0,129,185,1,0);
    qs_step(&g,no_buttons());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.hero.hearts==3);
    CHECK(g.hero.hurt_frames==0);
}
static void unprotected_multiple_enemies_hurt_once(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,180);foe(&g,1,139,180);
    g.hero.x=135;g.hero.y=180;
    qs_step(&g,no_buttons());
    CHECK(g.hero.hearts==2);
    CHECK(g.hero.hurt_frames==69);
    qs_step(&g,no_buttons());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==68);
}
static void two_shots_stun_multiple_hero_touching_enemies(void) {
    QsGame a=scene(QS_PHYSICS_REFERENCE_CITY),b;
    foe(&a,0,135,180);foe(&a,1,160,180);
    a.hero.x=145;a.hero.y=180;
    shot(&a,0,129,185,1,0);
    shot(&a,1,166,185,-1,1);
    b=a;
    qs_step(&a,no_buttons());qs_step(&b,no_buttons());
    CHECK(a.enemies[0].stunned_frames==179);
    CHECK(a.enemies[1].stunned_frames==179);
    CHECK(a.hero.hearts==3);
    CHECK(a.enemies[0].stunned_frames==b.enemies[0].stunned_frames);
    CHECK(a.enemies[1].stunned_frames==b.enemies[1].stunned_frames);
    CHECK(a.hero.hearts==b.hero.hearts);
}
static void prototype_keeps_wall_priority_on_tie(void) {
    QsGame g=scene(QS_PHYSICS_PROTOTYPE);
    wall(&g);foe(&g,0,159.75f,180);
    shot(&g,0,152.25f,185,1,1);
    qs_step(&g,no_buttons());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.enemies[0].stunned_frames==0);
}
int main(void) {
    enemy_face_before_wall_in_same_substep();
    wall_wins_coplanar_enemy_face();
    wall_wins_enemy_behind_contact();
    leftward_enemy_face_before_wall();
    leftward_wall_wins_tie();
    hero_protected_by_simultaneous_impact();
    unprotected_multiple_enemies_hurt_once();
    two_shots_stun_multiple_hero_touching_enemies();
    prototype_keeps_wall_priority_on_tie();
    puts("PASS: 9 city concurrent shot/wall/enemy/hero contact-order scenarios");
    return 0;
}
