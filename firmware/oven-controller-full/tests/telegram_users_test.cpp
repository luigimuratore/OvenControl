#include "../src/telegram_users.h"
#include <cassert>
#include <string>
int main(){
  using namespace telegram;
  assert(privateSender(42,42,"private"));
  assert(!privateSender(42,43,"private")&&!privateSender(-42,42,"private")&&!privateSender(42,42,"group"));
  Users users;users.begin(42,Mode::Essential);assert(users.valid(42)&&users.count()==1);
  User guest;guest.chat=9007199254740991LL;copyUserText(guest.name,sizeof(guest.name),"Oriana");
  copyUserText(guest.username,sizeof(guest.username),"oriana_1");guest.mode=uint8_t(Mode::Detailed);
  assert(users.add(guest)&&!users.add(guest)&&users.valid(42));
  auto proposed=users;assert(proposed.remove(guest.chat));
  assert(!commitUsers(users,proposed,[](const Users &){return false;}));
  assert(users.find(guest.chat)==1); // A failed durable revocation preserves the applied state.
  assert(commitUsers(users,proposed,[](const Users &){return true;}));
  assert(users.find(guest.chat)<0&&users.add(guest));
  assert(users.find(guest.chat)==1&&users.find(7)<0);
  assert(users.setMode(guest.chat,1)&&users.entries[0].mode==uint8_t(Mode::Essential));
  assert(!users.setMode(guest.chat,9)&&!users.setMode(7,1));
  assert(!users.remove(42));
  // Persisted records are validated against the configured owner and duplicate IDs.
  Users restored=users;assert(restored.valid(42)&&!restored.valid(43));
  restored.version=99;assert(!restored.valid(42));restored=users;
  restored.entries[2]=guest;assert(!restored.valid(42));restored=users;
  memset(restored.entries[1].name,'a',sizeof(restored.entries[1].name));assert(!restored.valid(42));
  for(int i=2;i<int(kMaxUsers);++i){guest.chat=i;assert(users.add(guest));}
  guest.chat=100;assert(users.count()==kMaxUsers&&!users.add(guest));
  assert(users.remove(9007199254740991LL)&&users.find(9007199254740991LL)<0);
  assert(users.add(guest)); // Slot reuse never authorizes the old chat ID.
  PendingUsers pending;guest.chat=101;assert(pending.add(guest,100)&&!pending.add(guest,200));
  pending.expire(100+kJoinLifetimeMs-1);assert(pending.find(101)>=0);
  pending.expire(100+kJoinLifetimeMs);assert(pending.find(101)<0);
  const uint32_t start=UINT32_MAX-1000;
  assert(pending.add(guest,start));pending.expire(start+kJoinLifetimeMs);assert(pending.find(101)<0);
  for(int i=0;i<int(kMaxUsers);++i){guest.chat=i+1;assert(pending.add(guest,1));}
  guest.chat=99;assert(!pending.add(guest,2));assert(pending.remove(1)&&pending.add(guest,2));
  char utf8[5];copyUserText(utf8,sizeof(utf8),"èèè");assert(std::string(utf8)=="èè");
  char small[4];copyUserText(small,sizeof(small),"🔥");assert(!small[0]);
  assert(modeIndex("essenziale")==0&&modeIndex("sintetica")==1&&modeIndex("dettagliata")==2&&modeIndex("bad")==-1);
  Users compact; compact.begin(42,Mode::Essential);
  assert(storedUsersBytes(compact)==104 && sizeof(Users)==776);
  assert(decodeUsers(&compact,storedUsersBytes(compact),42,restored) && restored.count()==1);
  assert(decodeUsers(&compact,sizeof(compact),42,restored)); // Previous full-size records remain readable.
  guest.chat=77;guest.mode=uint8_t(Mode::Detailed);assert(compact.add(guest));
  assert(storedUsersBytes(compact)==200);
  assert(decodeUsers(&compact,storedUsersBytes(compact),42,restored));
  assert(restored.find(77)==1 && restored.entries[1].mode==uint8_t(Mode::Detailed));
  const auto before=restored;
  for(size_t n:{size_t(0),size_t(8),size_t(103),size_t(199),sizeof(Users)+1}) {
    assert(!decodeUsers(&compact,n,42,restored) && !memcmp(&before,&restored,sizeof(before)));
  }
  assert(!decodeUsers(&compact,storedUsersBytes(compact),43,restored));
  compact.entries[7]=compact.entries[1];compact.entries[7].chat=88;
  assert(storedUsersBytes(compact)==sizeof(Users));
  assert(decodeUsers(&compact,storedUsersBytes(compact),42,restored) && restored.find(88)==7);
  assert(compact.remove(88) && compact.remove(77) && storedUsersBytes(compact)==104);
  assert(decodeUsers(&compact,storedUsersBytes(compact),42,restored) && restored.find(88)<0 && restored.find(77)<0);
}
