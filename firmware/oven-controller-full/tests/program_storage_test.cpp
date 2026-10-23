#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <type_traits>
#include "../src/program_storage.h"

int main() {
  static_assert(std::is_trivially_copyable<programs::Snapshot>::value, "Persistent value must own all bytes");
  programs::Snapshot data; data.count = 8;
  for (unsigned i=0; i<8; ++i) {
    auto &p = data.programs[i]; snprintf(p.id,sizeof(p.id),"programma-%u",i);
    snprintf(p.name,sizeof(p.name),"Prova alta temperatura %u",i); p.count = 12;
    for (auto &s : p.steps) {s.type=uint8_t(profile::Type::Hold); s.target=175; s.minutes=360;}
  }
  assert(programs::valid(data) && programs::bytes(data) == 1832);
  programs::Snapshot loaded;
  assert(programs::decode(&data,programs::bytes(data),loaded));
  assert(!memcmp(&loaded,&data,sizeof(data)));
  auto bad=data; bad.version=99; assert(!programs::valid(bad));
  bad=data; bad.count=0; assert(!programs::valid(bad));
  bad=data; bad.count=9; assert(!programs::valid(bad));
  bad=data; strcpy(bad.programs[1].id,bad.programs[0].id); assert(!programs::valid(bad));
  bad=data; strcpy(bad.programs[0].id,"manual:session"); assert(!programs::valid(bad));
  bad=data; memset(bad.programs[0].name,'x',sizeof(bad.programs[0].name)); assert(!programs::valid(bad));
  bad=data; bad.programs[0].count=13; assert(!programs::valid(bad));
  bad=data; bad.programs[0].reserved=1; assert(!programs::valid(bad));
  bad=data; bad.programs[0].steps[0].target=NAN; assert(!programs::valid(bad));
  bad=data; bad.programs[0].steps[0].target=400.01f; assert(!programs::valid(bad));
  bad=data; bad.programs[0].steps[0].minutes=0; assert(!programs::valid(bad));
  bad=data; bad.programs[0].steps[0].type=3; assert(!programs::valid(bad));
  bad=data; bad.programs[0].steps[0].type=uint8_t(profile::Type::Ramp); bad.programs[0].steps[0].rate=NAN;
  assert(!programs::valid(bad));
  // Every step owns its target independently of the preceding step.
  auto &p=data.programs[0]; p.count=3;
  p.steps[0]={100,2,0,uint8_t(profile::Type::Ramp),0};
  p.steps[1]={100,0,60,uint8_t(profile::Type::Hold),0};
  p.steps[2]={25,1,0,uint8_t(profile::Type::Cooldown),0};
  assert(programs::valid(data));
  bad=data; bad.programs[0].steps[1].target=101; assert(programs::valid(bad));
  bad=data; bad.programs[0].steps[2].target=101; assert(programs::valid(bad));
  data.count=1; assert(programs::bytes(data)==236);
  assert(programs::decode(&data,programs::bytes(data),loaded));
  const auto previous=loaded;
  for(size_t length:{size_t(0),size_t(7),programs::bytes(data)-1,programs::bytes(data)+1}) {
    assert(!programs::decode(&data,length,loaded));
    assert(!memcmp(&previous,&loaded,sizeof(loaded))); // Invalid reads never replace active data.
  }
  assert(!programs::decode(nullptr,programs::bytes(data),loaded));
  uint8_t blob[programs::kMaxBlobBytes]{}, again[programs::kMaxBlobBytes]{};
  // All adjacent type combinations and equal/higher/lower independent targets
  // must survive both storage schemas, including reboot decoding.
  for (uint8_t first=0;first<3;++first) for (uint8_t second=0;second<3;++second) {
    for (float target:{50.f,100.f,150.f}) {
      auto independent=data; auto &p=independent.programs[0]; p.count=2;
      p.steps[0]={100,1,10,first,0}; p.steps[1]={target,1,10,second,0};
      assert(programs::valid(independent));
      assert(programs::decode(&independent,programs::bytes(independent),loaded));
      const auto n=programs::encode(independent,blob,sizeof(blob));
      assert(n && programs::decode(blob,n,loaded));
      assert(loaded.programs[0].steps[1].target==target && loaded.programs[0].steps[1].type==second);
    }
  }
  const size_t length=programs::encode(data,blob,sizeof(blob));
  assert(length && length<programs::bytes(data));
  assert(!programs::encode(data,again,length-1));
  assert(programs::decode(blob,length,loaded));
  assert(loaded.count==1 && !strcmp(loaded.programs[0].name,data.programs[0].name));
  assert(loaded.programs[0].steps[0].rate==2 && loaded.programs[0].steps[1].minutes==60 && loaded.programs[0].steps[2].target==25);
  assert(programs::encode(loaded,again,sizeof(again))==length && !memcmp(blob,again,length));
  const auto compactPrevious=loaded;
  for(size_t n=0;n<length;++n) {
    assert(!programs::decode(blob,n,loaded));
    assert(!memcmp(&loaded,&compactPrevious,sizeof(loaded)));
  }
  blob[length]=0; assert(!programs::decode(blob,length+1,loaded));
  blob[8]=33; assert(!programs::decode(blob,length,loaded));
  programs::Snapshot maximum; maximum.count=8;
  for(unsigned i=0;i<8;++i) {
    auto &p=maximum.programs[i]; memset(p.id,'a',32); p.id[0]=char('a'+i); memset(p.name,'b',48); p.count=12;
    for(unsigned j=0;j<12;++j) p.steps[j]={float(20*j),1,0,uint8_t(profile::Type::Ramp),0};
  }
  assert(programs::encode(maximum,blob,sizeof(blob))==1536);
  assert(programs::decode(blob,1536,loaded) && loaded.count==8);
  assert(loaded.programs[7].steps[11].target==220);
  // The real two-program installation fits without the unused 18 step slots.
  programs::Snapshot actual; actual.count=2;
  for(unsigned i=0;i<2;++i) {
    auto &p=actual.programs[i]; strcpy(p.id,i?"ricetta-muzr9kgb":"prova-50");
    strcpy(p.name,i?"Recupero PA12":"Test carbonio 80°C"); p.count=3;
    p.steps[0]={i?175.f:80.f,i?10.f:1.375f,0,0,0};
    p.steps[1]={i?175.f:80.f,0,uint16_t(i?60:100),1,0}; p.steps[2]={i?20.f:25.f,i?2.f:1.f,0,2,0};
  }
  const auto actualBytes=programs::encode(actual,blob,sizeof(blob)); assert(actualBytes==120);
  assert(programs::decode(blob,actualBytes,loaded));
  assert(!strcmp(loaded.programs[0].name,"Test carbonio 80°C") && loaded.programs[0].steps[0].rate==1.375f);
  // Interrupted migrations retain either the legacy or committed applied copy.
  using W=programs::WriteStatus;
  for(int scenario=0;scenario<6;++scenario) {
    bool legacy=true; int committed=0, attempts=0, appliedAttempts=0, retired=0;
    const bool saved=programs::saveWithLegacyMigration(scenario!=5,[&] {
      ++attempts;
      if(scenario==0 || (attempts==2 && scenario==1)) {committed=2;return W::Saved;}
      return scenario==4?W::Error:W::NoSpace;
    },[&] {
      ++appliedAttempts;
      if(scenario==2)return W::Error;
      committed=1;return W::Saved;
    },[&] { assert(committed); legacy=false; ++retired; });
    assert(saved==(scenario==0 || scenario==1));
    assert(committed || legacy); // There is never a point with no recoverable copy.
    if(scenario==2 || scenario==4 || scenario==5) assert(legacy && !retired);
    if(scenario==3) assert(committed==1 && !legacy && attempts==2);
    if(scenario==4 || scenario==5) assert(!appliedAttempts && attempts==1);
  }
  std::cout<<"Program storage: V1 compatibility, compact V2 (120 bytes for installed programs), maximum collections, bounded decoding, Unicode/float round trips and interrupted migration safety passed\n";
}
