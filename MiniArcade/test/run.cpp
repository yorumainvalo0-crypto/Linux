/* Host test harness for the ESP-IDF build.
   Runs the real game code AND the real platform layer (arcade.cpp); only the
   hardware calls are redirected here. The screen is decoded from the bytes the
   SSD1306 driver puts on the I2C bus, so what the tests see is what the panel
   would show. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <map>
#include <set>
#include <string>
#include <vector>

std::map<std::string,uint16_t>     simNvsU16;
std::map<std::string,std::string>  simNvsBlob;

static uint64_t clockUs = 0;
static uint8_t  pinMode_[64], pinWire[64], pinPress[64];
static uint8_t  screen[64][128];
static long     frames = 0, maxInk = 0;
static long     toneCount = 0;
static int      lastTone = -1;
static bool     simHasBattery = true;
static std::set<std::string> seenTexts;
static std::string scenario;
static uint8_t  WIRED[5] = {3,4,5,6,7};
struct PinEv { uint32_t t; uint8_t pin; uint8_t on; };
static std::vector<std::pair<uint32_t,uint8_t>> script;
static std::vector<PinEv> pinScript;
static std::set<int> captureAt;
static bool autoplay = false;
struct SimEnd {};
static uint32_t simEnd = 0;

uint64_t simMicros(){ return clockUs; }
void simGpioConfig(uint64_t mask,int pu,int pd){
  for(int p=0;p<64;p++) if(mask>>p&1) pinMode_[p] = pd?3:(pu?2:0); }
int simGpioLevel(int p){
  if(pinWire[p]==3) return 1;                 // external pull-up on the board
  if(pinWire[p]==4) return 0;                 // external pull-down on the board
  if(pinPress[p]){ if(pinWire[p]==1) return 0; if(pinWire[p]==2) return 1; }
  return pinMode_[p]==3 ? 0 : 1; }            // otherwise the internal resistor wins
static int grabNext = 0;      // capture the next frame after a marker string
void simCpuMhz(int m){ printf("      cpu clock set to %d MHz\n", m); }
void simDeepSleep();
void simTone(int freq){ if(freq){ toneCount++; lastTone=freq; } }
static int  simBatPin = 0, simBatMv = 3800;   // cell behind two equal resistors
static int  simGhostPin = -1;                  // floating pin that holds ~1.9 V until pulled down
static int  simHighPin = -1;                   // divider on the 5 V booster output
static int  simBatStart = -1;                  // batPin before the scenario rewires it
static int  simMvRaw(int mv){ return mv * 4095 / 2500; }
int  simAdcRaw(int ch){
  bool down = pinMode_[ch]==3;                 // internal pull-down (~45k) against 28k
  if(ch==simBatPin && simHasBattery) return simMvRaw(simBatMv/2 * (down?62:100)/100);
  if(ch==simHighPin) return simMvRaw(2500 * (down?62:100)/100);
  if(ch==simGhostPin) return down ? 5 : simMvRaw(1900);
  if(down) return 5;
  return 300 + (int)(clockUs/1000%400);        // floating pin: drifts
}
void arcadeTraceStr(const char *s){
  seenTexts.insert(s);
  const char *mark = getenv("GRAB");
  if(mark && strstr(s, mark)) grabNext = 1;
}

static void applyMask(uint8_t m){ for(int i=0;i<5;i++) pinPress[WIRED[i]] = (m>>i&1); }

static bool simSlept = false;
void simDeepSleep(){ simSlept = true; printf("      board went to deep sleep\n"); throw SimEnd{}; }

static void (*simTimeHook)(uint32_t ms) = NULL;   // a scenario's own events (every ms)

void simDelayMs(uint32_t ms){
  for(uint32_t i=0;i<ms;i++){
    clockUs += 1000;
    uint32_t t=(uint32_t)(clockUs/1000);
    for(auto&e:script)    if(e.first==t) applyMask(e.second);
    for(auto&e:pinScript) if(e.t==t)     pinPress[e.pin]=e.on;
    if(autoplay && t>2000 && t%200==0){ static const uint8_t o[]={0,0,4,8,1,2}; applyMask(o[rand()%6]); }
    if(simTimeHook) simTimeHook(t);
  }
  if(clockUs/1000 > simEnd) throw SimEnd{};
}

// ---- frame based auto players, reading the decoded panel image ----
static void pongBot(){
  int pTop=-1, bally=-1;
  for(int y=17;y<64;y++) if(screen[y][3]){ pTop=y; break; }
  for(int y=17;y<64&&bally<0;y++) for(int x=8;x<120;x++) if(screen[y][x]){ bally=y; break; }
  if(pTop<0||bally<0){ applyMask(0); return; }
  int c=pTop+6;
  applyMask(bally<c-1?1:(bally>c+1?2:0));
}
static void flappyBot(){
  int bird=-1;
  for(int y=17;y<64;y++) if(screen[y][25]){ bird=y+2; break; }
  int col=-1;
  for(int x=32;x<126&&col<0;x++){ int n=0; for(int y=17;y<64;y++) n+=screen[y][x]; if(n>=3) col=x+2; }
  if(bird<0||col<0||col>127){ applyMask(0); return; }
  int bA=-1,bB=-1,run=-1;
  for(int y=17;y<=64;y++){
    bool set=(y==64)||screen[y][col];
    if(!set){ if(run<0) run=y; }
    else if(run>=0){ if(bA<0||y-run>bB-bA){ bA=run; bB=y; } run=-1; }
  }
  if(bA<0){ applyMask(0); return; }
  applyMask(bird>(bA+bB)/2 ? 1 : 0);
}
static void tunnelBot(){
  /* The nearest ring dominates the picture: take the bounding box of all lit
     pixels as the tube opening, then push away from whatever fills it.    */
  int x0=999,x1=-1,y0=999,y1=-1; long sx=0,sy=0,n=0;
  for(int y=17;y<64;y++) for(int x=0;x<128;x++) if(screen[y][x]){
    if(x<x0)x0=x; if(x>x1)x1=x; if(y<y0)y0=y; if(y>y1)y1=y;
    sx+=x; sy+=y; n++;
  }
  if(n<10||x1<=x0){ applyMask(0); return; }
  int bcx=(x0+x1)/2, bcy=(y0+y1)/2;
  int ccx=(int)(sx/n), ccy=(int)(sy/n);
  int tx=bcx + (bcx-ccx), ty=bcy + (bcy-ccy);       // away from the filled side
  uint8_t m=0;
  if(tx<62) m|=4; else if(tx>66) m|=8;
  if(ty<38) m|=1; else if(ty>42) m|=2;
  applyMask(m);
}

static void dinoBot(){          // jump over cacti, duck under birds
  bool low=false, high=false;
  for(int x=24;x<62;x++){
    for(int y=45;y<61;y++) if(screen[y][x]) low=true;      // cactus height
    for(int y=33;y<45;y++) if(screen[y][x]) high=true;     // pterodactyl height
  }
  /* pulse the key: a button that is still held when a round starts is muted
     by the firmware until it is released, so the bot must let go now and then */
  static int phase = 0;
  phase++;
  if(low)       applyMask((phase % 4 < 2) ? 1 : 0);   // UP
  else if(high) applyMask(2);                         // DOWN
  else          applyMask(0);
}
static void breakoutBot(){      // move the paddle under the ball
  int ballx=-1,bally=-1,pad=-1;
  for(int y=58;y<64;y++) for(int x=0;x<128;x++) if(screen[y][x]){ pad=x; goto padDone; }
  padDone:
  for(int y=56;y>=17;y--) for(int x=0;x<128;x++) if(screen[y][x]){ ballx=x; bally=y; goto ballDone; }
  ballDone:
  if(ballx<0||pad<0){ applyMask(0); return; }
  applyMask(ballx<pad+8 ? 4 : (ballx>pad+16 ? 8 : 0));
}
static void racerBot(){         // steer to the middle of the road
  int l=-1,r=-1;
  for(int x=0;x<128;x++) if(screen[46][x]){ if(l<0) l=x; r=x; }
  if(l<0||r<=l){ applyMask(0); return; }
  int mid=(l+r)/2, car=-1, run=0;
  for(int x=0;x<128;x++){                    // the player car is a solid 7 px run
    if(screen[58][x]){ if(++run>=5){ car=x-run+1; break; } }
    else run=0;
  }
  if(car<0){ applyMask(0); return; }
  bool blocked=false;                        // something in the lane ahead?
  for(int y=18;y<50&&!blocked;y++) for(int x=car-1;x<car+9;x++) if(x>=0&&x<128&&screen[y][x]) blocked=true;
  if(blocked){ applyMask(car+3>mid ? 4 : 8); return; }   // dodge towards the wider side
  applyMask(car+3<mid-2 ? 8 : (car+3>mid+2 ? 4 : 0));
}

void simFrameSent(const uint8_t* d, size_t n){
  if(n < 1000 || d[0] != 0x40) return;                 // not a pixel transfer
  frames++;
  for(int page=0;page<8;page++)
    for(int x=0;x<128;x++){
      uint8_t col=d[1+page*128+x];
      for(int b=0;b<8;b++) screen[page*8+b][x]=(col>>b)&1;
    }
  long ink=0; for(int y=16;y<64;y++) for(int x=0;x<128;x++) ink+=screen[y][x];
  if(ink>maxInk) maxInk=ink;
  uint32_t t=(uint32_t)(clockUs/1000);
  if(scenario=="pong"   && t>2200 && t<25000) pongBot();
  if(scenario=="flappy" && t>2800 && t<26000) flappyBot();
  if(scenario=="tunnel"   && t>2900 && t<28000) tunnelBot();
  if(scenario=="dino"     && t>3400 && t<40000) dinoBot();
  if(scenario=="breakout" && t>3200 && t<40000) breakoutBot();
  if(scenario=="racer"    && t>3600 && t<40000) racerBot();
  if(grabNext){ grabNext = 0; captureAt.insert(frames); }
  if(captureAt.count(frames)){
    char fn[64]; snprintf(fn,sizeof(fn),"frames/%s_%04ld.txt",scenario.c_str(),frames);
    FILE*fp=fopen(fn,"w");
    for(int y=0;y<64;y++){ for(int x=0;x<128;x++) fputc(screen[y][x]?'#':'.',fp); fputc('\n',fp); }
    fclose(fp);
  }
}

#include "Arduino.h"
#include <time.h>
#include "net.h"             // pretend network, before the sketch finds main/net.h
#include "link.h"            // multiplayer with a bot next door
#include "MiniArcade.ino"
#include "sksolve.h"
#include "backupfmt.h"

static bool saw(const char*s){ for(auto&t:seenTexts) if(t.find(s)!=std::string::npos) return true; return false; }
static uint16_t maxScore(const char*p){
  uint16_t m=0;
  for(auto&t:seenTexts){ if(t.rfind(p,0)!=0) continue; int v=atoi(t.c_str()+strlen(p)); if(v>(int)m) m=v; }
  return m;
}
static int fails=0;
static void check(const char*w,bool ok){ printf("  [%s] %s\n", ok?"OK":"FAIL", w); if(!ok) fails++; }
static void downs(int k, uint32_t t0=1300, uint32_t step=250){
  for(int i=0;i<k;i++){ script.push_back({t0+i*step,2}); script.push_back({t0+i*step+60,0}); }
}
// the library wraps: 1 up = Settings, 2 = Stats, 3 = Multiplayer
static void ups(int k, uint32_t t0=1300, uint32_t step=250){
  for(int i=0;i<k;i++){ script.push_back({t0+i*step,1}); script.push_back({t0+i*step+60,0}); }
}

int main(int argc,char**argv){
  scenario = argc>1?argv[1]:"menu";
  srand(7);
  for(int i=0;i<64;i++){ pinMode_[i]=2; pinWire[i]=0; pinPress[i]=0; }
  for(int i=0;i<5;i++) pinWire[WIRED[i]]=2;                 // buttons wired to 3V3
  if(scenario.rfind("wizard",0)!=0){
    simNvsU16["pset"]=2;
    simNvsU16["sset"]=1;  simNvsU16["snd"]=10;      // buzzer already known
    for(int i=0;i<5;i++){ simNvsU16["p"+std::to_string(i)]=3+i; simNvsU16["a"+std::to_string(i)]=1; }
  }

  if(scenario=="menu"){        simEnd=10000; downs(GAME_COUNT-1,1300,300); captureAt={30,300,700};
  } else if(scenario=="tetris"){ simEnd=60000; script={{1300,16},{1360,0}}; autoplay=true; captureAt={200,900};
  } else if(scenario=="snake"){  simEnd=20000; downs(1);
      script.push_back({1500,16}); script.push_back({1560,0});
      script.push_back({9000,16}); script.push_back({9060,0});     // restart after death
      script.push_back({15000,16}); script.push_back({16200,0});   // hold OK -> pause menu
      script.push_back({17000,16}); script.push_back({18200,0});   // hold again -> library
      captureAt={200,1000};
  } else if(scenario=="pong"){   simEnd=40000; downs(2);
      script.push_back({1700,16}); script.push_back({1760,0}); captureAt={300,900};
  } else if(scenario=="doom"){   simEnd=60000; downs(3);
      script.push_back({1900,16}); script.push_back({1960,0});
      for(uint32_t t=3000;t<57000;t+=400){           // sweep slowly and keep firing
        script.push_back({t,8});                      // short turn
        script.push_back({t+120,1});                  // step forward
        script.push_back({t+220,0});
        script.push_back({t+260,16});                 // OK tap = shoot
        script.push_back({t+330,0}); }
      captureAt={400,1500};
  } else if(scenario=="mine"){   simEnd=30000; downs(4);
      script.push_back({2300,16}); script.push_back({2360,0});
      for(uint32_t t=3500;t<20000;t+=1000){
        script.push_back({t,8}); script.push_back({t+400,0});
        script.push_back({t+500,16}); script.push_back({t+560,0});
        script.push_back({t+700,1}); script.push_back({t+780,0}); }
      script.push_back({26000,16}); script.push_back({27500,0});   // hold OK -> pause menu
      script.push_back({27800,2});  script.push_back({27860,0});   // "quit to menu"
      script.push_back({28100,2});  script.push_back({28160,0});
      script.push_back({28400,16}); script.push_back({28460,0});   // -> save & exit
      captureAt={900,3000};
  } else if(scenario=="tunnel"){ simEnd=30000; downs(5,1300,200);
      script.push_back({2400,16}); script.push_back({2460,0}); captureAt={620,700,820,950};
  } else if(scenario=="flappy"){ simEnd=30000; downs(6,1300,200);
      script.push_back({2600,16}); script.push_back({2660,0}); captureAt={400,1200};
  } else if(scenario=="invaders"){ simEnd=40000; downs(7,1300,200);
      script.push_back({2800,16}); script.push_back({2860,0});
      for(uint32_t t=3000;t<36000;t+=800){
        script.push_back({t,4}); script.push_back({t+300,0});
        script.push_back({t+350,16}); script.push_back({t+420,0});
        script.push_back({t+500,8}); script.push_back({t+750,0}); }
      captureAt={620,700,800,950};
  } else if(scenario=="dino"){     simEnd=45000; downs(8,1300,180);
      script.push_back({3000,16}); script.push_back({3060,0});
      captureAt={620,700,780,900};
  } else if(scenario=="breakout"){ simEnd=45000; downs(9,1300,180);
      script.push_back({3100,16}); script.push_back({3160,0});
      captureAt={900,1500};
  } else if(scenario=="rocks"){    simEnd=45000; downs(10,1300,180);
      script.push_back({3300,16}); script.push_back({3360,0});
      for(uint32_t t=4000;t<42000;t+=500){        // spin and fire
        script.push_back({t,8}); script.push_back({t+200,0});
        script.push_back({t+260,16}); script.push_back({t+330,0}); }
      captureAt={900,1500};
  } else if(scenario=="racer"){    simEnd=45000; downs(11,1300,180);
      script.push_back({3500,16}); script.push_back({3560,0});
      captureAt={700,720,745,770};
  } else if(scenario=="frogger"){  simEnd=45000; downs(12,1300,180);
      script.push_back({3700,16}); script.push_back({3760,0});     // open frogger
      script.push_back({4000,2});  script.push_back({4060,0});     // choose "normal"
      script.push_back({4300,16}); script.push_back({4360,0});
      for(uint32_t t=5000;t<42000;t+=700){        // hop forward, sometimes sideways
        script.push_back({t,1}); script.push_back({t+120,0});
        script.push_back({t+300,8}); script.push_back({t+380,0}); }
      captureAt={760,860};
  } else if(scenario=="c4"){       simEnd=60000; downs(13,1300,180);
      script.push_back({3900,16}); script.push_back({3960,0});     // open 4 wins
      script.push_back({4200,2});  script.push_back({4260,0});     // "1P normal"
      script.push_back({4500,16}); script.push_back({4560,0});
      for(uint32_t t=5200, k=0; t<58000; t+=900, k++){   // wander over the columns
        uint8_t dir = (k%3==0) ? 4 : 8;                  // sometimes left, mostly right
        script.push_back({t,dir});        script.push_back({t+90,0});
        if(k%2){ script.push_back({t+140,dir}); script.push_back({t+230,0}); }
        script.push_back({t+400,16});     script.push_back({t+480,0}); }
      captureAt={790,1400};
  } else if(scenario=="ttt"){      simEnd=60000; downs(14,1300,180);
      script.push_back({4200,16}); script.push_back({4260,0});     // open tic tac toe
      script.push_back({4600,16}); script.push_back({4660,0});     // "1P easy"
      for(uint32_t t=5200;t<58000;t+=260){                       // wander and place
        static const uint8_t keys[4]={1,2,4,8};
        script.push_back({t,keys[rand()%4]}); script.push_back({t+60,0});
        script.push_back({t+130,16}); script.push_back({t+190,0}); }
      captureAt={};
  } else if(scenario=="g2048"){   simEnd=60000; downs(15,1300,180);
      script.push_back({4200,16}); script.push_back({4260,0});
      for(uint32_t t=4600;t<55000;t+=150){                        // mash the arrows
        static const uint8_t keys[4]={1,2,4,8};
        script.push_back({t,keys[rand()%4]}); script.push_back({t+50,0}); }
      captureAt={1500,2500};
  } else if(scenario=="mines"){   simEnd=60000; downs(16,1300,180);
      script.push_back({4400,16}); script.push_back({4460,0});     // open minesweeper
      script.push_back({4800,16}); script.push_back({4860,0});     // easy
      for(uint32_t t=5500;t<55000;t+=700){                        // wander, open, sometimes flag
        static const uint8_t keys[4]={1,2,4,8};
        script.push_back({t,keys[rand()%4]}); script.push_back({t+60,0});
        script.push_back({t+150,16}); script.push_back({t+200,0});
        if(rand()%5==0){ script.push_back({t+300,16}); script.push_back({t+350,0}); } }
      captureAt={1500,1700};
  } else if(scenario=="pacman"){  simEnd=90000; downs(17,1300,180);
      script.push_back({4600,16}); script.push_back({4660,0});
      for(uint32_t t=5000;t<85000;t+=400){                        // wander
        static const uint8_t keys[4]={1,2,4,8};
        script.push_back({t,keys[rand()%4]}); script.push_back({t+60,0}); }
      captureAt={1100,1600,2600};
  } else if(scenario=="shooter"){ simEnd=60000; downs(18,1300,180);
      script.push_back({4800,16}); script.push_back({4860,0});
      for(uint32_t t=5200;t<58000;t+=500){                        // weave up and down
        script.push_back({t,(uint8_t)(rand()%2?1:2)}); script.push_back({t+300,0});
        if(rand()%12==0){ script.push_back({t+350,16}); script.push_back({t+400,0}); } }   // a bomb now and then
      captureAt={1500,2500};
  } else if(scenario=="jump"){    simEnd=60000; downs(19,1300,180);
      script.push_back({5000,16}); script.push_back({5060,0});
      for(uint32_t t=5400;t<58000;t+=600){                        // run right, jump now and then
        script.push_back({t,8}); script.push_back({t+250,9}); script.push_back({t+400,8}); script.push_back({t+580,0}); }
      captureAt={1500,2500};
  } else if(scenario=="sokoban"){ simEnd=20000; downs(20,1300,150);
      script.push_back({5000,16}); script.push_back({5060,0});     // level list
      script.push_back({5500,16}); script.push_back({5560,0});     // play level 1
      static const uint8_t mv[]={4,4,4,1,4,2,8,8,2,4};             // some steps ...
      for(int i=0;i<10;i++){ script.push_back({6000+i*300,mv[i]}); script.push_back({6060+i*300,0}); }
      for(int i=0;i<3;i++){ script.push_back({9500+i*300,16}); script.push_back({9560+i*300,0}); }   // ... 3 undone
      captureAt={1100,1900};
  } else if(scenario=="battleship"){ simEnd=120000; downs(21,1300,150);
      script.push_back({4600,16}); script.push_back({4660,0});     // open
      script.push_back({5000,1});  script.push_back({5060,0});     // shuffle once
      script.push_back({5400,16}); script.push_back({5460,0});     // ready
      for(uint32_t t=6000;t<115000;t+=450){                       // aim somewhere, fire
        static const uint8_t keys[4]={1,2,4,8};
        script.push_back({t,keys[rand()%4]}); script.push_back({t+60,0});
        script.push_back({t+200,16}); script.push_back({t+260,0}); }
      captureAt={1100,3000};
  } else if(scenario=="bsplace"){ simEnd=13000; downs(21,1300,150);
      auto tap=[&](uint32_t t,uint8_t k){ script.push_back({t,k}); script.push_back({t+60,0}); };
      tap(4600,16);                                                // open battleship
      tap(5000,2);                                                 // place the ships myself
      tap(5400,16);                                                // ship 4 at the top left
      tap(6000,2); tap(6200,2); tap(6400,16);                      // ship 3 two rows lower
      tap(7000,2); tap(7200,2); tap(7400,16);                      // ship 3
      tap(8000,2); tap(8200,2); tap(8400,16);                      // ship 2
      for(int i=0;i<5;i++) tap(9000+i*160,8);                      // last ship 2 further right ...
      tap(10000,16); tap(10150,16);                                // ... turned (double OK) ...
      tap(10600,16);                                               // ... and set down
      tap(11200,16);                                               // go
      captureAt={1100,1600};
  } else if(scenario=="pause"){   simEnd=36000;
      script.push_back({1300,16});  script.push_back({1360,0});    // open tetris
      script.push_back({3000,16});  script.push_back({4000,0});    // hold OK -> pause
      script.push_back({24000,16}); script.push_back({24060,0});   // 20 s later: continue
      script.push_back({26000,16}); script.push_back({27000,0});   // pause again ...
      script.push_back({27300,2});  script.push_back({27360,0});
      script.push_back({27600,16}); script.push_back({27660,0});   // ... restart
      script.push_back({30000,16}); script.push_back({31000,0});   // pause again ...
      script.push_back({31300,2});  script.push_back({31360,0});
      script.push_back({31600,2});  script.push_back({31660,0});
      script.push_back({31900,16}); script.push_back({31960,0});   // ... quit to menu
      captureAt={900};
  } else if(scenario=="stats"){   simEnd=9000; ups(2,1300,200);
      StatBlob1 b; memset(&b,0,sizeof(b)); b.ver=1;                 // as 9.4 stored it
      b.plays[0]=3; b.secs[0]=3725; b.plays[1]=1; b.secs[1]=61; b.awards=1;
      simNvsBlob["stats"]=std::string((const char*)&b,sizeof(b));
      script.push_back({5000,16}); script.push_back({5060,0});     // open stats
      script.push_back({6500,8});  script.push_back({6560,0});     // awards page
      script.push_back({7000,2});  script.push_back({7060,0});     // second award
      captureAt={1000,1300};
  } else if(scenario=="sleep"){   simEnd=90000;
      simNvsU16["slp"]=1;                       // one minute, then nothing happens
      captureAt={200};
  } else if(scenario=="settings"){ simEnd=26000; ups(1,1300,220);
      script.push_back({5000,16}); script.push_back({5060,0});     // open settings
      script.push_back({5600,4});  script.push_back({5660,0});     // brightness down
      script.push_back({5900,4});  script.push_back({5960,0});
      script.push_back({6300,2});  script.push_back({6360,0});     // line: cpu clock
      script.push_back({6700,4});  script.push_back({6760,0});     // slower clock
      script.push_back({7100,2});  script.push_back({7160,0});     // line: sleep
      script.push_back({7500,8});  script.push_back({7560,0});     // longer timeout
      script.push_back({8000,2});  script.push_back({8060,0});     // down to battery
      script.push_back({8300,2});  script.push_back({8360,0});
      script.push_back({8600,2});  script.push_back({8660,0});
      script.push_back({9000,16}); script.push_back({9060,0});     // open battery page
      script.push_back({9600,1});  script.push_back({9660,0});
      script.push_back({9900,1});  script.push_back({9960,0});
      script.push_back({10200,1}); script.push_back({10260,0});
      script.push_back({10500,1}); script.push_back({10560,0});
      script.push_back({10800,1}); script.push_back({10860,0});
      script.push_back({11200,16});script.push_back({11260,0});    // pick GPIO0
      captureAt={1000,1700};
  } else if(scenario=="wlan"){ simEnd=12000; ups(1,1300,220);
      script.push_back({5000,16}); script.push_back({5060,0});     // open settings
      for(int i=0;i<6;i++){ script.push_back({5600+i*300,2}); script.push_back({5660+i*300,0}); }
      script.push_back({7500,16}); script.push_back({7560,0});     // wlan page, joins
      script.push_back({8200,2});  script.push_back({8260,0});     // down to the update line
      script.push_back({8500,2});  script.push_back({8560,0});
      script.push_back({9000,16}); script.push_back({9060,0});     // check for update
      script.push_back({9500,16}); script.push_back({9560,0});     // install it
      captureAt={1000,1100};
  } else if(scenario=="wlanfail"){ simEnd=16000; ups(1,1300,220); simNoNet=true;
      script.push_back({5000,16}); script.push_back({5060,0});     // open settings
      for(int i=0;i<6;i++){ script.push_back({5600+i*300,2}); script.push_back({5660+i*300,0}); }
      script.push_back({7500,16}); script.push_back({7560,0});     // wlan page: joins, but in vain
      captureAt={1600,2800};
  } else if(scenario=="wlanstay"){ simEnd=16000; ups(1,1300,220);
      auto tap=[&](uint32_t t,uint8_t k){ script.push_back({t,k}); script.push_back({t+60,0}); };
      tap(5000,16);                                                // open settings
      for(int i=0;i<6;i++) tap(5600+i*300,2);
      tap(7500,16);                                                // wlan page, joins
      for(int i=0;i<5;i++) tap(8000+i*250,2);                      // down to "stay online"
      tap(9200,16);                                                // -> yes
      simTimeHook=[](uint32_t t){ if(t==10000) simRestoreAsk=true; };   // a backup comes in
      tap(10800,16);                                               // OK on the question
      tap(11500,2); tap(11800,16);                                 // "back"
      script.push_back({12500,16}); script.push_back({13400,0});   // hold OK: settings -> menu
      captureAt={2150,2600};
  } else if(scenario=="wlanplay"){ simEnd=14000; ups(1,1300,220);
      auto tap=[&](uint32_t t,uint8_t k){ script.push_back({t,k}); script.push_back({t+60,0}); };
      tap(5000,16);                                                // open settings
      for(int i=0;i<6;i++) tap(5600+i*300,2);
      tap(7500,16);                                                // wlan page, joins
      for(int i=0;i<3;i++) tap(8000+i*250,2);                      // down to "phone hotspot"
      tap(9000,16);                                                // open it
      tap(9800,4);                                                 // LEFT: new password
      tap(10500,8);                                                // RIGHT: no password
      tap(11200,8);                                                // RIGHT: password again
      tap(11900,16);                                               // OK: stop
      captureAt={2400,2600};
  } else if(scenario=="batfind"){ simEnd=16000; ups(1,1300,220);
      simNvsU16["bat"]=255;                     // older firmware found nothing
      simBatPin=2; simGhostPin=1;               // the real one comes after a trap
      auto tap=[&](uint32_t t,uint8_t k){ script.push_back({t,k}); script.push_back({t+60,0}); };
      tap(5000,16);                                                // open settings
      for(int i=0;i<5;i++) tap(5600+i*300,2);                      // down to battery
      tap(7500,16);                                                // open battery page
      simTimeHook=[](uint32_t t){ if(t==8000){ simBatStart=batPin; simBatPin=0; } };   // rewired to GPIO0
      tap(8500,8);                                                 // RIGHT: search
      tap(12000,16);                                               // OK keeps GPIO0
  } else if(scenario=="bat5v"){ simEnd=12000; ups(1,1300,220);
      simNvsU16["bat"]=254; simHasBattery=false; simHighPin=1;
      auto tap=[&](uint32_t t,uint8_t k){ script.push_back({t,k}); script.push_back({t+60,0}); };
      tap(5000,16);
      for(int i=0;i<5;i++) tap(5600+i*300,2);
      tap(7500,16);
      tap(8500,8);                                                 // RIGHT: search
  } else if(scenario=="batnone"){ simEnd=12000; ups(1,1300,220);
      simNvsU16["bat"]=0;                       // battery on GPIO0 ...
      auto tap=[&](uint32_t t,uint8_t k){ script.push_back({t,k}); script.push_back({t+60,0}); };
      tap(5000,16);
      for(int i=0;i<5;i++) tap(5600+i*300,2);
      tap(7500,16);
      for(int i=0;i<5;i++) tap(8000+i*250,2);                      // ... but "no battery" picked
      tap(9800,16);
  } else if(scenario=="versions"){ simEnd=8000;
      simTwoSlots=true;                                             // fresh update, nothing pressed
      script.push_back({6000,2}); script.push_back({6060,0});      // first key press in the menu
      captureAt={250,500};
  } else if(scenario=="versions2"){ simEnd=8000;
      simTwoSlots=true;
      script.push_back({1600,2}); script.push_back({1660,0});      // pick the previous version
      script.push_back({1900,16}); script.push_back({1960,0});
      captureAt={250};
  } else if(scenario=="versions3"){ simEnd=90000;
      simTwoSlots=true; simNvsU16["slp"]=1;                        // fresh update, sleep after 1 min
  } else if(scenario=="upload"){ simEnd=9000; ups(1,1300,220); simUpload=true;
      script.push_back({5000,16}); script.push_back({5060,0});     // open settings
      for(int i=0;i<6;i++){ script.push_back({5600+i*300,2}); script.push_back({5660+i*300,0}); }
      script.push_back({7500,16}); script.push_back({7560,0});     // wlan page, joins
      script.push_back({8300,16}); script.push_back({8360,0});     // OK on the question
  } else if(scenario=="mp4"||scenario=="mpno"||scenario=="mpwait"||scenario=="mpleft"){
      simEnd = (scenario=="mpwait") ? 45000 : 40000;
      if(scenario=="mpno")   simBotAnswer=0;
      if(scenario=="mpwait") simBotAnswer=-1;
      if(scenario=="mpleft") simBotLeaveAfter=2;
      ups(3,1300,180);
      script.push_back({4300,16}); script.push_back({4360,0});     // open multiplayer
      script.push_back({6500,2});  script.push_back({6560,0});     // ANNA's row
      script.push_back({6900,16}); script.push_back({6960,0});     // challenge ...
      script.push_back({7400,16}); script.push_back({7460,0});     // ... to 4 wins
      if(scenario!="mpwait") for(uint32_t t=10000;t<36000;t+=450){   // play: move and drop
        script.push_back({t,(uint8_t)((rand()%2)?4:8)}); script.push_back({t+60,0});
        script.push_back({t+200,16}); script.push_back({t+260,0}); }
  } else if(scenario=="mppong"||scenario=="mpsnake"||scenario=="mppac"||scenario=="mpship"){ simEnd=scenario=="mpship"?400000:90000;
      bool pong = scenario=="mppong";
      int downsTo = pong ? 2 : (scenario=="mpsnake" ? 3 : (scenario=="mppac" ? 4 : 5));
      ups(3,1300,180);
      script.push_back({4300,16}); script.push_back({4360,0});     // open multiplayer
      script.push_back({6500,2});  script.push_back({6560,0});     // ANNA's row
      script.push_back({6900,16}); script.push_back({6960,0});     // challenge ...
      for(int i=0;i<downsTo;i++){ script.push_back({7200+i*230,2}); script.push_back({7260+i*230,0}); }
      script.push_back({8200,16}); script.push_back({8260,0});     // ... to pong / snake / pac-man
      if(scenario=="mpship"){ script.push_back({9500,16}); script.push_back({9560,0}); }   // fleet ready
      for(uint32_t t=10000;t<(scenario=="mpship"?395000u:80000u);t+=300){                // wander
        static const uint8_t keys[4]={1,2,4,8};
        script.push_back({t,keys[rand()%4]}); script.push_back({t+(pong?250:60),0});
        if(scenario=="mpship"){ script.push_back({t+150,16}); script.push_back({t+200,0}); } }
      captureAt={2200,2600};
  } else if(scenario=="mpold"){ simEnd=12000;
      simBotOld=true;                                              // ANNA runs an old firmware
      ups(3,1300,180);
      script.push_back({4300,16}); script.push_back({4360,0});
      script.push_back({6500,2});  script.push_back({6560,0});
      script.push_back({6900,16}); script.push_back({6960,0});
      script.push_back({7200,2});  script.push_back({7260,0});
      script.push_back({7450,2});  script.push_back({7510,0});
      script.push_back({8200,16}); script.push_back({8260,0});     // pong
  } else if(scenario=="mpin"){ simEnd=40000;
      simBotInviteAt=7000; simBotGame=LKG_TTT;
      ups(3,1300,180);
      script.push_back({4300,16}); script.push_back({4360,0});     // open multiplayer
      script.push_back({9000,16}); script.push_back({9060,0});     // accept the challenge
      for(uint32_t t=11000;t<36000;t+=400){                       // wander and place
        static const uint8_t keys[4]={1,2,4,8};
        script.push_back({t,keys[rand()%4]}); script.push_back({t+60,0});
        script.push_back({t+180,16}); script.push_back({t+240,0}); }
  } else if(scenario=="mpname"){ simEnd=9000;
      ups(3,1300,180);
      script.push_back({4300,16}); script.push_back({4360,0});     // open multiplayer
      script.push_back({6000,16}); script.push_back({6060,0});     // own row: rename
      script.push_back({6500,1});  script.push_back({6560,0});     // P -> Q
      script.push_back({7000,16}); script.push_back({7060,0});     // save
  } else if(scenario=="wizard2"){ simEnd=20000;
      /* like "wizard", but the board holds GPIO2 and GPIO10 high through
         external pull-ups - this used to make key detection impossible */
      uint8_t w[5]={21,20,5,1,0};
      for(int i=0;i<5;i++){ pinWire[WIRED[i]]=0; WIRED[i]=w[i]; pinWire[w[i]]=2; }
      pinWire[2]=3; pinWire[10]=3;
      simEnd=26000;
      pinScript={{4000,21,1},{4400,21,0},{4600,20,1},{5000,20,0},{5200,5,1},{5600,5,0},
                 {5800,1,1},{6200,1,0},{6400,0,1},{6800,0,0},
                 {14000,0,1},{14150,0,0},{20000,0,1},{20150,0,0}};
      captureAt={40,80};
  } else if(scenario=="wizard"){ simEnd=16000;
      uint8_t w[5]={21,20,10,1,0};                               // unusual pins, 3V3
      for(int i=0;i<5;i++){ pinWire[WIRED[i]]=0; WIRED[i]=w[i]; pinWire[w[i]]=2; }
      simEnd=26000;
      pinScript={{4000,21,1},{4400,21,0},{4600,20,1},{5000,20,0},{5200,10,1},{5600,10,0},
                 {5800,1,1},{6200,1,0},{6400,0,1},{6800,0,0},
                 {14000,0,1},{14150,0,0},        // confirm the buzzer
                 {20000,0,1},{20150,0,0}};       // then start a game
      captureAt={20,40,60,70,80,90,100};
  }

  int easyScore=0, normalScore=0, normalLoss=0;
  if(scenario=="c4bench"){
    /* measured on a PC: depth 5 needs ~13 ms there, roughly 0.4 s on the
       C3 - that is why "hard" stops at depth 5.                          */
    // --- tactical unit tests ---
    memset(c4,0,sizeof(c4));
    c4[5][1]=2; c4[5][2]=2; c4[5][3]=2;                 // engine can win at 0 or 4
    { uint8_t m=c4Think(2); printf("  win-in-1 -> col %u\n", m);
      if(m!=0&&m!=4) printf("  ### engine missed the win\n"); }
    memset(c4,0,sizeof(c4));
    c4[5][0]=1; c4[5][1]=1; c4[5][2]=1;                 // only col 3 blocks
    { uint8_t m=c4Think(2); printf("  block     -> col %u\n", m);
      if(m!=3) printf("  ### engine failed to block\n"); }
    memset(c4,0,sizeof(c4));
    c4[5][3]=1; c4[4][3]=1; c4[3][3]=1;                 // vertical threat
    { uint8_t m=c4Think(2); printf("  block col -> col %u\n", m);
      if(m!=3) printf("  ### engine failed to block the column\n"); }

    for(int opp=0; opp<2; opp++){
    int hardWins=0, easyWins=0, draws=0;
    for(int g=0; g<12; g++){
      memset(c4,0,sizeof(c4));
      uint8_t turn = (g&1) ? 1 : 2;     // alternate who starts
      for(int ply=0; ply<42; ply++){
        uint8_t col;
        if(turn==2) col = c4Think(2);                       // hard plays as 2
        else {                                              // easy plays as 1
          for(int r=0;r<C4_H;r++) for(int c=0;c<C4_W;c++)
            if(c4[r][c]) c4[r][c] = 3 - c4[r][c];           // flip colours
          col = c4Think(opp);                               // 0 = easy, 1 = normal
          for(int r=0;r<C4_H;r++) for(int c=0;c<C4_W;c++)
            if(c4[r][c]) c4[r][c] = 3 - c4[r][c];
        }
        if(c4Drop(col,turn) < 0) break;
        if(c4Wins(turn)){ (turn==2?hardWins:easyWins)++; goto done; }
        if(c4Full()){ draws++; goto done; }
        turn = 3 - turn;
      }
      done: ;
    }
    printf("  hard vs %-6s : %2d wins, %2d losses, %d draws\n",
           opp ? "normal" : "easy", hardWins, easyWins, draws);
    if(opp==0) easyScore = hardWins; else { normalScore = hardWins; normalLoss = easyWins; }
    }
    bool ok = (easyScore>=10 && normalScore>=5 && normalLoss<=2);
    printf("%s\n", ok ? "all checks passed" : "### FAILURES ###");
    return ok ? 0 : 1;
  }

  if(scenario=="phoneapi"){
    /* what the phone pages get from the console (phone.cpp only adds HTTP) */
    simEnd=0x7fffffff;
    static char out[6144];
    int n=appStats(out,sizeof(out));
    int games=0; for(char*p=out;(p=strstr(p,"\"b\":"));p++) games++;
    check("stats: every game and award in valid-looking JSON",
          n>100&&out[0]=='{'&&out[n-1]=='}'&&strstr(out,"\"n\":\"Tetris\"")&&strstr(out,"\"n\":\"Battleship\"")
          &&strstr(out,"RECORD BREAKER")&&games==realGames());
    appSettings(out,sizeof(out));
    printf("      settings %s\n", out);
    check("settings: name and values", strstr(out,"\"name\":\"PLAYER\"")&&strstr(out,"\"sleep\":5"));
    check("brightness set and saved", !appSet("bright","50")&&cfgBright==127&&simNvsU16["bri"]==127);
    check("wrong values refused", appSet("bright","0")&&appSet("sleep","99")&&appSet("clock","100")&&appSet("nope","1"));
    check("clock kept for the next start", !appSet("clock","80")&&simNvsU16["clk"]==80);
    sndPin=10;                                       // a buzzer (setup() did not run here)
    long t0=toneCount; appSet("sound","0"); sfx(1000,10);
    bool silent = toneCount==t0 && cfgMute && simNvsU16["mut"]==1;
    appSet("sound","1");
    check("sound off is silent, on beeps", silent && toneCount>t0 && !cfgMute);
    check("player name cleaned up", !appSet("name","anna!")&&!strcmp(linkName(),"ANNA")&&appSet("name","!!"));
    oled.clearBuffer(); oled.drawBox(0,0,8,8);
    uint8_t scr[1024]; appScreen(scr);
    check("screen copy in panel order", scr[0]==0xFF&&scr[7]==0xFF&&scr[8]==0&&scr[128]==0);
    appKey(B_DOWN,true); bool held=rawPressed(B_DOWN);
    simDelayMs(800);
    check("phone key held, then lets go by itself", held&&!rawPressed(B_DOWN));
    appKey(B_UP,true); appKey(B_UP,false);
    check("phone key released at once", !rawPressed(B_UP));
    const char *lvl="#####|#@$.#|#####";
    check("own level kept", !appLevel(0,lvl)&&appLevelGet(0,out,sizeof(out))&&!strcmp(out,lvl)&&skOpen(SK_N)&&!skOpen(SK_N+1));
    check("own level playable", skLoad(SK_N)&&skStep(3)==2&&skSolved());
    check("bad levels refused", appLevel(1,"#####|#@$ #|#####")&&appLevel(1,"#@$.#")&&appLevel(1,"######|#@$.@#|######")
          &&appLevel(1,"#####|#@$x#|#####")&&appLevel(3,lvl));
    printf("      e.g. \"%s\"\n", appLevel(1,"#@$.#"));
    check("slot emptied", !appLevel(0,"")&&!skOpen(SK_N));
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="bkformat"){
    char line[2000]; static uint8_t buf[1100], blob[1024];
    for(int i=0;i<1024;i++) blob[i]=(uint8_t)(i*7+3);
    int bad=0;
    for(int len : {0,1,2,3,4,5,201,1024}){
      int n=bkLine(line,sizeof(line),"arcade","world",BK_BLOB,blob,len);
      BkEntry e; if(n<0||line[n-1]!='\n'||!bkParse(line,e,buf,sizeof(buf))||e.type!=BK_BLOB||e.len!=len||memcmp(buf,blob,len)) bad++;
    }
    for(unsigned v : {0u,1u,65535u}){
      uint16_t x=(uint16_t)v; bkLine(line,sizeof(line),"arcade","hs3",BK_U16,&x,2);
      BkEntry e; if(!bkParse(line,e,buf,sizeof(buf))||e.type!=BK_U16||e.u16!=v||strcmp(e.key,"hs3")) bad++;
    }
    bkLine(line,sizeof(line),"link","name",BK_STR,"AN NA",0);
    { BkEntry e; if(!bkParse(line,e,buf,sizeof(buf))||e.type!=BK_STR||strcmp(e.text,"AN NA")||strcmp(e.ns,"link")) bad++; }
    check("u16, text and blobs come back unchanged", bad==0);
    const char *wrong[]={"arcade hs0 u16 70000","arcade hs0 u32 1","arcade  u16 1","arcade hs0 blob @@@",
                         "arcade averyveryverylongkey u16 1","arcade hs0 u16","arcade hs0 u16 12x"};
    int taken=0; for(const char*w:wrong){ strcpy(line,w); BkEntry e; if(bkParse(line,e,buf,sizeof(buf))) taken++; }
    check("broken lines refused", taken==0);
    check("names with spaces or too long refused", bkLine(line,sizeof(line),"ar cade","x",BK_STR,"1",0)<0
          && bkLine(line,sizeof(line),"arcade","x",BK_STR,"two\nlines",0)<0);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="g2logic"){
    struct Case { uint8_t in[4]; uint8_t out[4]; uint32_t gain; };
    static const Case C[] = {                          // one row, moved left (exponents)
      {{1,1,1,1},{2,2,0,0},8}, {{1,1,2,0},{2,2,0,0},4}, {{0,0,0,1},{1,0,0,0},0},
      {{2,1,1,0},{2,2,0,0},4}, {{1,2,1,2},{1,2,1,2},0}, {{3,0,3,3},{4,3,0,0},16} };
    for(auto&c:C){
      uint8_t b[16]; memset(b,0,16); memcpy(b,c.in,4);
      uint32_t g=0; g2Move(b,B_LEFT,&g);
      bool ok=!memcmp(b,c.out,4)&&g==c.gain;
      if(!ok) printf("      %u%u%u%u -> %u%u%u%u gain %u\n",c.in[0],c.in[1],c.in[2],c.in[3],b[0],b[1],b[2],b[3],(unsigned)g);
      check("row slides and merges once per pair", ok);
    }
    uint8_t b[16]; memset(b,0,16); b[0]=1; b[4]=1; b[12]=2; uint32_t g=0;
    g2Move(b,B_DOWN,&g); check("column moves down", b[12]==2&&b[8]==2&&!b[0]&&!b[4]&&g==4);
    uint8_t full[16]; for(int i=0;i<16;i++) full[i]=1+(i%2+(i/4)%2)%2;   // checkerboard of 2 and 4
    check("no move left on a checkerboard", !g2CanMove(full));
    full[5]=full[6]; check("a pair is a move", g2CanMove(full));
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="mslogic"){
    int bad=0, zeroOpen=0;
    for(int round=0; round<300; round++){
      randomSeed(50+round);
      uint8_t sx=random(MS_W), sy=random(MS_H), mines=20+(round%3)*8;
      msPlace(mines,sx,sy);
      int n=0; for(int y=0;y<MS_H;y++) for(int x=0;x<MS_W;x++){
        if(ms[y][x]&MS_MINE){ n++; if(abs(x-sx)<=1&&abs(y-sy)<=1) bad++; }
        int c=0; for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){ int X=x+dx,Y=y+dy;
          if((dx||dy)&&X>=0&&X<MS_W&&Y>=0&&Y<MS_H&&(ms[Y][X]&MS_MINE)) c++; }
        if(c!=(ms[y][x]&0x0F)) bad++; }
      if(n!=mines) bad++;
      if(!msOpen(sx,sy)) bad++;
      // after the flood: every open empty cell has all neighbours open, no mine is open
      for(int y=0;y<MS_H;y++) for(int x=0;x<MS_W;x++){
        uint8_t c=ms[y][x];
        if((c&MS_OPEN)&&(c&MS_MINE)) bad++;
        if((c&MS_OPEN)&&!(c&0x0F)&&!(c&MS_MINE)){ zeroOpen++;
          for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){ int X=x+dx,Y=y+dy;
            if(X>=0&&X<MS_W&&Y>=0&&Y<MS_H&&!(ms[Y][X]&MS_OPEN)) bad++; } } }
    }
    printf("      %d empty cells opened by flood fill\n", zeroOpen);
    check("mines placed right, first cell safe, flood fill complete", bad==0);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="pmlogic"){
    /* thousands of ticks with a random player: nobody walks through walls,
       every ghost gets out of the house, the dots go down, games end */
    int wallHits=0, games=0, maxEaten=0; bool out[RM_GH]={false,false,false};
    uint32_t r=99;
    for(int g=0; g<30; g++){
      RtPac P; P.begin(1234+g*77, 1+(g%2));
      uint16_t start=P.dotsLeft; uint8_t in[2]={0,0};
      for(int t=0; t<60000 && !P.winner; t++){
        if(t%25==0){ in[0]=1+rtRand(r)%4; in[1]=1+rtRand(r)%4; }
        P.step(in);
        for(uint8_t p=0;p<P.np;p++) if(RtPac::wallAt(P.pac[p].x,P.pac[p].y)) wallHits++;
        for(uint8_t k=0;k<RM_GH;k++){
          const RmEnt&e=P.gh[k];
          if(e.state==RG_CHASE) out[k]=true;
          if(RtPac::wallAt(e.x,e.y)&&!RtPac::doorAt(e.x,e.y)) wallHits++;
        }
        if(start-P.dotsLeft>maxEaten) maxEaten=start-P.dotsLeft;
      }
      if(P.winner) games++;
    }
    printf("      %d of 30 games ended, up to %d dots eaten in one game\n", games, maxEaten);
    check("nobody inside a wall", wallHits==0);
    check("every ghost leaves the house", out[0]&&out[1]&&out[2]);
    check("games end", games==30);
    check("dots get eaten", maxEaten>40);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="jrfair"){
    /* the jump itself: how high and how far, straight from the constants */
    int vy=-JR_JUMP, y=0, top=0, t=0; do { y+=vy; vy+=JR_G; if(y<top) top=y; t++; } while(y<0);
    int high=-top/JR_Q, far=t*JR_RUN/JR_Q;
    printf("      jump: %d px high, %d px far at full speed\n", high, far);
    check("steps up are within the jump", high >= JR_UP*JR_T + 4);
    check("gaps are within the jump", far >= JR_GAP*JR_T + 8);
    /* and the level: never a wider gap or a higher step than that */
    int bad=0; long cols=0;
    for(int g=0; g<200; g++){
      randomSeed(300+g); jrMade=0; jrCur=1; jrLeft=0; jrMode=0; jrDiff=g%6;
      memset(jrE,0,sizeof(jrE));
      int gap=0, lastLand=1;
      for(int c=0;c<3000;c++,cols++){
        jrMake(); uint8_t i=c%JR_RING, land = jrH[i] ? jrH[i] : jrP[i];
        for(uint8_t k=0;k<JR_EN;k++) jrE[k].on=false;
        if(!land){ gap++; continue; }
        if(gap>JR_GAP){ bad++; if(bad<5) printf("      gap of %d at %d\n",gap,c); }
        if(land>lastLand+JR_UP || (gap>=2 && land>lastLand+1)){ bad++; if(bad<5) printf("      step %d->%d after gap %d\n",lastLand,land,gap); }
        gap=0; lastLand=land;
      }
    }
    printf("      %ld columns made\n", cols);
    check("level never asks for an impossible jump", bad==0);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="sklevels"){
    /* every level: fits the screen, one player, as many boxes as goals,
       and the solver finds a way; the game's own moves undo cleanly */
    int bad=0, last=-1;
    for(uint8_t l=0; l<SK_N; l++){
      SkLevel L=skParse(SK_LEVELS[l]);
      int goals=0; for(uint8_t g:L.goal) goals+=g;
      if(L.w>SK_W||L.h>SK_H||goals!=(int)L.boxes.size()||L.boxes.empty()){ bad++; printf("      level %u malformed\n",l+1); continue; }
      int p=skSolve(L);
      printf("      level %2u: %dx%d, %d boxes, %d pushes\n", l+1, L.w, L.h, (int)L.boxes.size(), p);
      if(p<=0){ bad++; continue; }
      if(p<last) printf("      (easier than the one before)\n");
      last=p;
      if(!skLoad(l)||skW!=L.w||skH!=L.h||skX!=L.px||skY!=L.py){ bad++; printf("      level %u loads differently\n",l+1); }
      uint8_t before[SK_H][SK_W]; memcpy(before,sk,sizeof(sk)); uint8_t bx=skX, by=skY;
      randomSeed(l); int n=0;
      for(int k=0;k<300;k++) if(skStep(random(4))) n++;
      while(skBack()) {}
      if(n>SK_UNDO) { skLoad(l); }                     // more than the undo can hold: fine
      else if(memcmp(before,sk,sizeof(sk))||skX!=bx||skY!=by){ bad++; printf("      level %u: undo does not restore\n",l+1); }
    }
    check("all levels well formed and solvable, undo exact", bad==0);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="bslogic"){
    int bad=0; long shots=0; int most=0;
    for(int g=0; g<500; g++){
      randomSeed(700+g);
      bsPlace(bsTheirs);
      int cells=0, len[6]={0};
      for(int i=0;i<64;i++){ uint8_t s=bsTheirs[i]; if(!s) continue; cells++; len[s]++;
        int x=i%8,y=i/8;                                     // a different ship next to it?
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){ int X=x+dx,Y=y+dy;
          if(X>=0&&Y>=0&&X<8&&Y<8&&bsTheirs[Y*8+X]&&bsTheirs[Y*8+X]!=s) bad++; } }
      if(cells!=14) bad++;
      for(int s=0;s<5;s++) if(len[s+1]!=BS_LEN[s]) bad++;
      memset(bsAiShot,0,64);
      int n=0;
      while(!bsAllSunk(bsTheirs) && n<64){ uint8_t c=bsAiPick(bsAiShot); uint8_t r=bsFire(bsTheirs,c); if(!r) bad++; bsMark(bsAiShot,c,r); n++; }
      if(!bsAllSunk(bsTheirs)) bad++;
      shots+=n; if(n>most) most=n;
    }
    printf("      CPU needs %.1f shots on average, at most %d (64 cells)\n", shots/500.0, most);
    check("fleets legal (sizes, never touching) and the CPU always finishes", bad==0);
    check("the CPU plays well", shots/500.0 < 48);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="frogroll"){
    /* Rolls lanes for every difficulty and level and checks them: objects
       never overlap, every gap has the promised size, the road always
       leaves room for the frog, and neighbouring lanes move opposite ways. */
    int bad=0, rolls=0;
    for(uint8_t diff=0; diff<3; diff++)
      for(int round=0; round<3000; round++){
        randomSeed(7+round*3+diff);
        frRollAll(diff, round%12);
        for(uint8_t l=0;l<FR_LANES;l++,rolls++){
          const FrLane &L=frL[l];
          bool water=frWater(l);
          int glo = water ? 6 : (diff==0 ? 22 : 16);
          int ghi = water ? (diff==0 ? 22 : (diff==1 ? 28 : 34)) : 999;
          if(L.n<2||L.n>FR_MAXO){ bad++; continue; }
          int used=0; for(int k=0;k<L.n;k++) used+=L.w[k];
          for(int k=0;k<L.n;k++){                    // gap after object k, around the ring
            int a=L.x[k]/FR_Q, b=L.x[(k+1)%L.n]/FR_Q;
            int gap=((b-a+SCR_W)%SCR_W) - L.w[k];
            if(L.n==1) gap=SCR_W-L.w[k];
            if(gap<glo||gap>ghi){ bad++; if(bad<5) printf("      diff %u lane %u: gap %d not in %d..%d\n",diff,l,gap,glo,ghi); }
          }
          if(!L.spd) bad++;
          if(l && (L.spd>0)==(frL[l-1].spd>0)) bad++;
        }
      }
    printf("      %d lanes rolled\n", rolls);
    check("lanes always valid (gaps, room for the frog, directions)", bad==0);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="tttbench"){
    /* Every possible way to play against "hard", both with the player and
       with the machine starting, several seeds: it must never lose.     */
    long games=0; int lost=0, easyLost=0;
    std::vector<uint8_t> stack;
    for(int seed=0; seed<8; seed++){
      randomSeed(100+seed);
      for(uint8_t starter=1; starter<=2; starter++){
        // depth first over all player moves, the machine answers each position
        struct F { uint8_t b[9]; };
        std::vector<F> todo; F f0; memset(f0.b,0,9);
        if(starter==2){ memcpy(tt,f0.b,9); tt[ttThink(2,2)]=2; memcpy(f0.b,tt,9); }
        todo.push_back(f0);
        while(!todo.empty()){
          F f=todo.back(); todo.pop_back();
          for(uint8_t i=0;i<9;i++){
            if(f.b[i]) continue;
            memcpy(tt,f.b,9); tt[i]=1;
            if(ttLine(1)>=0){ lost++; games++; continue; }
            if(ttFull()){ games++; continue; }
            tt[ttThink(2,2)]=2;
            if(ttLine(2)>=0||ttFull()){ games++; continue; }
            F g; memcpy(g.b,tt,9); todo.push_back(g);
          }
        }
      }
    }
    for(int g=0; g<500; g++){                   // easy against a random player
      randomSeed(9000+g); memset(tt,0,9); uint8_t turn=1+(g&1);
      while(ttLine(1)<0&&ttLine(2)<0&&!ttFull()){
        if(turn==2) tt[ttThink(0,2)]=2;
        else { uint8_t fr[9],n=0; for(uint8_t i=0;i<9;i++) if(!tt[i]) fr[n++]=i; tt[fr[random(n)]]=1; }
        turn=3-turn;
      }
      if(ttLine(1)>=0) easyLost++;
    }
    printf("      %ld games against hard, %d lost; easy lost %d of 500 to a random player\n", games, lost, easyLost);
    check("hard never loses", lost==0);
    check("easy can be beaten", easyLost>20);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }
  if(scenario=="racerfair"){
    /* Plays the real car spawning at every speed and keeps the set of all
       positions the player could be in. If that set ever runs empty, the
       road was closed - an impossible situation.                        */
    dInitSin();
    int stuck=0; long steps=0;
    for(int g=0; g<400; g++){
      randomSeed(1000+g);
      rcStart();
      static bool can[SCR_W], nx[SCR_W];
      memset(can,0,sizeof(can)); can[rcMid(0,RC_CARY)-3]=true;
      for(int t=0;t<6000;t++,steps++){
        memset(nx,0,sizeof(nx));
        for(int x=0;x<SCR_W;x++) if(can[x]){
          nx[x]=true;
          if(x>2) nx[x-RC_STEP]=true;
          if(x<SCR_W-9) nx[x+RC_STEP]=true;
        }
        rcAdvance();
        bool any=false;
        for(int x=0;x<SCR_W;x++){ if(nx[x]&&rcCrash(x)) nx[x]=false; any|=nx[x]; }
        if(!any){ stuck++; printf("      game %d: no way through after %d steps at speed %u\n",g,t,rc.spd); break; }
        memcpy(can,nx,sizeof(can));
      }
    }
    printf("      %ld steps checked\n", steps);
    check("road never closes up (400 games up to top speed)", stuck==0);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }

  if(scenario=="batcurve"){
    fails=0;
    check("curve ends", batCurve(3200)==0&&batCurve(4200)==100);
    bool mono=true; for(int mv=3200;mv<4250;mv+=5) if(batCurve(mv+5)<batCurve(mv)) mono=false;
    check("curve never goes down with more voltage", mono);
    check("flat middle is not linear", batCurve(3800)==50&&batCurve(3700)<35);
    batPin=0; simHasBattery=true; simBatPin=0; batReset();
    int lo=100, hi=0, last=0;
    for(int i=0;i<600;i++){                  // 60 s at 3.80 V with load dips of 0.25 V
      clockUs += 100000;
      simBatMv = (i%10<3) ? 3550 : 3800;
      int p=batPercent();
      if(i>100){ lo=std::min(lo,p); hi=std::max(hi,p); }
      last=p;
    }
    printf("      with load dips: %d..%d %%\n", lo, hi);
    check("load dips do not make it jump", hi-lo<=3);
    for(int i=0;i<1200;i++){ clockUs += 100000; simBatMv = 3800 - i/4; last=batPercent(); }
    printf("      after sinking to 3.50 V: %d %%\n", last);
    check("a real drop is followed", last<12);
    printf("%s\n", fails?"### FAILURES ###":"all checks passed");
    return fails?1:0;
  }

  try { setup(); for(;;) loop(); } catch(SimEnd&){}

  printf("scenario %s: %ld panel updates, %u ms simulated\n", scenario.c_str(), frames, (unsigned)(clockUs/1000));
  if(getenv("DUMP")) for(auto&t:seenTexts) printf("   |%s|\n", t.c_str());

  if(scenario=="menu"){
    check("library drawn", saw("MiniArcade"));
    check("all games listed", saw("Tetris")&&saw("Snake")&&saw("Pong")&&saw("Doom")&&saw("Mine")&&saw("Tunnel 3D")&&saw("Flappy")&&saw("Invaders")&&saw("Dino")&&saw("Breakout")&&saw("Rocks")&&saw("Racer")&&saw("Frogger")&&saw("4 wins")&&saw("Tic Tac Toe")&&saw("Multiplayer"));
    check("list scrolls to the last entry", saw("Settings"));
    printf("      tones played: %ld, last %d Hz\n", toneCount, lastTone);
    check("menu clicks are audible", toneCount>0);
    check("battery percentage shown", saw("%"));
  } else if(scenario=="tetris"){
    check("HUD drawn", saw("TETRIS")&&saw("LINES")&&saw("LEVEL")&&saw("NEXT"));
    check("game over reached", saw("GAME OVER")||saw("NEW RECORD!"));
    check("high score consistent", simNvsU16["hs0"]==maxScore("SCORE "));
    check("award for the first game", saw("AWARD!")&&saw("FIRST STEPS"));
  } else if(scenario=="snake"){
    check("snake drawn", saw("SNAKE"));
    check("died at the wall", saw("GAME OVER")||saw("NEW RECORD!"));
    check("back in the library after a long press", saw("MiniArcade"));
    check("high score consistent", simNvsU16["hs1"]==maxScore("SCORE "));
  } else if(scenario=="pong"){
    check("pong drawn", saw("PONG"));
    check("returned the ball at least once", maxScore("SCORE ")>0);
    check("game over after missing", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs2 %u\n", maxScore("SCORE "), simNvsU16["hs2"]);
    check("high score consistent", simNvsU16["hs2"]==maxScore("SCORE "));
    check("award for the record", saw("RECORD BREAKER"));
  } else if(scenario=="doom"){
    check("doom drawn", saw("DOOM"));
    check("3D view renders", maxInk>400);
    check("killed a monster", maxScore("SCORE ")>0);
    check("lost all lives", saw("GAME OVER")||saw("NEW RECORD!"));
    check("high score consistent", simNvsU16["hs3"]==maxScore("SCORE "));
  } else if(scenario=="mine"){
    check("mine drawn", saw("MINE"));
    check("world rendered", maxInk>300);
    check("bag counter shown", saw("BAG 0")||saw("BAG 1"));
    check("blocks were mined", simNvsU16["hs4"]>0);
    check("world saved to flash", simNvsBlob.count("world")>0 && simNvsBlob["world"].size()==1024);
    check("back in the library", saw("MiniArcade"));
    printf("      blocks mined %u\n", simNvsU16["hs4"]);
  } else if(scenario=="tunnel"){
    check("tunnel drawn", saw("TUNNEL"));
    check("wireframe renders", maxInk>150);
    check("flew through rings", maxScore("SCORE ")>3);
    check("crash -> game over", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      rings %u, stored hs5 %u\n", maxScore("SCORE "), simNvsU16["hs5"]);
    check("high score consistent", simNvsU16["hs5"]==maxScore("SCORE "));
  } else if(scenario=="flappy"){
    check("flappy drawn", saw("FLAPPY"));
    check("passed a pipe", maxScore("SCORE ")>0);
    check("crash -> game over", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      pipes %u, stored hs6 %u\n", maxScore("SCORE "), simNvsU16["hs6"]);
    check("high score consistent", simNvsU16["hs6"]==maxScore("SCORE "));
  } else if(scenario=="invaders"){
    check("invaders drawn", saw("INVADERS"));
    check("shot aliens", maxScore("SCORE ")>0);
    check("game over reached", saw("GAME OVER")||saw("NEW RECORD!"));
    check("high score consistent", simNvsU16["hs7"]==maxScore("SCORE "));
  } else if(scenario=="dino"){
    check("dino screen drawn", saw("DINO"));
    check("ran some distance", maxScore("SCORE ")>0);
    check("crashed -> game over", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs8 %u\n", maxScore("SCORE "), simNvsU16["hs8"]);
    check("high score consistent", simNvsU16["hs8"]==maxScore("SCORE "));
  } else if(scenario=="breakout"){
    check("breakout screen drawn", saw("BREAKOUT"));
    check("bricks were hit", maxScore("SCORE ")>0);
    check("game over reached", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs9 %u\n", maxScore("SCORE "), simNvsU16["hs9"]);
    check("high score consistent", simNvsU16["hs9"]==maxScore("SCORE "));
  } else if(scenario=="rocks"){
    check("asteroids screen drawn", saw("ROCKS"));
    check("rocks rendered", maxInk>60);
    check("something was shot", maxScore("SCORE ")>0);
    check("game over reached", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs10 %u\n", maxScore("SCORE "), simNvsU16["hs10"]);
    check("high score consistent", simNvsU16["hs10"]==maxScore("SCORE "));
  } else if(scenario=="racer"){
    check("racer screen drawn", saw("RACER"));
    check("road rendered", maxInk>60);
    check("drove some distance", maxScore("SCORE ")>0);
    check("crash -> game over", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs11 %u\n", maxScore("SCORE "), simNvsU16["hs11"]);
    check("high score consistent", simNvsU16["hs11"]==maxScore("SCORE "));
  } else if(scenario=="frogger"){
    check("frogger screen drawn", saw("FROGGER"));
    check("difficulty menu offered", saw("easy")&&saw("normal")&&saw("hard"));
    check("lanes rendered", maxInk>100);
    check("game over reached", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs12 %u\n", maxScore("SCORE "), simNvsU16["hs12"]);
    check("high score consistent", simNvsU16["hs12"]==maxScore("SCORE "));
  } else if(scenario=="ttt"){
    check("tic tac toe drawn", saw("TIC TAC"));
    check("modes offered", saw("1P hard")&&saw("2 players"));
    check("a round ended", saw("YOU WIN")||saw("CPU WINS")||saw("DRAW"));
    check("the machine lost at least once to key mashing", saw("YOU WIN")||saw("DRAW"));
    check("game over after a loss", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs14 %u\n", maxScore("SCORE "), simNvsU16["hs14"]);
    check("high score consistent", simNvsU16["hs14"]==maxScore("SCORE "));
  } else if(scenario=="c4"){
    check("connect four drawn", saw("4 WINS"));
    check("hint shown", saw("OK=drop"));
    check("difficulty menu offered", saw("1P normal")&&saw("2 players"));
    check("the cpu takes its turn", saw("thinking..."));
    check("a game ended", saw("GAME OVER")||saw("NEW RECORD!")||saw("YOU WIN!")||saw("DRAW"));
  } else if(scenario=="sleep"){   simEnd=90000;
      simNvsU16["slp"]=1;                       // one minute, then nothing happens
      captureAt={200};
  } else if(scenario=="settings"){ simEnd=26000; ups(1,1300,220);
      script.push_back({5000,16}); script.push_back({5060,0});     // open settings
      script.push_back({5600,4});  script.push_back({5660,0});     // brightness down
      script.push_back({5900,4});  script.push_back({5960,0});
      script.push_back({6300,2});  script.push_back({6360,0});     // line: cpu clock
      script.push_back({6700,4});  script.push_back({6760,0});     // slower clock
      script.push_back({7100,2});  script.push_back({7160,0});     // line: sleep
      script.push_back({7500,8});  script.push_back({7560,0});     // longer timeout
      script.push_back({8000,2});  script.push_back({8060,0});     // down to battery
      script.push_back({8300,2});  script.push_back({8360,0});
      script.push_back({8600,2});  script.push_back({8660,0});
      script.push_back({9000,16}); script.push_back({9060,0});     // open battery page
      script.push_back({9600,1});  script.push_back({9660,0});
      script.push_back({9900,1});  script.push_back({9960,0});
      script.push_back({10200,1}); script.push_back({10260,0});
      script.push_back({10500,1}); script.push_back({10560,0});
      script.push_back({10800,1}); script.push_back({10860,0});
      script.push_back({11200,16});script.push_back({11260,0});    // pick GPIO0
      captureAt={1000,1700};
  } else if(scenario=="g2048"){
    check("2048 drawn", saw("2048")&&saw("MAX"));
    check("tiles merged", maxScore("SCORE ")>0||simNvsU16["hs15"]>0);
    check("game over reached", saw("NO MOVES")&&(saw("GAME OVER")||saw("NEW RECORD!")));
    printf("      score %u, stored hs15 %u\n", maxScore("SCORE "), simNvsU16["hs15"]);
    check("high score consistent", simNvsU16["hs15"]==maxScore("SCORE "));
  } else if(scenario=="mines"){
    check("difficulty offered", saw("easy    20 mines")&&saw("hard    36 mines"));
    check("minesweeper drawn", saw("MINES")&&saw("*20  0s"));
    check("a round ended", saw("GAME OVER")||saw("NEW RECORD!"));
  } else if(scenario=="pacman"){
    check("pac-man drawn", saw("PAC-MAN")&&saw("READY!"));
    check("dots eaten", maxScore("SCORE ")>0);
    check("game over reached", saw("GAME OVER")&&(saw("NEW RECORD!")||saw("BEST ")));
    printf("      score %u, stored hs17 %u\n", maxScore("SCORE "), simNvsU16["hs17"]);
    check("high score consistent", simNvsU16["hs17"]==maxScore("SCORE "));
  } else if(scenario=="shooter"){
    check("shooter drawn", saw("SHOOTER"));
    check("something shot down", maxScore("SCORE ")>0);
    check("game over reached", saw("GAME OVER")||saw("NEW RECORD!"));
    printf("      score %u, stored hs18 %u\n", maxScore("SCORE "), simNvsU16["hs18"]);
    check("high score consistent", simNvsU16["hs18"]==maxScore("SCORE "));
  } else if(scenario=="jump"){
    check("jump & run drawn", saw("JUMP"));
    check("ran some way", maxScore("SCORE ")>0||simNvsU16["hs19"]>0);
    printf("      score %u, stored hs19 %u\n", maxScore("SCORE "), simNvsU16["hs19"]);
    check("game over reached", saw("GAME OVER")||saw("NEW RECORD!"));
  } else if(scenario=="sokoban"){
    check("level list shown", saw("SOKOBAN 1")&&saw("<1/")&&saw(" OK"));
    check("moves counted", saw("7 moves")||saw("8 moves")||saw("9 moves"));
    check("undo counts back", saw("5 moves")||saw("6 moves"));
  } else if(scenario=="bsplace"){
    static const char *want[8]={"1111....","........","222.....","........","333.....","........","44...5..",".....5.."};
    bool same=true;
    for(int y=0;y<8;y++) for(int x=0;x<8;x++){ uint8_t c=bsMine[y*8+x]&0x7F; char w=want[y][x];
      if((w=='.'&&c)||(w!='.'&&c!=w-'0')) same=false; }
    if(!same) for(int y=0;y<8;y++){ printf("      "); for(int x=0;x<8;x++){ uint8_t c=bsMine[y*8+x]&0x7F; putchar(c?'0'+c:'.'); } printf("\n"); }
    check("placing by hand offered and shown", saw("UP=mix")&&saw("DOWN=own")&&saw("ship 4")&&saw("2xOK=turn"));
    check("ships where they were put, the last one turned", same);
    check("game started with this fleet", saw("your go"));
  } else if(scenario=="battleship"){
    check("battleship drawn", saw("BATTLESHIP")&&saw("UP=mix")&&saw("your go"));
    check("shots fired", saw("miss")||saw("hit!"));
    check("a game ended", saw("GAME OVER")||saw("NEW RECORD!"));
  } else if(scenario=="pause"){
    StatBlob b; memset(&b,0,sizeof(b));
    if(simNvsBlob.count("stats")) memcpy(&b,simNvsBlob["stats"].data(),sizeof(b));
    printf("      tetris started %u times, %u s counted\n", b.plays[0], (unsigned)b.secs[0]);
    check("pause menu shown", saw("PAUSE")&&saw("continue")&&saw("restart")&&saw("quit to menu"));
    check("restart counted as a new start", b.plays[0]==2);
    check("both rounds counted, the pause menu not", b.secs[0]>=5 && b.secs[0]<=14);
  } else if(scenario=="stats"){
    check("stats page drawn with the total time", saw("STATS")&&saw("1h03  AWARDS>"));
    check("per game line", saw("Tetris        3x   1h02"));
    check("awards page drawn", saw("AWARDS")&&saw("1/30  <STATS")&&saw("* FIRST STEPS")&&saw("- EXPLORER"));
    check("what an award needs is shown", saw("try every game"));
  } else if(scenario=="wlan"){
    check("settings list the wlan page", saw("wlan and update..."));
    check("address of the update page shown", saw("http://192.168.1.50"));
    check("online state shown, disconnect offered", saw("online")&&saw("disconnect"));
    check("newer release offered", saw("install 9.3"));
    check("update written, restart announced", saw("done - restarting"));
  } else if(scenario=="wlanstay"){
    check("stay online offered and switched on", saw("stay online: no")&&saw("stay online: yes"));
    check("restore question shown and answered on the console", saw("restore the backup")&&simRestoreAnswer==1);
    check("WLAN stays on after leaving the page", phoneLink&&pollHook==phoneHook&&simNet==NET_ONLINE);
    bool shown=false; for(auto&t:seenTexts) if(t.rfind("WLAN ",0)==0&&t.find('%')!=std::string::npos) shown=true;
    check("menu shows that the WLAN is on", shown);
    phoneLinkOff();
    check("multiplayer (or anyone) can switch it off", !phoneLink&&!pollHook&&simNet==NET_OFF);
  } else if(scenario=="batfind"){
    check("found at start, the floating pin was skipped", simBatStart==2&&saw("*2:"));
    check("battery page marks the divider", saw("RIGHT=find")&&saw(" bat"));
    check("search finds the rewired pin", saw("found on GPIO0"));
    check("stored", simNvsU16["bat"]==0&&batPin==0);
    check("menu shows percent", saw("%"));
  } else if(scenario=="bat5v"){
    check("nothing taken as battery", batPin>4);
    check("5 V divider explained", saw("no battery found")&&saw("GPIO1 sees 5 V:")&&saw("to BAT+ (not OUT)"));
  } else if(scenario=="batnone"){
    check("no battery chosen by hand is kept", simNvsU16["bat"]==253&&batPin==253);
    batDetect();
    check("and not searched again at the next start", batPin==253);
  } else if(scenario=="wlanplay"){
    check("hotspot offered on the wlan page", saw("phone hotspot"));
    check("name, password and address shown", saw("PHONE HOTSPOT")&&saw("join  MiniArcade-AB12")&&saw("pass  12345678")&&saw("open  http://192.168.4.1"));
    check("LEFT makes a new password", saw("pass  87654321"));
    check("RIGHT switches the password off and on", saw("no password - open!")&&!simApOpen);
    check("OK stops the hotspot", simNet==NET_OFF||simNet==NET_ONLINE);
  } else if(scenario=="wlanfail"){
    check("while joining: state shown, no disconnect offered", saw("joining")&&saw("stop joining"));
    check("after failing: failed, connect offered", saw("failed")&&saw("can't join HomeNet")&&saw("connect"));
    check("never claims to be connected", !saw("disconnect")&&!seenTexts.count("online"));
  } else if(scenario=="versions"){
    check("version choice shown at start", saw("VERSION")&&saw("previous")&&saw("new"));
    check("countdown shown", saw("3s")&&saw("1s"));
    check("started the new version by itself", saw("MiniArcade")&&!simSwitched);
    check("kept after the first key press", simConfirmed);
  } else if(scenario=="versions2"){
    check("version choice shown at start", saw("VERSION"));
    check("switched to the previous version", simSwitched&&saw("STARTING"));
    check("new version not confirmed", !simConfirmed);
  } else if(scenario=="versions3"){
    check("no deep sleep while the update is unconfirmed", !simSlept);
    check("not confirmed without a key press", !simConfirmed);
  } else if(scenario=="upload"){
    check("upload question shown on the device", saw("install firmware")&&saw("v9.4"));
    check("confirmed with OK", simAnswer==1);
    check("update written, restart announced", saw("done - restarting"));
  } else if(scenario=="mp4"){
    check("multiplayer page lists ANNA", saw("LOBBY")&&saw("ANNA"));
    check("challenge sent", saw("waiting for ANNA"));
    check("game against ANNA shown", saw("vs ANNA"));
    check("a game was decided", saw("YOU WIN")||saw("LOST")||saw("DRAW"));
    check("the bot saw the same end", simBotGames>=1);
  } else if(scenario=="mpno"){
    check("decline reported", saw("ANNA said no"));
  } else if(scenario=="mpwait"){
    check("challenge counts down", saw("29s")&&saw("1s"));
    check("no answer after 30 s", saw("no answer from ANNA"));
  } else if(scenario=="mpleft"){
    check("opponent leaving reported", saw("ANNA left the game"));
  } else if(scenario=="mppong"||scenario=="mpsnake"||scenario=="mppac"||scenario=="mpship"){
    bool pong = scenario=="mppong"; (void)pong;
    check("challenge offers the real-time games", saw("Pong")&&saw("Snake")&&(scenario!="mppac"||saw("Pac-Man")));
    check("game against ANNA shown", scenario=="mpship" ? saw("vs ANNA")&&saw("UP=mix") : saw("YOU ")&&saw(" ANNA"));
    check("a game was decided", saw("YOU WIN")||saw("LOST")||saw("DRAW"));
    bool same = (simBotResult==1&&saw("LOST"))||(simBotResult==2&&saw("YOU WIN"))||(simBotResult==3&&saw("DRAW"));
    printf("      bot games %d, bot result %d (%s)\n", simBotGames, simBotResult, scenario.c_str());
    check("both consoles saw the same end", simBotGames>=1 && same);
    StatBlob b; memset(&b,0,sizeof(b));
    if(simNvsBlob.count("stats")) memcpy(&b,simNvsBlob["stats"].data(),sizeof(b));
    check("online game counted, award shown", b.mpGames>=1 && saw("HELLO THERE"));
  } else if(scenario=="mpold"){
    check("old firmware: no real-time challenge, a hint instead", saw("ANNA needs an update")&&!saw("waiting for ANNA"));
  } else if(scenario=="mpin"){
    check("incoming challenge shown", saw("CHALLENGE!")&&saw("wants to play Tic Tac Toe"));
    check("game against ANNA shown", saw("vs ANNA"));
    check("a game was decided", saw("YOU WIN")||saw("LOST")||saw("DRAW"));
    check("the bot saw the same end", simBotGames>=1);
  } else if(scenario=="mpname"){
    check("name editor shown", saw("YOUR NAME"));
    check("new name kept", !strcmp(linkName(),"QLAYER"));
  } else if(scenario=="wizard2"){
    printf("      learned UP=%u DOWN=%u LEFT=%u RIGHT=%u OK=%u\n",
      simNvsU16["p0"],simNvsU16["p1"],simNvsU16["p2"],simNvsU16["p3"],simNvsU16["p4"]);
    check("asked to release buttons", saw("release all buttons"));
    check("asked for every key", saw("press  UP")&&saw("press  OK"));
    check("keys learned although two pins are held by the board",
      simNvsU16["p0"]==21&&simNvsU16["p1"]==20&&simNvsU16["p2"]==5&&simNvsU16["p3"]==1&&simNvsU16["p4"]==0);
    check("polarity detected as 3V3", simNvsU16["a0"]==1&&simNvsU16["a4"]==1);
    check("library reached", saw("MiniArcade"));
} else if(scenario=="wizard"){
    check("asked to release buttons", saw("release all buttons"));
    check("asked for every key", saw("press  UP")&&saw("press  DOWN")&&saw("press  LEFT")&&saw("press  RIGHT")&&saw("press  OK"));
    printf("      learned UP=%u DOWN=%u LEFT=%u RIGHT=%u OK=%u  polarity %u\n",
      simNvsU16["p0"],simNvsU16["p1"],simNvsU16["p2"],simNvsU16["p3"],simNvsU16["p4"],simNvsU16["a0"]);
    check("pins learned", simNvsU16["p0"]==21&&simNvsU16["p1"]==20&&simNvsU16["p2"]==10&&simNvsU16["p3"]==1&&simNvsU16["p4"]==0);
    check("polarity detected as 3V3", simNvsU16["a0"]==1&&simNvsU16["a4"]==1);
    check("library reached", saw("MiniArcade"));
    check("sound wizard ran", saw("SOUND")&&saw("do you hear a beep?"));
    printf("      sound pin stored: %u\n", simNvsU16["snd"]);
    check("buzzer pin stored", simNvsU16["snd"]!=255);
    check("learned OK starts a game", saw("TETRIS"));
  }
  printf("%s\n", fails?"### FAILURES ###":"all checks passed");
  return fails?1:0;
}
