#include <cassert>
#include <cstdint>
#include <iostream>
#include "../src/report_curve.h"
struct Sample { uint32_t seq, ms; uint8_t flags; };
int main() {
  Sample ring[] = {{4,600,0x20},{5,700,0x10},{1,300,3},{2,400,0x13},{3,500,1}};
  Sample out[5]{}; bool partial = false;
  auto count = reports::copyCurve(ring,5,2,5,400,600,out,5,partial);
  assert(count==3 && !partial && out[0].seq==2 && out[2].seq==4);
  assert(out[1].flags==1); // Invalid probe pairs remain visible as gaps.
  ring[3]={20,1400,0x10};
  assert(out[0].seq==2); // Starting a new cycle cannot mutate the frozen report.
  count=reports::copyCurve(ring,5,2,5,400,600,out,5,partial);
  assert(count==2 && partial); // Start overwritten: never call this a full curve.
  ring[3]={2,400,0x13};
  count=reports::copyCurve(ring,5,2,5,400,600,out,1,partial);
  assert(count==1 && partial); // Destination bounds are enforced.
  count=reports::copyCurve(ring,5,2,5,800,900,out,5,partial);
  assert(count==0 && partial); // No fallback to samples from another cycle.
  Sample wrap[]={{1,UINT32_MAX-100,0x13},{2,UINT32_MAX-10,3},{3,20,3},{4,100,3}};
  count=reports::copyCurve(wrap,4,0,4,UINT32_MAX-100,20,out,5,partial);
  assert(count==3 && !partial && out[2].ms==20);
  count=reports::copyCurve(wrap,4,3,2,UINT32_MAX-10,20,out,5,partial);
  assert(count==2 && partial && out[0].seq==2 && out[1].seq==3);
  count=reports::copyCurve(wrap,0,0,0,0,10,out,5,partial);
  assert(count==0 && partial);
  std::cout << "OK: report curve isolation, ring order, missing probes, partial history, capacity and timer wrap\n";
}
