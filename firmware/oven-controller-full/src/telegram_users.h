#pragma once
#include "telegram_policy.h"
#include <cstddef>
#include <cstdio>

namespace telegram {
constexpr size_t kMaxUsers = 8;
constexpr uint32_t kUsersVersion = 1, kJoinLifetimeMs = 10UL * 60000UL;
inline void copyUserText(char *out,size_t capacity,const char *text){
  if(!capacity)return;
  size_t bytes=strlen(text);if(bytes>=capacity){bytes=capacity-1;while(bytes&&(uint8_t(text[bytes])&0xc0)==0x80)--bytes;}
  memcpy(out,text,bytes);out[bytes]=0;
}
inline bool privateSender(int64_t chat, int64_t sender, const char *type) {
  return chat > 0 && chat == sender && type && !strcmp(type,"private");
}
inline int modeIndex(const char *key) {
  for (int i=0;i<3;++i) if (key && !strcmp(key,modeKey(modeFrom(i)))) return i;
  return -1;
}
struct User {
  int64_t chat = 0;
  char name[49]{}, username[33]{};
  uint8_t mode = uint8_t(Mode::Brief);
};
struct Users {
  uint32_t version = kUsersVersion;
  User entries[kMaxUsers]{};
  void begin(int64_t owner, Mode mode) {
    *this=Users{}; entries[0].chat=owner; entries[0].mode=uint8_t(mode);
    snprintf(entries[0].name,sizeof(entries[0].name),"Proprietario");
  }
  int find(int64_t chat) const {
    if (chat<=0) return -1;
    for (size_t i=0;i<kMaxUsers;++i) if (entries[i].chat==chat) return int(i);
    return -1;
  }
  bool valid(int64_t owner) const {
    if (version!=kUsersVersion || owner<=0 || entries[0].chat!=owner) return false;
    for (size_t i=0;i<kMaxUsers;++i) {
      const auto &u=entries[i];
      if (u.chat<0 || !validMode(u.mode) || !memchr(u.name,0,sizeof(u.name)) || !memchr(u.username,0,sizeof(u.username))) return false;
      if (u.chat && find(u.chat)!=int(i)) return false;
    }
    return true;
  }
  size_t count() const { size_t n=0; for (const auto &u:entries) if(u.chat>0) ++n; return n; }
  bool add(const User &user) {
    if (user.chat<=0 || find(user.chat)>=0 || !validMode(user.mode)) return false;
    for (size_t i=1;i<kMaxUsers;++i) if(!entries[i].chat) {entries[i]=user;return true;}
    return false;
  }
  bool remove(int64_t chat) { const int i=find(chat);if(i<=0)return false;entries[i]=User{};return true; }
  bool setMode(int64_t chat, int mode) {
    const int i=find(chat);if(i<0||!validMode(mode))return false;entries[i].mode=uint8_t(mode);return true;
  }
};
// Preserve the existing layout, but stop reserving flash for unused trailing
// slots. Holes stay in place; the owner's slot and per-user modes never move.
inline size_t storedUsersBytes(const Users &users) {
  size_t slots = kMaxUsers;
  while (slots > 1 && !users.entries[slots - 1].chat) --slots;
  return offsetof(Users, entries) + slots * sizeof(User);
}
inline bool decodeUsers(const void *blob, size_t length, int64_t owner, Users &out) {
  constexpr size_t start = offsetof(Users, entries);
  if (!blob || length < start + sizeof(User) || length > sizeof(Users) || (length - start) % sizeof(User)) return false;
  Users candidate{}; memcpy(&candidate, blob, length);
  if (!candidate.valid(owner)) return false;
  out = candidate; return true;
}
struct Join { User user; uint32_t created=0; };
template<class Save>
bool commitUsers(Users &current,const Users &proposed,Save save){
  if(!proposed.valid(current.entries[0].chat)||!save(proposed))return false;
  current=proposed;return true;
}
class PendingUsers {
public:
  Join entries[kMaxUsers]{};
  void expire(uint32_t now) { for(auto &j:entries) if(j.user.chat && uint32_t(now-j.created)>=kJoinLifetimeMs)j=Join{}; }
  int find(int64_t chat) const { for(size_t i=0;i<kMaxUsers;++i)if(chat>0&&entries[i].user.chat==chat)return int(i);return -1; }
  bool add(const User &user,uint32_t now) {
    expire(now); if(user.chat<=0||find(user.chat)>=0)return false;
    for(auto &j:entries)if(!j.user.chat){j.user=user;j.created=now;return true;}
    return false;
  }
  bool remove(int64_t chat) {int i=find(chat);if(i<0)return false;entries[i]=Join{};return true;}
};
struct UserRequest {
  User user;
  Command command=Command::None; // None means a private /start requesting access.
  uint32_t created=0;
};
} // namespace telegram
