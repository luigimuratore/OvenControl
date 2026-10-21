#include <cassert>
#include "../src/plot_snapshot.h"
struct Sample { uint32_t ms; float t1, t2, setpoint; uint8_t step, flags; };
int main() {
  Sample ring[4] = {{10000,100,102,105,0,11},{20000,101,103,106,1,27},
    {30000,102,104,107,1,9},{40000,103,105,108,1,11}};
  reportpdf::Point points[4];
  Sample live{45000,104,106,109,1,11};
  // Saturated output includes the most recent samples and fresh live reading.
  size_t n=plotpng::snapshot(ring,4,0,4,live,points,4);
  assert(n==4 && points[0].ms==20000 && points[3].mean==105);
  assert(points[0].flags==27 && points[0].step==1);
  assert(!std::isfinite(points[1].mean) && points[1].target==107);
  // Ring rotation remains chronological; live duplicate replaces stale values.
  ring[0]={50000,104,106,109,2,11}; live={50000,110,112,110,2,11};
  n=plotpng::snapshot(ring,4,1,4,live,points,4);
  assert(n==3 && points[0].ms==30000 && points[2].mean==111);
  // Unsigned ages support timer wrap and omit samples outside the 30-minute window.
  ring[0]={UINT32_MAX-1000,90,92,95,0,11};
  ring[1]={uint32_t(1000-plotpng::kWindowMs-1),70,72,75,0,11};
  live={1000,100,102,105,0,11};
  n=plotpng::snapshot(ring,4,2,2,live,points,4);
  assert(n==2 && points[0].mean==91 && points[1].ms==1000);
  n=plotpng::snapshot(ring,4,0,0,live,points,4); assert(n==1 && points[0].mean==101);
  assert(!plotpng::snapshot(ring,4,0,5,live,points,4));
  assert(!plotpng::snapshot(ring,4,0,0,live,points,1));
  // A full hour contains more than 257 samples: retain the start, gaps and markers.
  Sample hour[361]; reportpdf::Point full[362];
  for (size_t i=0;i<361;++i) hour[i]={uint32_t(i*10000),100,102,105,0,11};
  hour[1].flags=27; hour[350].flags=0;
  live=hour[360];
  for (unsigned minutes : {15U,30U,60U}) {
    n=plotpng::snapshot(hour,361,0,361,live,full,362,minutes*60000UL);
    assert(n==minutes*6+1 && full[0].ms==(60-minutes)*60000UL);
    assert(full[n-1].ms==live.ms && !std::isfinite(full[n-11].mean));
    if (minutes==60) assert(full[1].flags==27);
  }
  assert(!plotpng::snapshot(hour,361,0,361,live,full,362,0));
  assert(!plotpng::snapshot(hour,361,0,361,live,full,362,3600001));
}
