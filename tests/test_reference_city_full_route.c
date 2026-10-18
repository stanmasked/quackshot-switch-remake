#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
  fprintf(stderr,"full city route FAIL line %d: %s\n",__LINE__,#x); \
  exit(EXIT_FAILURE); } } while(0)

typedef struct {
    int pit_jump, roof_jump, checkpoint_tick, crate_fire, crate_broken;
    int beacon_a, beacon_b, spring_tick, heart_tick, sky_ledge_tick;
    int finish_tick, best_clear, best_checkpoint, best_sector;
    int frames, pause_added, max_hearts, final_cleared, got_heart;
    int bonus_points, red_unlocked, deaths, initial_foes;
    float max_x, ledge_x;
} FullWitness;

static QsInput idle(void) { QsInput in={0};return in; }

/* Stage 87: ONE continuous attempt starting at the actual stage spawn.
 * Only qs_step(QsInput) moves the hero. No position/velocity/terrain,
 * enemy, health or reward mutations after qs_begin_stage. */
static FullWitness drive_from_spawn(int pause_at_checkpoint) {
    QsGame g;FullWitness w={0};
    int pit_hold=0,roof_hold=0,crate_fired=0;
    int sprung=0,claimed=0,on_ledge=0,paused=0;
    int previous_mask=0,previous_crate=1;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    CHECK(g.mode==QS_STAGE && g.hero.x==40 && g.hero.y==188);
    CHECK(g.city_attempt_frames==0 && !g.city_checkpoint_active);
    CHECK(g.city_crate_active==1 && g.city_air_reward_collected==0);
    for(int k=0;k<3;k++)w.initial_foes+=g.enemies[k].active==1;
    CHECK(w.initial_foes==3 && g.pickups[0].active && g.pickups[1].active);
    w.max_hearts=g.hero.hearts;
    w.max_x=g.hero.x;
    for(int t=1;t<=1700;t++) {
        QsInput in=idle();
        in.right=1;
        /* Reuse the tested stage-86 real-input jump policy, while
         * keeping all enemy patrols and obstacles active. */
        if(!w.pit_jump && g.hero.x>=278 && g.hero.x<320 && g.hero.grounded) {
            w.pit_jump=t;pit_hold=15;
        }
        if(w.pit_jump && !w.roof_jump &&
           g.hero.x>=394 && g.hero.x<=425 && g.hero.grounded) {
            w.roof_jump=t;roof_hold=15;
        }
        if(pit_hold>0) {in.jump=1;--pit_hold;}
        else if(roof_hold>0) {in.jump=1;--roof_hold;}
        /* Avoid overhead terrain by shooting the 16px floor crate
         * rather than jumping into the underside of the city roof. */
        if(!crate_fired && g.city_checkpoint_active &&
           g.hero.x>=902 && g.hero.x<940 &&
           g.city_crate_active==1) {
            in.fire=1;crate_fired=1;w.crate_fire=t;
        }
        /* Once sprung, steer towards the heart and then align above
         * the ledge; after landing, continue right into the exit. */
        if(sprung && !on_ledge) {
            in.right=0;
            if(!claimed && g.hero.x<QS_CITY_AIR_REWARD_X-1) in.right=1;
            if(claimed && g.hero.x<QS_CITY_SKY_LEDGE_X+6) in.right=1;
        }
        if(on_ledge) in.right=1;
        /* Use the built-in training pause after checkpoint activation
         * without advancing stage time or changing game state. */
        if(pause_at_checkpoint && !paused && g.city_checkpoint_active) {
            QsInput chord=idle();chord.back=chord.change=1;
            int before=g.city_attempt_frames,frames=g.frames;
            float x=g.hero.x,y=g.hero.y,camera=g.camera_x;
            qs_step(&g,chord);
            CHECK(g.city_paused==1 && g.city_attempt_frames==before);
            for(int k=0;k<19;k++){
                qs_step(&g,idle());
                CHECK(g.city_attempt_frames==before);
                CHECK(g.hero.x==x && g.hero.y==y && g.camera_x==camera);
            }
            qs_step(&g,chord);
            CHECK(g.city_paused==0 && g.city_attempt_frames==before);
            CHECK(g.frames==frames+21);
            w.pause_added=21;
            paused=1;
        }
        int hearts_before=g.hero.hearts;
        qs_step(&g,in);
        if(g.hero.x>w.max_x)w.max_x=g.hero.x;
        if(g.hero.hearts>w.max_hearts)w.max_hearts=g.hero.hearts;
        if(g.hero.x<40 || g.hero.hearts<1 || g.hero.hearts>3) {
            fprintf(stderr,"FULL_ROUTE_INVALID t=%d x=%f y=%f hearts=%d\n",
                    t,g.hero.x,g.hero.y,g.hero.hearts);
            exit(1);
        }
        if(g.city_checkpoint_active && !w.checkpoint_tick) {
            w.checkpoint_tick=t;
            CHECK(g.city_checkpoint_split_frames==g.city_attempt_frames);
        }
        if(!w.beacon_a && (g.city_bonus_mask&1))w.beacon_a=t;
        if(!w.beacon_b && (g.city_bonus_mask&2))w.beacon_b=t;
        if(previous_crate && !g.city_crate_active)w.crate_broken=t;
        previous_crate=g.city_crate_active;
        if(!sprung && g.city_spring_flash_frames==QS_CITY_SPRING_FLASH_TICKS) {
            sprung=1;w.spring_tick=t;
            CHECK(g.hero.vy==QS_CITY_SPRING_BOUNCE);
        }
        if(!claimed && g.city_air_reward_collected) {
            claimed=1;w.heart_tick=t;
            CHECK(g.city_air_reward_fx_frames==QS_CITY_AIR_REWARD_FX_TICKS);
        }
        if(claimed && !on_ledge && g.hero.grounded &&
           g.hero.y==QS_CITY_SKY_LEDGE_TOP-20) {
            on_ledge=1;w.sky_ledge_tick=t;w.ledge_x=g.hero.x;
        }
        if(g.mode==QS_MAP) {
            CHECK(g.cleared[0]==1);
            w.finish_tick=t;
            w.best_clear=g.city_best_clear_frames;
            w.best_checkpoint=g.city_best_checkpoint_frames;
            w.best_sector=g.city_best_sector_frames;
            w.frames=g.frames;
            w.final_cleared=g.cleared[0];
            w.bonus_points=g.city_best_bonus_score;
            w.got_heart=claimed;
            w.red_unlocked=g.red_unlocked;
            CHECK(g.city_attempt_frames==0 && !g.city_paused);
            CHECK(g.city_checkpoint_active==0);
            CHECK(g.city_air_reward_collected==0);
            CHECK(g.city_air_reward_fx_frames==0);
            CHECK(g.camera_x==0 && g.render_camera_x==0);
            /* Re-entry uses the same game/menu input path. */
            qs_step(&g,idle());
            QsInput reenter=idle();reenter.select=reenter.dash=1;
            qs_step(&g,reenter);
            CHECK(g.mode==QS_STAGE && g.stage==0);
            CHECK(g.city_attempt_frames==0 && !g.city_checkpoint_active);
            CHECK(g.city_crate_active==1 && !g.city_air_reward_collected);
            CHECK(g.city_best_clear_frames==w.best_clear);
            CHECK(g.cleared[0]==1);
            break;
        }
        if(t==1700)fprintf(stderr,
          "FULL_ROUTE_INCOMPLETE x=%f y=%f vx=%f vy=%f max=%f checkpoint=%d crate=%d fired=%d sprung=%d heart=%d ledge=%d bonuses=%d frames=%d hearts=%d dead=%d\n",
           g.hero.x,g.hero.y,g.hero.vx,g.hero.vy,w.max_x,
           w.checkpoint_tick,g.city_crate_active,w.crate_fire,
           w.spring_tick,w.heart_tick,w.sky_ledge_tick,g.city_bonus_mask,
           g.city_attempt_frames,g.hero.hearts,w.deaths);
        if(g.mode!=QS_STAGE)CHECK(0);
        if(g.hero.hearts<hearts_before)++w.deaths; /* contact witness */
        previous_mask=g.city_bonus_mask;
    }
    (void)previous_mask;
    CHECK(w.pit_jump>0 && w.roof_jump>w.pit_jump);
    CHECK(w.checkpoint_tick>w.roof_jump);
    CHECK(w.crate_fire>w.checkpoint_tick);
    CHECK(w.crate_broken>=w.crate_fire);
    CHECK(w.spring_tick>w.crate_broken);
    CHECK(w.heart_tick>w.spring_tick);
    CHECK(w.sky_ledge_tick>w.heart_tick);
    CHECK(w.finish_tick>w.sky_ledge_tick);
    CHECK(w.final_cleared && w.best_clear>0 && w.best_clear<=1700);
    CHECK(w.best_checkpoint>0 && w.best_sector>0);
    CHECK(w.pause_added==(pause_at_checkpoint?21:0));
    return w;
}
static void all_obstacles_are_crossed_in_one_run(void) {
    FullWitness w=drive_from_spawn(0);
    CHECK(w.max_x>=(QS_COLUMNS-4)*QS_TILE);
    CHECK(w.got_heart && w.initial_foes==3);
}
static void pause_only_changes_wall_clock(void) {
    FullWitness base=drive_from_spawn(0),pause=drive_from_spawn(1);
    CHECK(base.pit_jump==pause.pit_jump);
    CHECK(base.roof_jump==pause.roof_jump);
    CHECK(base.checkpoint_tick==pause.checkpoint_tick);
    CHECK(base.crate_fire==pause.crate_fire);
    CHECK(base.spring_tick==pause.spring_tick);
    CHECK(base.heart_tick==pause.heart_tick);
    CHECK(base.sky_ledge_tick==pause.sky_ledge_tick);
    CHECK(base.finish_tick==pause.finish_tick);
    CHECK(base.best_clear==pause.best_clear);
    CHECK(pause.frames==base.frames+21);
}
static void repeats_are_reproducible(void) {
    FullWitness a=drive_from_spawn(0),b=drive_from_spawn(0);
    CHECK(a.finish_tick==b.finish_tick);
    CHECK(a.checkpoint_tick==b.checkpoint_tick);
    CHECK(a.crate_broken==b.crate_broken);
    CHECK(a.heart_tick==b.heart_tick);
    CHECK(a.sky_ledge_tick==b.sky_ledge_tick);
    CHECK(a.best_clear==b.best_clear && a.best_sector==b.best_sector);
    CHECK(a.ledge_x==b.ledge_x && a.max_x==b.max_x);
}
int main(void) {
    all_obstacles_are_crossed_in_one_run();
    pause_only_changes_wall_clock();
    repeats_are_reproducible();
    FullWitness w=drive_from_spawn(0);
    printf("PASS: 3 continuous city route scenarios, checkpoint=%d crate=%d spring=%d heart=%d ledge=%d finish=%d\n",
           w.checkpoint_tick,w.crate_broken,w.spring_tick,w.heart_tick,
           w.sky_ledge_tick,w.finish_tick);
    return EXIT_SUCCESS;
}
