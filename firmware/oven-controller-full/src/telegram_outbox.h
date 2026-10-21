#pragma once
#include <SPIFFS.h>
#include <esp_partition.h>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include "durable_outbox.h"

namespace telegram {
struct SpiffsOutboxDisk {
  using File = fs::File;
  static bool blankPartition() {
    const auto *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!p) return false;
    uint8_t bytes[512];
    for (size_t at=0;at<p->size;at+=sizeof(bytes)) {
      if (esp_partition_read(p,at,bytes,sizeof(bytes))!=ESP_OK)return false;
      for(auto byte:bytes)if(byte!=0xff)return false;
      if(!(at%16384))vTaskDelay(1);
    }
    return true;
  }
  bool begin() {
    if(SPIFFS.begin(false,"/oven-outbox",6,"spiffs"))return true;
    // Never format a nonempty/unreadable partition. The existing layout stays
    // unchanged; only the previously unused, fully erased SPIFFS area is created.
    return blankPartition() && SPIFFS.format() && SPIFFS.begin(false,"/oven-outbox",6,"spiffs");
  }
  File open(const std::string &p,const char *mode){return SPIFFS.open(p.c_str(),mode);}
  bool exists(const std::string &p){return SPIFFS.exists(p.c_str());}
  bool remove(const std::string &p){return SPIFFS.remove(p.c_str());}
  bool rename(const std::string &a,const std::string &b){return SPIFFS.rename(a.c_str(),b.c_str());}
  bool sync(const std::string &p){
    const std::string full="/oven-outbox"+p;
    const int fd=::open(full.c_str(),O_RDWR);
    if(fd<0)return false;
    const bool ok=::fsync(fd)==0;::close(fd);return ok;
  }
  size_t availableBytes(){return SPIFFS.totalBytes()-SPIFFS.usedBytes();}
  uint64_t now(){const auto t=time(nullptr);return t>=1700000000?uint64_t(t):0;}
  void pause(){vTaskDelay(1);}
};
using DurableOutbox = BasicOutbox<SpiffsOutboxDisk>;
} // namespace telegram
