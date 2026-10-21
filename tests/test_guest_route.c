#include "qs_route_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"guest route test failed: line %d\n",__LINE__);exit(1);}}while(0)
int main(void){
    const char *expected[]={"SPAWN","CHECKPOINT","CRATE_FIRE",
      "CRATE_BROKEN","SPRING","HEART","LEDGE","CLEAR","REENTRY"};
    FILE *f=tmpfile();
    char buf[256],event[60],source[80];
    unsigned seq;int tick,attempt,x,y,detail,i=0,last_tick=0,clear_time=0;
    CHECK(f!=NULL);
    CHECK(qs_route_probe_write(f));
    rewind(f);
    CHECK(fgets(buf,sizeof(buf),f)!=NULL);
    CHECK(strcmp(buf,"seq,event,tick,attempt,x_milli,y_milli,detail,source\n")==0);
    while(fgets(buf,sizeof(buf),f)) {
        CHECK(i<9);
        CHECK(sscanf(buf,"%u,%59[^,],%d,%d,%d,%d,%d,%79s",
                     &seq,event,&tick,&attempt,&x,&y,&detail,source)==8);
        CHECK(seq==(unsigned)i && strcmp(expected[i],event)==0);
        CHECK(strcmp(source,"qs_step_simulated_inputs")==0);
        CHECK(tick>=last_tick && attempt>=0 && x>=0 && y>=0);
        if(i==0)CHECK(tick==0 && x==40000 && y==188000 && detail==3);
        if(i==1)CHECK(detail==attempt && detail>0);
        if(i==4)CHECK(detail==12);
        if(i==5)CHECK(detail==1);
        if(i==7){CHECK(detail>0);clear_time=detail;}
        if(i==8)CHECK(attempt==0 && x==40000 && detail==clear_time);
        last_tick=tick;i++;
    }
    CHECK(i==9 && last_tick<1700);
    fclose(f);
    puts("PASS: NRO route witness generated 9 ordered guest events");
    return 0;
}
