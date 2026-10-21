#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)){ \
    fprintf(stderr,"final route FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE);} } while(0)

typedef struct {
    int spring_tick, heart_tick, ledge_tick, exit_tick;
    int pause_ticks, attempt_at_exit, total_frames, reached_exit;
    int hearts_at_exit, best_time, cleared, jump_used;
    float landing_x;
} RouteWitness;

static QsInput idle(void) { QsInput i={0};return i; }
static QsGame final_segment(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    /* This is an opt-in final-segment fixture, not an assertion that
     * every earlier city obstacle can be passed without steering. */
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=QS_CITY_SPRING_X+6;
    g.hero.y=QS_CITY_SPRING_TOP-20;
    g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;g.hero.hearts=2;
    return g;
}
/* Genuine per-tick game inputs; after fixture initialization no
 * teleporting, flag mutation, or artificial velocity is allowed. */
static RouteWitness play(int use_pause,int jump_from_ledge) {
    QsGame g=final_segment();
    RouteWitness w={0};
    int paused_once=0, launched=0, earned=0, landed=0, jumped=0;
    for(int tick=1;tick<=260;tick++) {
        QsInput in=idle();
        if(tick==1) {
            CHECK(!g.city_air_reward_collected);
            CHECK(g.city_best_clear_frames==0);
        }
        if(use_pause && earned && !paused_once) {
            QsInput chord=idle();chord.back=chord.change=1;
            int frames_before=g.city_attempt_frames;
            float hero_x=g.hero.x,hero_y=g.hero.y;
            float cam=g.camera_x;
            qs_step(&g,chord);
            CHECK(g.city_paused==1);
            for(int hold=0;hold<27;hold++) {
                qs_step(&g,idle());
                CHECK(g.hero.x==hero_x && g.hero.y==hero_y);
                CHECK(g.camera_x==cam);
                CHECK(g.city_attempt_frames==frames_before);
                CHECK(g.city_air_reward_collected==1);
            }
            qs_step(&g,chord);
            CHECK(g.city_paused==0);
            CHECK(g.city_attempt_frames==frames_before);
            w.pause_ticks=29;
            paused_once=1;
        }
        if(landed) {
            in.right=1;
            if(jump_from_ledge && !jumped) {
                in.jump=1;
                jumped=1;
            }
        } else if(earned && g.hero.x < QS_CITY_SKY_LEDGE_X+6) {
            in.right=1;
        }
        qs_step(&g,in);
        if(!launched && g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS) {
            launched=1;
            w.spring_tick=tick;
            CHECK(g.hero.vy==QS_CITY_SPRING_BOUNCE);
        }
        if(!earned && g.city_air_reward_collected) {
            earned=1;
            w.heart_tick=tick;
            CHECK(g.hero.hearts==3);
            CHECK(g.city_air_reward_fx_frames==QS_CITY_AIR_REWARD_FX_TICKS);
        }
        if(earned && !landed && g.hero.grounded &&
           g.hero.y==QS_CITY_SKY_LEDGE_TOP-20) {
            landed=1;
            w.ledge_tick=tick;
            w.landing_x=g.hero.x;
            CHECK(g.hero.x+12>QS_CITY_SKY_LEDGE_X);
            CHECK(g.hero.x<QS_CITY_SKY_LEDGE_X+QS_CITY_SKY_LEDGE_WIDTH);
            CHECK(g.mode==QS_STAGE);
        }
        if(g.mode==QS_MAP) {
            w.exit_tick=tick;
            w.reached_exit=1;
            w.total_frames=g.frames;
            w.hearts_at_exit=g.hero.hearts;
            w.cleared=g.cleared[0];
            w.best_time=g.city_best_clear_frames;
            w.attempt_at_exit=w.best_time;
            w.jump_used=jumped;
            CHECK(g.city_attempt_frames==0);
            CHECK(g.city_air_reward_collected==0); /* map teardown */
            CHECK(g.city_air_reward_fx_frames==0);
            CHECK(g.camera_x==0 && g.render_camera_x==0);
            break;
        }
    }
    CHECK(launched && earned && landed);
    CHECK(w.spring_tick==1);
    CHECK(w.spring_tick<w.heart_tick && w.heart_tick<w.ledge_tick);
    CHECK(w.ledge_tick<w.exit_tick && w.exit_tick<=260);
    CHECK(w.reached_exit && w.cleared==1);
    CHECK(w.hearts_at_exit==3);
    CHECK(w.best_time>0 && w.best_time<=260);
    CHECK(w.jump_used==(jump_from_ledge!=0));
    CHECK(paused_once==(use_pause!=0));
    return w;
}
static void route_finishes_by_walking_off_ledge(void) {
    RouteWitness w=play(0,0);
    CHECK(w.pause_ticks==0 && w.jump_used==0);
}
static void route_finishes_by_jumping_from_ledge(void) {
    RouteWitness w=play(0,1);
    CHECK(w.jump_used==1);
}
static void paused_route_excludes_training_pause_from_timer(void) {
    RouteWitness base=play(0,0), paused=play(1,0);
    CHECK(paused.pause_ticks==29);
    CHECK(paused.spring_tick==base.spring_tick);
    CHECK(paused.heart_tick==base.heart_tick);
    CHECK(paused.ledge_tick==base.ledge_tick);
    CHECK(paused.exit_tick==base.exit_tick);
    CHECK(paused.best_time==base.best_time);
    CHECK(paused.total_frames==base.total_frames+29);
}
static void identical_input_sequences_are_replayable(void) {
    RouteWitness a=play(1,1),b=play(1,1);
    CHECK(a.spring_tick==b.spring_tick && a.heart_tick==b.heart_tick);
    CHECK(a.ledge_tick==b.ledge_tick && a.exit_tick==b.exit_tick);
    CHECK(a.pause_ticks==b.pause_ticks);
    CHECK(a.best_time==b.best_time && a.total_frames==b.total_frames);
    CHECK(a.landing_x==b.landing_x);
}
static void early_back_abandons_without_completion(void) {
    QsGame g=final_segment();
    QsInput in=idle();
    qs_step(&g,in); /* launches */
    for(int i=0;i<40 && !g.city_air_reward_collected;i++)
        qs_step(&g,idle());
    CHECK(g.city_air_reward_collected);
    in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.cleared[0]==0);
    CHECK(g.city_best_clear_frames==0 && g.city_attempt_frames==0);
    CHECK(g.city_air_reward_collected==0);
}
static void reached_exit_cannot_be_refuted_by_simultaneous_back(void) {
    QsGame g=final_segment();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE;
    QsInput in=idle();in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.cleared[0]==1);
    CHECK(g.city_best_clear_frames==1);
}
static void untouched_profile_has_no_reference_route(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=QS_CITY_SPRING_X+6;
    g.hero.y=QS_CITY_SPRING_TOP-20;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    g.hero.hearts=2;
    qs_step(&g,idle());
    CHECK(g.hero.vy!=QS_CITY_SPRING_BOUNCE);
    CHECK(g.city_air_reward_collected==0);
    CHECK(g.city_air_reward_fx_frames==0);
}
int main(void) {
    route_finishes_by_walking_off_ledge();
    route_finishes_by_jumping_from_ledge();
    paused_route_excludes_training_pause_from_timer();
    identical_input_sequences_are_replayable();
    early_back_abandons_without_completion();
    reached_exit_cannot_be_refuted_by_simultaneous_back();
    untouched_profile_has_no_reference_route();
    RouteWitness w=play(0,0);
    printf("PASS: 7 final-route scenarios, spring=%d heart=%d ledge=%d exit=%d\n",
           w.spring_tick,w.heart_tick,w.ledge_tick,w.exit_tick);
    return EXIT_SUCCESS;
}
