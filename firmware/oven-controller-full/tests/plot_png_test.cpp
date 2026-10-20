#include <cassert>
#include <fstream>
#include <vector>
#include "../src/plot_png.h"
int main(int argc, char **argv) {
  std::vector<char> image(plotpng::kCapacity);
  std::vector<uint8_t> raster(plotpng::kRasterBytes);
  std::vector<reportpdf::Point> points;
  const uint32_t start = UINT32_MAX - 500000;
  for (unsigned i = 0; i <= 180; ++i) {
    const float target = i < 90 ? 100 + i * 75.f / 90 : 175;
    points.push_back({start + i * 10000, i == 100 ? NAN : target - 1 + .3f * std::sin(float(i)), target});
  }
  const size_t size = plotpng::generate(image.data(), image.size(), raster.data(), raster.size(),
    points.data(), points.size(), start, start + 1800000);
  assert(size > 400000 && size < image.size());
  assert(!memcmp(image.data(), "\x89PNG\r\n\x1a\n", 8));
  assert(!plotpng::generate(image.data(), 1024, raster.data(), raster.size(), points.data(), points.size(), start, start + 1800000));
  assert(!plotpng::generate(image.data(), image.size(), nullptr, 0, points.data(), points.size(), start, start + 1800000));
  if (argc > 1) { std::ofstream f(argv[1], std::ios::binary); f.write(image.data(), size); assert(f.good()); }
}
