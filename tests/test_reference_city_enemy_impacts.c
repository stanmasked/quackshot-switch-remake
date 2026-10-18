#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define T(x) do { if (!(x)) { fprintf(stderr,"enemy impact FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)

/* All game geometry below is independent and contains no commercial assets. */
static QsInput idle(void) { QsInput x={0}; return x; }
static QsGame empty(QsPhysicsProfile profile) {
    QsGame g; int x,y,i; qs_init(&g); g.physics_profile=profile;
    qs_begin_stage(&g,0);
    for(y=0;y<QS_ROWS;y++) for(x=0;x<QS_COLUMNS;x++) g.terrain[y][x]='.';
    for(i=0;i<QS_MAX_ENEMIES;i++) g.enemies[i].active=0;
    for(i=0;i<QS_MAX_PICKUPS;i++) g.pickups[i].active=0;
    g.hero.x=40; g.hero.y=140; g.hero.vx=g.hero.vy=0;
    return g;
}
static void foe(QsGame *g,int slot,float x) {
    QsEnemy *e=&g->enemies[slot]; memset(e,0,sizeof(*e));
    e->active=1; e->x=x; e->y=180; e->dir=1;
    e->min_x=x-20; e->max_x=x+20;
}
static void projectile(QsGame *g,int slot,float x,float vx,int red) {
    QsShot *s=&g->shots[slot]; memset(s,0,sizeof(*s));
    s->active=1; s->frames=65; s->x=x; s->y=185; s->vx=vx; s->red=red;
}
static void right_order_does_not_follow_slot(void) {
    QsGame a=empty(QS_PHYSICS_REFERENCE_CITY),b=empty(QS_PHYSICS_REFERENCE_CITY);
    foe(&a,0,136);foe(&a,1,135);foe(&b,0,135);foe(&b,1,136);
    projectile(&a,0,129,1,0);projectile(&b,0,129,1,0);
    qs_step(&a,idle());qs_step(&b,idle());
    T(!a.shots[0].active && !b.shots[0].active);
    T(a.enemies[1].stunned_frames==179 && a.enemies[0].stunned_frames==0);
    T(b.enemies[0].stunned_frames==179 && b.enemies[1].stunned_frames==0);
}
static void left_order_does_not_follow_slot(void) {
    QsGame a=empty(QS_PHYSICS_REFERENCE_CITY),b=empty(QS_PHYSICS_REFERENCE_CITY);
    foe(&a,0,154);foe(&a,1,157);foe(&b,0,157);foe(&b,1,154);
    projectile(&a,0,165,-1,1);projectile(&b,0,165,-1,1);
    qs_step(&a,idle());qs_step(&b,idle());
    T(a.enemies[1].stunned_frames==179 && a.enemies[0].stunned_frames==0);
    T(b.enemies[0].stunned_frames==179 && b.enemies[1].stunned_frames==0);
    T(!a.shots[0].active && !a.shots[0].attached);
}
static void both_colors_stun_not_anchor(void) {
    int red;
    for(red=0;red<2;red++) {
        QsGame g=empty(QS_PHYSICS_REFERENCE_CITY);
        foe(&g,0,135);projectile(&g,0,129,1,red);
        qs_step(&g,idle());T(g.enemies[0].stunned_frames==179);
        T(!g.shots[0].active && !g.shots[0].attached);
    }
}
static void freeze_and_recover_after_stun(void) {
    QsGame g=empty(QS_PHYSICS_REFERENCE_CITY);
    int k;float at;
    foe(&g,0,135);projectile(&g,0,129,1,0);
    qs_step(&g,idle()); at=g.enemies[0].x;
    for(k=0;k<179;k++){qs_step(&g,idle());T(g.enemies[0].x==at);}
    T(g.enemies[0].stunned_frames==0);
    qs_step(&g,idle());T(g.enemies[0].x>at);
}
static void later_hit_resets_timer(void) {
    QsGame g=empty(QS_PHYSICS_REFERENCE_CITY);
    int k;
    foe(&g,0,135);projectile(&g,0,129,1,0);qs_step(&g,idle());
    for(k=0;k<40;k++)qs_step(&g,idle());
    T(g.enemies[0].stunned_frames==139);
    projectile(&g,0,129,1,1);qs_step(&g,idle());
    T(g.enemies[0].stunned_frames==179 && !g.shots[0].active);
}
static void frozen_enemy_is_harmless_until_resumed(void) {
    QsGame g=empty(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135);g.enemies[0].stunned_frames=2;
    g.hero.x=135;g.hero.y=180;
    qs_step(&g,idle());qs_step(&g,idle());
    T(g.enemies[0].stunned_frames==0 && g.hero.hearts==3);
    qs_step(&g,idle());T(g.hero.hearts==2);
}
static void solid_wall_occludes_both_colors(void) {
    QsGame yellow=empty(QS_PHYSICS_REFERENCE_CITY);
    QsGame red=empty(QS_PHYSICS_REFERENCE_CITY);
    yellow.terrain[11][9]=red.terrain[11][9]='#';
    foe(&yellow,0,162);foe(&red,0,162);
    projectile(&yellow,0,128,60,0);
    projectile(&red,0,128,60,1);
    qs_step(&yellow,idle());qs_step(&red,idle());
    T(yellow.enemies[0].stunned_frames==0);
    T(red.enemies[0].stunned_frames==0);
    T(!yellow.shots[0].active);
    T(red.shots[0].active && red.shots[0].attached && red.shots[0].x==136);
}
static void two_shots_same_tick_replay(void) {
    QsGame a=empty(QS_PHYSICS_REFERENCE_CITY),b;
    int k;
    foe(&a,0,135);foe(&a,1,160);
    projectile(&a,0,110,55,0);projectile(&a,1,190,-55,1);
    b=a;
    for(k=0;k<20;k++){
        qs_step(&a,idle());qs_step(&b,idle());
        T(a.enemies[0].x==b.enemies[0].x);
        T(a.enemies[1].x==b.enemies[1].x);
        T(a.enemies[0].stunned_frames==b.enemies[0].stunned_frames);
        T(a.enemies[1].stunned_frames==b.enemies[1].stunned_frames);
        T(a.shots[0].active==b.shots[0].active);
        T(a.shots[1].active==b.shots[1].active);
    }
    T(a.enemies[0].stunned_frames>0 && a.enemies[1].stunned_frames>0);
}
static void legacy_mode_retains_original_slot_order(void) {
    QsGame g=empty(QS_PHYSICS_PROTOTYPE);
    foe(&g,0,136);foe(&g,1,135);
    projectile(&g,0,129,1,0);
    qs_step(&g,idle());
    T(g.enemies[0].stunned_frames==179 && g.enemies[1].stunned_frames==0);
}
int main(void) {
    right_order_does_not_follow_slot();
    left_order_does_not_follow_slot();
    both_colors_stun_not_anchor();
    freeze_and_recover_after_stun();
    later_hit_resets_timer();
    frozen_enemy_is_harmless_until_resumed();
    solid_wall_occludes_both_colors();
    two_shots_same_tick_replay();
    legacy_mode_retains_original_slot_order();
    puts("PASS: 9 ROM-free enemy impact, stun, recovery, occlusion and replay scenarios");
    return 0;
}
