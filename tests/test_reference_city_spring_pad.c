#include "qs_game.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"spring pad FAIL line %d: %s\n",__LINE__,#expr); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput input={0};return input; }
static QsGame city(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    return g;
}
static void position(QsGame *g,float x,float y,int grounded) {
    g->hero.x=x;g->hero.y=y;
    g->hero.vx=g->hero.vy=0;
    g->hero.grounded=grounded;
}
static void spring(QsGame *g) {
    position(g,QS_CITY_SPRING_X+6,188,1);
    qs_step(g,idle());
    CHECK(g->hero.grounded==0);
    CHECK(g->hero.vy==QS_CITY_SPRING_BOUNCE);
    CHECK(g->city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS);
}
static void new_attempt_has_no_flash(void) {
    QsGame g=city();
    CHECK(!g.city_spring_flash_frames);
    CHECK(QS_CITY_SPRING_X>QS_CITY_BONUS_B_X);
}
static void grounded_contact_launches_at_exact_impulse(void) {
    QsGame g=city();spring(&g);
    CHECK(g.hero.x==QS_CITY_SPRING_X+6);
    CHECK(g.hero.y==188);
    CHECK(g.city_attempt_frames==1);
}
static void second_tick_travels_up_and_ages_flash(void) {
    QsGame g=city();spring(&g);
    qs_step(&g,idle());
    CHECK(g.hero.y<188);
    CHECK(g.hero.vy<0);
    CHECK(!g.hero.grounded);
    CHECK(g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS-1);
}
static void holding_jump_cannot_extend_spring_impulse(void) {
    QsGame g=city();spring(&g);
    QsInput in=idle();in.jump=1;
    qs_step(&g,in);
    CHECK(g.hero.vy>QS_CITY_SPRING_BOUNCE);
    CHECK(g.reference_jump_steps>0);
}
static void far_left_does_not_trigger(void) {
    QsGame g=city();
    position(&g,QS_CITY_SPRING_X-12,188,1);
    qs_step(&g,idle());
    CHECK(g.hero.grounded==1 && g.hero.vy==0);
    CHECK(!g.city_spring_flash_frames);
}
static void far_right_does_not_trigger(void) {
    QsGame g=city();
    position(&g,QS_CITY_SPRING_X+QS_CITY_SPRING_WIDTH,188,1);
    qs_step(&g,idle());
    CHECK(g.hero.grounded==1 && g.hero.vy==0);
    CHECK(!g.city_spring_flash_frames);
}
static void narrow_overlap_at_left_edge_triggers(void) {
    QsGame g=city();
    position(&g,QS_CITY_SPRING_X-11.5f,188,1);
    qs_step(&g,idle());
    CHECK(g.hero.vy==QS_CITY_SPRING_BOUNCE);
}
static void narrow_overlap_at_right_edge_triggers(void) {
    QsGame g=city();
    position(&g,QS_CITY_SPRING_X+QS_CITY_SPRING_WIDTH-0.5f,188,1);
    qs_step(&g,idle());
    CHECK(g.hero.vy==QS_CITY_SPRING_BOUNCE);
}
static void passing_above_pad_without_landing_does_not_launch(void) {
    QsGame g=city();
    position(&g,QS_CITY_SPRING_X+6,150,0);
    qs_step(&g,idle());
    CHECK(g.hero.y<188);
    CHECK(g.hero.vy>0 && g.hero.vy<QS_CITY_SPRING_BOUNCE*-1);
    CHECK(!g.city_spring_flash_frames);
}
static void flash_counts_down_to_zero(void) {
    QsGame g=city();spring(&g);
    position(&g,100,188,1);
    for(int i=1;i<=QS_CITY_SPRING_FLASH_TICKS;i++) {
        qs_step(&g,idle());
        CHECK(g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS-i);
    }
    for(int i=0;i<30;i++) {
        qs_step(&g,idle());
        CHECK(!g.city_spring_flash_frames);
    }
}
static void pit_respawn_does_not_forge_new_bounce(void) {
    QsGame g=city();spring(&g);
    int before=g.city_spring_flash_frames;
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==40);
    CHECK(g.city_spring_flash_frames==before-1);
}
static void quick_retry_resets_spring_flash(void) {
    QsGame g=city();spring(&g);
    QsInput in=idle();in.back=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.hero.x==40);
    CHECK(!g.city_spring_flash_frames && !g.city_attempt_frames);
}
static void map_exit_and_reentry_reset_flash(void) {
    QsGame g=city();spring(&g);
    QsInput in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_spring_flash_frames);
    qs_step(&g,idle());in=idle();in.select=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && !g.city_spring_flash_frames);
}
static void pause_freezes_bounce_flash(void) {
    QsGame g=city();spring(&g);
    QsInput in=idle();in.back=in.change=1;
    qs_step(&g,in);
    CHECK(g.city_paused);
    for(int i=0;i<40;i++)qs_step(&g,idle());
    CHECK(g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS);
    CHECK(g.hero.vy==QS_CITY_SPRING_BOUNCE);
    qs_step(&g,in);
    CHECK(!g.city_paused);
    CHECK(g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS);
    qs_step(&g,idle());
    CHECK(g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS-1);
}
static void invalid_flash_values_normalize_safely(void) {
    const int bad[]={INT_MIN,-1,QS_CITY_SPRING_FLASH_TICKS+1,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.city_spring_flash_frames=bad[i];
        qs_step(&g,idle());
        CHECK(g.city_spring_flash_frames==0);
    }
}
static void prototype_does_not_gain_spring_physics(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    position(&g,QS_CITY_SPRING_X+6,188,1);
    qs_step(&g,idle());
    CHECK(g.hero.vy!=QS_CITY_SPRING_BOUNCE);
    CHECK(!g.city_spring_flash_frames);
}
static void spring_is_repeatable_after_new_landing(void) {
    QsGame g=city();spring(&g);
    int launches=1;
    for(int i=0;i<125;i++) {
        int prev=g.city_spring_flash_frames;
        qs_step(&g,idle());
        if(g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS &&
           prev!=QS_CITY_SPRING_FLASH_TICKS) launches++;
    }
    CHECK(launches>=2);
}
static void deterministic_540_tick_bounce_route(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<540;t++) {
        QsInput in=idle();
        int phase=t%90;
        if(phase==5 && a.mode==QS_STAGE) {
            position(&a,QS_CITY_SPRING_X+6,188,1);
            position(&b,QS_CITY_SPRING_X+6,188,1);
        }
        if(phase==30 && a.mode==QS_STAGE){in.back=in.change=1;}
        if(phase==47 && a.mode==QS_STAGE){in.back=in.change=1;}
        if(phase==52 && a.mode==QS_STAGE){in.back=in.dash=1;}
        if(phase==72 && a.mode==QS_STAGE){in.back=1;}
        if(phase==76 && a.mode==QS_MAP){in.select=in.dash=1;}
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_spring_flash_frames==b.city_spring_flash_frames);
        CHECK(a.city_attempt_frames==b.city_attempt_frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.vy==b.hero.vy);
        CHECK(a.city_spring_flash_frames>=0 &&
              a.city_spring_flash_frames<=QS_CITY_SPRING_FLASH_TICKS);
    }
    CHECK(a.frames==540 && b.frames==540);
}
int main(void) {
    new_attempt_has_no_flash();
    grounded_contact_launches_at_exact_impulse();
    second_tick_travels_up_and_ages_flash();
    holding_jump_cannot_extend_spring_impulse();
    far_left_does_not_trigger();
    far_right_does_not_trigger();
    narrow_overlap_at_left_edge_triggers();
    narrow_overlap_at_right_edge_triggers();
    passing_above_pad_without_landing_does_not_launch();
    flash_counts_down_to_zero();
    pit_respawn_does_not_forge_new_bounce();
    quick_retry_resets_spring_flash();
    map_exit_and_reentry_reset_flash();
    pause_freezes_bounce_flash();
    invalid_flash_values_normalize_safely();
    prototype_does_not_gain_spring_physics();
    spring_is_repeatable_after_new_landing();
    deterministic_540_tick_bounce_route();
    puts("PASS: 18 spring landing scenarios and 540 deterministic ticks");
    return EXIT_SUCCESS;
}
