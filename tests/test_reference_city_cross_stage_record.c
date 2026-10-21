#include "qs_game.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"cross-stage record FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void touch(QsGame *g,int index) {
    g->hero.x=(index ? QS_CITY_BONUS_B_X : QS_CITY_BONUS_A_X)-5;
    g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
}
static void back_to_map(QsGame *g) {
    QsInput in=idle();in.back=1;
    qs_step(g,in);
    CHECK(g->mode==QS_MAP);
    qs_step(g,idle());
}
static void enter_next_stage(QsGame *g) {
    QsInput in=idle();in.right=1;
    CHECK(g->mode==QS_MAP);
    qs_step(g,in);
    qs_step(g,idle());
    in=idle();in.select=1;
    qs_step(g,in);
    CHECK(g->mode==QS_STAGE);
}
static void finish_stage(QsGame *g) {
    qs_step(g,idle()); /* release selector */
    g->hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    qs_step(g,idle());
    CHECK(g->mode==QS_MAP);
}
static void city_best_survives_back_and_map(void) {
    QsGame g=city();
    touch(&g,0);
    CHECK(qs_city_best_bonus_score(&g)==500);
    back_to_map(&g);
    CHECK(g.stage==0 && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(qs_city_best_bonus_score(&g)==500);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void ruins_gameplay_stays_prototype_but_map_keeps_record(void) {
    QsGame g=city();
    touch(&g,1);
    back_to_map(&g);
    enter_next_stage(&g);
    CHECK(g.stage==1 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(qs_city_best_bonus_score(&g)==0);
    CHECK(g.city_best_bonus_score==500);
    back_to_map(&g);
    CHECK(g.stage==1 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(qs_city_best_bonus_score(&g)==500);
}
static void rooftop_gameplay_stays_prototype_but_map_keeps_record(void) {
    QsGame g=city();
    touch(&g,0);touch(&g,1);
    back_to_map(&g);
    enter_next_stage(&g);
    back_to_map(&g);
    enter_next_stage(&g);
    CHECK(g.stage==2 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(qs_city_best_bonus_score(&g)==0);
    back_to_map(&g);
    CHECK(g.stage==2 && qs_city_best_bonus_score(&g)==1000);
}
static void completing_regular_stage_keeps_city_record(void) {
    QsGame g=city();
    touch(&g,0);
    back_to_map(&g);
    enter_next_stage(&g);
    finish_stage(&g);
    CHECK(g.cleared[1]==1);
    CHECK(qs_city_best_bonus_score(&g)==500);
}
static void another_selected_row_does_not_hide_city_record(void) {
    QsGame g=city();
    touch(&g,0);
    back_to_map(&g);
    enter_next_stage(&g);
    back_to_map(&g);
    CHECK(g.selected_stage==1 && qs_city_best_bonus_score(&g)==500);
    QsInput in=idle();in.right=1;
    qs_step(&g,in);
    CHECK(g.selected_stage==2 && qs_city_best_bonus_score(&g)==500);
}
static void reference_reentry_preserves_record_but_clears_current(void) {
    QsGame g=city();
    touch(&g,0);
    back_to_map(&g);
    enter_next_stage(&g);
    back_to_map(&g);
    QsInput in=idle();in.left=1;
    qs_step(&g,in);
    CHECK(g.selected_stage==0);
    qs_step(&g,idle());
    in=idle();in.select=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(qs_city_best_bonus_score(&g)==500);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void reference_reentry_can_improve_best(void) {
    QsGame g=city();
    touch(&g,1);
    back_to_map(&g);
    enter_next_stage(&g);
    back_to_map(&g);
    QsInput in=idle();in.left=1;qs_step(&g,in);
    qs_step(&g,idle());
    in=idle();in.select=in.dash=1;qs_step(&g,in);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    touch(&g,0);touch(&g,1);
    CHECK(qs_city_best_bonus_score(&g)==1000);
    back_to_map(&g);
    CHECK(qs_city_best_bonus_score(&g)==1000);
}
static void standard_city_reentry_does_not_enable_reference_mechanics(void) {
    QsGame g=city();
    touch(&g,0);
    back_to_map(&g);
    enter_next_stage(&g);
    back_to_map(&g);
    QsInput in=idle();in.left=1;qs_step(&g,in);
    qs_step(&g,idle());
    in=idle();in.select=1;qs_step(&g,in);
    CHECK(g.stage==0 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(qs_city_best_bonus_score(&g)==0);
    CHECK(g.city_best_bonus_score==500);
    back_to_map(&g);
    CHECK(qs_city_best_bonus_score(&g)==500);
}
static void invalid_record_is_hidden_on_regular_stage_map(void) {
    const int bad[]={INT_MIN,-1,1,499,501,999,1001,INT_MAX};
    for(int i=0;i<8;i++) {
        QsGame g=city();
        g.city_best_bonus_score=bad[i];
        back_to_map(&g);
        enter_next_stage(&g);
        back_to_map(&g);
        CHECK(qs_city_best_bonus_score(&g)==0);
    }
}
static void fresh_prototype_session_has_no_record(void) {
    QsGame g;qs_init(&g);
    CHECK(g.mode==QS_MAP && qs_city_best_bonus_score(&g)==0);
    qs_begin_stage(&g,1);
    back_to_map(&g);
    CHECK(qs_city_best_bonus_score(&g)==0);
}
static void new_init_clears_previous_session_record(void) {
    QsGame g=city();
    touch(&g,0);touch(&g,1);
    CHECK(qs_city_best_bonus_score(&g)==1000);
    qs_init(&g);
    CHECK(qs_city_best_bonus_score(&g)==0);
}
static void deterministic_500_tick_cross_stage_replay(void) {
    QsGame a=city(),b;
    touch(&a,0);
    back_to_map(&a);
    b=a;
    for(int t=0;t<500;t++) {
        QsInput in=idle();
        int p=t%50;
        if(p==0) {
            if(a.mode==QS_MAP && b.mode==QS_MAP) {
                in.right=1;
            }
        } else if(p==2) {
            if(a.mode==QS_MAP && b.mode==QS_MAP) in.select=1;
        } else if(p==8) {
            if(a.mode==QS_STAGE && b.mode==QS_STAGE) in.back=1;
        } else if(p==14) {
            if(a.mode==QS_MAP && b.mode==QS_MAP) in.left=1;
        } else if(p==17) {
            if(a.mode==QS_MAP && b.mode==QS_MAP) {
                in.select=1;in.dash=1;
            }
        } else if(p==25 && a.mode==QS_STAGE &&
                 a.physics_profile==QS_PHYSICS_REFERENCE_CITY) {
            a.hero.x=b.hero.x=QS_CITY_BONUS_B_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=0;a.hero.vy=b.hero.vy=0;
        } else if(p==33 && a.mode==QS_STAGE) {
            in.back=1;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames);
        CHECK(a.mode==b.mode && a.stage==b.stage);
        CHECK(a.physics_profile==b.physics_profile);
        CHECK(qs_city_best_bonus_score(&a)==qs_city_best_bonus_score(&b));
        CHECK(a.city_best_bonus_score==b.city_best_bonus_score);
        CHECK(a.city_best_bonus_score==0 || a.city_best_bonus_score==500 ||
              a.city_best_bonus_score==1000);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
    }
}
int main(void) {
    city_best_survives_back_and_map();
    ruins_gameplay_stays_prototype_but_map_keeps_record();
    rooftop_gameplay_stays_prototype_but_map_keeps_record();
    completing_regular_stage_keeps_city_record();
    another_selected_row_does_not_hide_city_record();
    reference_reentry_preserves_record_but_clears_current();
    reference_reentry_can_improve_best();
    standard_city_reentry_does_not_enable_reference_mechanics();
    invalid_record_is_hidden_on_regular_stage_map();
    fresh_prototype_session_has_no_record();
    new_init_clears_previous_session_record();
    deterministic_500_tick_cross_stage_replay();
    puts("PASS: 12 cross-stage city record scenarios and 500 deterministic ticks");
    return EXIT_SUCCESS;
}
