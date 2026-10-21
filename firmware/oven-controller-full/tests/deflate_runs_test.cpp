#include <cassert>
#include <vector>
#include <fstream>
#include "../src/deflate_rle.h"

int main(int argc,char **argv) {
  std::vector<std::vector<uint8_t>> inputs{{},{0},{255},{0,0},{1,2,3,144,255}};
  for(size_t n:{3,10,258,259,516,65536})inputs.emplace_back(n,0xff);
  std::vector<uint8_t> random(65536);uint32_t state=42;
  for(auto &byte:random){state=state*1664525+1013904223;byte=uint8_t(state>>24);}inputs.push_back(random);
  std::ofstream file;if(argc>1)file.open(argv[1],std::ios::binary);
  for(const auto &input:inputs){
    std::vector<uint8_t> output(input.size()*2+64);
    const size_t n=plotpng::deflateRuns(output.data(),output.size(),input.data(),input.size());assert(n>0);
    if(file){
      const uint32_t sizes[]={uint32_t(input.size()),uint32_t(n)};
      file.write(reinterpret_cast<const char *>(sizes),sizeof(sizes));
      file.write(reinterpret_cast<const char *>(input.data()),input.size());
      file.write(reinterpret_cast<const char *>(output.data()),n);
    }
    for(size_t capacity=0;capacity<80;++capacity){
      std::vector<uint8_t> small(capacity+32,0xa5);
      plotpng::deflateRuns(small.data(),capacity,input.data(),input.size());
      for(size_t i=capacity;i<small.size();++i)assert(small[i]==0xa5);
    }
  }
}
