#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"bonus set heal FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame scene(QsPhysicsProfile profile) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=profile;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void touch(QsGame *g,int token) {
    g->hero.x=token==0?QS_CITY_BONUS_A_X-5:QS_CITY_BONUS_B_X-5;
    g->hero.y=188;
    g->hero.vx=g->hero.vy=0;
    g->hero.grounded=1;
    qs_step(g,idle());
}
static void foe(QsGame *g) {
    QsEnemy *e=&g->enemies[0];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=e->min_x=e->max_x=g->hero.x;
    e->y=g->hero.y;e->dir=1;
}
static void first_token_does_not_heal(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=2;
    touch(&g,0);
    CHECK(g.city_bonus_mask==1 && g.hero.hearts==2);
}
static void first_token_other_order_does_not_heal(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=1;
    touch(&g,1);
    CHECK(g.city_bonus_mask==2 && g.hero.hearts==1);
}
static void second_token_A_then_B_heals_one(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=2;
    touch(&g,0);touch(&g,1);
    CHECK(g.city_bonus_mask==3 && g.hero.hearts==3);
}
static void second_token_B_then_A_heals_one(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=1;
    touch(&g,1);touch(&g,0);
    CHECK(g.city_bonus_mask==3 && g.hero.hearts==2);
}
static void full_hearts_never_go_to_four(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    touch(&g,0);touch(&g,1);
    CHECK(g.hero.hearts==3 && g.city_bonus_mask==3);
}
static void revisit_after_damage_never_heals_twice(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=2;
    touch(&g,0);touch(&g,1);
    CHECK(g.hero.hearts==3);
    g.hero.hearts=1;
    for(int i=0;i<10;i++) {
        touch(&g,i&1);
        CHECK(g.city_bonus_mask==3 && g.hero.hearts==1);
    }
}
static void dying_while_touching_last_token_does_not_heal(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    touch(&g,0);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.hero.x=QS_CITY_BONUS_B_X-5;g.hero.y=188;
    foe(&g);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
    CHECK(g.hero.hearts==3 && g.hero.x==40);
    CHECK(g.hero.hurt_frames==89);
}
static void nonfatal_hit_and_second_token_heal_on_same_tick(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    touch(&g,0);
    g.hero.hearts=2;g.hero.hurt_frames=0;
    g.hero.x=QS_CITY_BONUS_B_X-5;g.hero.y=188;
    foe(&g);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==3);
    CHECK(g.hero.hearts==2);
    CHECK(g.hero.hurt_frames==69);
}
static void pit_retry_cannot_collect_last_token(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    touch(&g,0);
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3 && g.city_bonus_mask==1);
}
static void checkpoint_retry_retains_set_without_another_heal(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    g.hero.hearts=2;
    touch(&g,0);touch(&g,1);
    CHECK(g.city_bonus_mask==3 && g.hero.hearts==3);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    foe(&g);
    qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X &&
          g.hero.hearts==3 && g.city_bonus_mask==3);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.hearts=2;
    touch(&g,0);touch(&g,1);
    CHECK(g.hero.hearts==2);
}
static void map_reentry_allows_new_once_only_heal(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    QsInput back=idle();
    g.hero.hearts=2;
    touch(&g,0);touch(&g,1);
    CHECK(g.hero.hearts==3);
    back.back=1;qs_step(&g,back);
    CHECK(g.mode==QS_MAP && g.city_bonus_mask==0);
    qs_step(&g,idle());
    QsInput enter=idle();enter.select=enter.dash=1;
    qs_step(&g,enter);
    CHECK(g.mode==QS_STAGE && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.city_bonus_mask==0);
    g.hero.hearts=2;
    touch(&g,1);touch(&g,0);
    CHECK(g.hero.hearts==3 && g.city_bonus_mask==3);
}
static void forged_finished_set_does_not_heal(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.city_bonus_mask=3;
    g.hero.hearts=1;
    touch(&g,1);
    CHECK(g.hero.hearts==1 && g.city_bonus_mask==3);
}
static void corrupt_mask_resets_without_bogus_healing(void) {
    const int bad[]={INT_MIN,-1,4,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
        g.city_bonus_mask=bad[i];
        g.hero.hearts=1;
        qs_step(&g,idle());
        CHECK(g.city_bonus_mask==0 && g.hero.hearts==1);
    }
}
static void prototype_retains_legacy_health(void) {
    QsGame g=scene(QS_PHYSICS_PROTOTYPE);
    g.hero.hearts=2;
    touch(&g,0);touch(&g,1);
    CHECK(g.hero.hearts==2 && g.city_bonus_mask==0);
}
static void deterministic_540_tick_health_bonus_replay(void) {
    QsGame a=scene(QS_PHYSICS_REFERENCE_CITY),b=a;
    for(int t=0;t<540;t++) {
        int phase=t%90;
        QsInput in=idle();
        if(phase==2 && a.mode==QS_STAGE) {
            a.hero.hearts=b.hero.hearts=2;
            a.hero.hurt_frames=b.hero.hurt_frames=0;
            a.hero.x=b.hero.x=QS_CITY_BONUS_A_X-5;
        }
        if(phase==4 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_BONUS_B_X-5;
        }
        if(phase==8 && a.mode==QS_STAGE) {
            a.hero.hearts=b.hero.hearts=1;
            a.hero.hurt_frames=b.hero.hurt_frames=0;
            foe(&a);foe(&b);
        }
        if(phase==20 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;
            a.hero.vy=b.hero.vy=7.5f;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(phase==40 && a.mode==QS_STAGE) a.hero.x=b.hero.x=NAN;
        if(phase==60)in.back=1;
        if(phase==62)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_bonus_mask==b.city_bonus_mask);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x);
        CHECK(a.city_bonus_mask>=0 && a.city_bonus_mask<=3);
        CHECK(a.hero.hearts>=1 && a.hero.hearts<=3);
    }
    CHECK(a.frames==540 && b.frames==540);
}
int main(void) {
    first_token_does_not_heal();
    first_token_other_order_does_not_heal();
    second_token_A_then_B_heals_one();
    second_token_B_then_A_heals_one();
    full_hearts_never_go_to_four();
    revisit_after_damage_never_heals_twice();
    dying_while_touching_last_token_does_not_heal();
    nonfatal_hit_and_second_token_heal_on_same_tick();
    pit_retry_cannot_collect_last_token();
    checkpoint_retry_retains_set_without_another_heal();
    map_reentry_allows_new_once_only_heal();
    forged_finished_set_does_not_heal();
    corrupt_mask_resets_without_bogus_healing();
    prototype_retains_legacy_health();
    deterministic_540_tick_health_bonus_replay();
    puts("PASS: 15 two-beacon healing scenarios and 540 deterministic ticks");
    return EXIT_SUCCESS;
}
