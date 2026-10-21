#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include <iostream>
#include "../src/durable_outbox.h"

struct Disk {
  static std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
  static bool mounted,failRename,failRemove,tearAppend,failSync;
  static size_t space;
  struct File {
    std::shared_ptr<std::vector<uint8_t>> bytes;size_t pos=0;bool append=false;
    explicit operator bool()const{return bool(bytes);}
    size_t size()const{return bytes?bytes->size():0;}
    bool available()const{return bytes&&pos<bytes->size();}
    size_t read(uint8_t *out,size_t n){if(!bytes)return 0;n=std::min(n,size()-pos);memcpy(out,bytes->data()+pos,n);pos+=n;return n;}
    size_t write(const uint8_t *p,size_t n){
      if(!bytes)return 0;if(append&&tearAppend){n=std::min(n,size_t(3));tearAppend=false;}
      if(pos+n>size())bytes->resize(pos+n);memcpy(bytes->data()+pos,p,n);pos+=n;return n;
    }
    bool seek(size_t n){pos=n;return bool(bytes)&&n<=size();}void flush(){}void close(){}
  };
  bool begin(){return mounted;}size_t availableBytes(){return space;}uint64_t now(){return 1792580000;}void pause(){}
  bool exists(const std::string &p){return files.count(p);}
  bool sync(const std::string &){return !failSync;}
  bool remove(const std::string &p){if(failRemove)return false;return files.erase(p)>0;}
  bool rename(const std::string &a,const std::string &b){if(failRename||files.count(b)||!files.count(a))return false;files[b]=files[a];files.erase(a);return true;}
  File open(const std::string &p,const char *mode){
    if(*mode=='r'&&!files.count(p))return {};
    if(*mode=='w'||!files.count(p))files[p]=std::make_shared<std::vector<uint8_t>>();
    File f;f.bytes=files[p];f.append=*mode=='a';f.pos=f.append?f.size():0;return f;
  }
};
std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> Disk::files;
bool Disk::mounted=true,Disk::failRename=false,Disk::failRemove=false,Disk::tearAppend=false,Disk::failSync=false;
size_t Disk::space=1000000;
using Box=telegram::BasicOutbox<Disk>;
int main(){
  using telegram::StoredKind;
  Disk::files["/miei-report.pdf"]=std::make_shared<std::vector<uint8_t>>(100,42);
  const char message[]="Allarme forno";const std::vector<uint8_t> curve(1000,7);
  Box live;assert(live.begin(42));
  const int a=live.put(StoredKind::Alarm,message,sizeof(message));assert(a>=0&&live.count()==1);
  const int doc=live.put(StoredKind::Report,message,sizeof(message),curve.data(),curve.size());assert(doc>=0);
  Box reboot;assert(reboot.begin(42));assert(reboot.count()==2&&reboot.next(StoredKind::Alarm)==a);
  char recovered[sizeof(message)]{};assert(reboot.read(a,recovered,sizeof(recovered)));assert(!strcmp(message,recovered));
  assert(!reboot.read(a,recovered,sizeof(recovered)-1));
  assert(reboot.mark(doc,0,true));
  Disk::failSync=true;assert(!reboot.mark(doc,0,false));
  assert(reboot.entry(doc).progress.ambiguous);Disk::failSync=false;
  assert(reboot.mark(doc,0,false));assert(reboot.mark(doc,0,true));
  Box cutDuringUpload;assert(cutDuringUpload.begin(42));
  assert(cutDuringUpload.entry(doc).progress.ambiguous&&cutDuringUpload.entry(doc).progress.index==0);
  assert(cutDuringUpload.mark(doc,1,false));
  Disk::tearAppend=true;assert(!cutDuringUpload.mark(doc,1,true));
  Box afterTornAck;assert(afterTornAck.begin(42));assert(!afterTornAck.entry(doc).progress.ambiguous&&afterTornAck.entry(doc).progress.index==1);
  assert(afterTornAck.mark(doc,1,true)); // Padding restores alignment after a torn tail.
  Box aligned;assert(aligned.begin(42));assert(aligned.entry(doc).progress.ambiguous&&aligned.entry(doc).progress.index==1);
  // A durable tombstone survives an unlink failure and prevents a replay.
  Disk::failRemove=true;aligned.erase(doc);Disk::failRemove=false;
  Box consumed;assert(consumed.begin(42));assert(consumed.next(StoredKind::Report)<0);
  consumed.erase(a);assert(consumed.count()==0);
  // Old ACK state cannot authorize skipping a new document reusing this slot.
  const int reused=consumed.put(StoredKind::Report,message,sizeof(message));assert(reused==a);
  Box reuseBoot;assert(reuseBoot.begin(42));assert(reuseBoot.entry(reused).progress.index==0&&!reuseBoot.entry(reused).progress.ambiguous);
  Disk::space=10;assert(reuseBoot.put(StoredKind::Alarm,message,sizeof(message))<0);Disk::space=1000000;
  Disk::failRename=true;assert(reuseBoot.put(StoredKind::Alarm,message,sizeof(message))<0);Disk::failRename=false;
  Disk::failSync=true;assert(reuseBoot.put(StoredKind::Alarm,message,sizeof(message))<0);Disk::failSync=false;
  // Corruption and a different configured owner never replay protected content.
  Disk::files["/ovtg0.dat"]->back()^=0x20;
  Box corrupt;assert(corrupt.begin(42));assert(corrupt.count()==0&&corrupt.failures()>0);
  assert(corrupt.put(StoredKind::Alarm,message,sizeof(message))>=0);
  Box otherOwner;assert(otherOwner.begin(99));assert(otherOwner.count()==0);
  assert(Disk::files.count("/miei-report.pdf"));
  Disk::mounted=false;Box unavailable;assert(!unavailable.begin(42));assert(unavailable.put(StoredKind::Alarm,message,sizeof(message))<0);
  std::cout<<"Outbox: restart replay, payload validation, send-intent/ACK crash boundaries, torn writes, slot reuse, full/unavailable storage, corruption and owner isolation passed\n";
}
