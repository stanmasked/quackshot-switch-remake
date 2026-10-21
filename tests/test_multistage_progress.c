#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "multistage progress FAIL line %d: %s\n", __LINE__, #x); \
    exit(EXIT_FAILURE); } } while(0)

/* Only handmade TEST maps. No original QuackShot data is used. */
static QsInput idle(void) { QsInput x={0}; return x; }
static void finish(QsGame *g) {
    CHECK(g->mode == QS_STAGE);
    g->hero.x = (QS_COLUMNS-4)*QS_TILE + 0.5f;
    qs_step(g,idle());
    CHECK(g->mode == QS_MAP && g->cleared[g->stage]);
}
static QsGame map(void) {
    QsGame g; qs_init(&g);
    g.red_unlocked=1;g.weapon=QS_RED;
    return g;
}
static void navigate(QsGame *g,int backwards) {
    QsInput in=idle();
    if(backwards)in.left=1;else in.right=1;
    qs_step(g,in);
    qs_step(g,idle());
}
static void enter(QsGame *g,int city_opt_in) {
    QsInput in=idle();
    in.select=1;in.dash=city_opt_in;
    qs_step(g,in);
    CHECK(g->mode==QS_STAGE);
}
static void concurrent_right_and_confirm_only_navigates(void) {
    QsGame g=map();QsInput in=idle();
    in.right=1;in.select=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    qs_step(&g,in); /* held select never launches */
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    qs_step(&g,idle());
    in=idle();in.select=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==1);
}
static void simultaneous_opposite_direction_cancels_and_blocks_enter(void) {
    QsGame g=map();QsInput in=idle();
    in.left=1;in.right=1;in.select=1;in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
    qs_step(&g,idle());
    in=idle();in.select=1;in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
}
static void multistage_route_preserves_all_clears_and_red_unlock(void) {
    QsGame g=map();
    int stage,k;
    enter(&g,1);
    for(stage=0;stage<3;stage++) {
        CHECK(g.mode==QS_STAGE && g.stage==stage);
        CHECK(g.physics_profile==(stage==0 ?
            QS_PHYSICS_REFERENCE_CITY : QS_PHYSICS_PROTOTYPE));
        finish(&g);
        for(k=0;k<3;k++)CHECK(g.cleared[k]==(k<=stage));
        CHECK(g.red_unlocked && g.weapon==QS_RED);
        if(stage<2) {
            navigate(&g,0);
            CHECK(g.selected_stage==stage+1);
            enter(&g,0);
        }
    }
    CHECK(g.selected_stage==2);
    navigate(&g,0);
    CHECK(g.selected_stage==0 && g.mode==QS_MAP);
    enter(&g,1);
    CHECK(g.stage==0 && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
    CHECK(!g.pickups[0].active);
    for(k=0;k<3;k++)CHECK(g.cleared[k]);
}
static void alternate_profile_city_reentry_never_leaks_city_to_ruins(void) {
    QsGame g=map();
    enter(&g,1);finish(&g);
    navigate(&g,0);
    enter(&g,0);
    CHECK(g.stage==1 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    finish(&g);
    navigate(&g,1);
    enter(&g,0); /* city via ordinary select = legacy physics */
    CHECK(g.stage==0 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(g.cleared[0]&&g.cleared[1]&&g.red_unlocked);
}
static void down_navigation_and_select_same_tick_is_atomic(void) {
    QsGame g=map();QsInput in=idle();
    in.down=1;in.jump=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    in=idle();qs_step(&g,in);
    in.jump=1;qs_step(&g,in);
    CHECK(g.stage==1 && g.mode==QS_STAGE);
}
static void simultaneous_right_and_down_move_only_one_stage(void) {
    QsGame g=map();QsInput in=idle();
    in.right=1;in.down=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
}
static void progress_is_monotonic_after_repeat_clear(void) {
    QsGame g=map();
    int i;
    enter(&g,1);
    finish(&g);
    CHECK(g.cleared[0]);
    for(i=0;i<4;i++) {
        enter(&g,1);
        CHECK(g.cleared[0] && g.red_unlocked);
        finish(&g);
        CHECK(g.cleared[0]);
    }
}
static void long_three_stage_replay_stays_deterministic(void) {
    QsGame a=map(), b;
    int tick,k;
    b=a;
    for(tick=0;tick<240;tick++) {
        QsInput in=idle();
        /* Each 24-tick block navigates to one new test stage, enters,
         * completes with a test-positioned hero, then rests on map. */
        int phase=tick%24;
        if(phase==0)in.right=1;
        if(phase==2)in.select=1;
        if(phase==7 && a.mode==QS_STAGE)
            a.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
        if(phase==7 && b.mode==QS_STAGE)
            b.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
        qs_step(&a,in);
        qs_step(&b,in);
        CHECK(a.mode==b.mode && a.stage==b.stage);
        CHECK(a.selected_stage==b.selected_stage);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x);
        CHECK(a.render_camera_x==b.render_camera_x);
        CHECK(a.red_unlocked==b.red_unlocked && a.weapon==b.weapon);
        for(k=0;k<QS_STAGE_COUNT;k++)
            CHECK(a.cleared[k]==b.cleared[k]);
    }
    CHECK(a.cleared[0]&&a.cleared[1]&&a.cleared[2]);
}
int main(void) {
    concurrent_right_and_confirm_only_navigates();
    simultaneous_opposite_direction_cancels_and_blocks_enter();
    multistage_route_preserves_all_clears_and_red_unlock();
    alternate_profile_city_reentry_never_leaks_city_to_ruins();
    down_navigation_and_select_same_tick_is_atomic();
    simultaneous_right_and_down_move_only_one_stage();
    progress_is_monotonic_after_repeat_clear();
    long_three_stage_replay_stays_deterministic();
    puts("PASS: 8 multi-stage navigation/progress cases and 240-tick replay");
    return EXIT_SUCCESS;
}
