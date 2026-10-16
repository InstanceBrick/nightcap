// Isolated CPU cost of regenerating one 81x81 map. This is not an FPS test.
#include "mocktail/vr/foveation_focus.h"
#include <chrono>
#include <cstdio>

int main() {
  constexpr unsigned iterations=1000;
  volatile unsigned checksum=0;
  const auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<iterations;++i) {
    const auto map=mocktail::vr::internal::GazeDensityMap(81,81,0,0,2,.2f,512.f/2592,512.f/2592);
    checksum=checksum+map[i%map.size()];
  }
  const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/iterations;
  std::printf("Map regeneration: %.2f microseconds per eye, %u iterations, checksum=%u; unchanged focus cache skips this work\n",us,iterations,unsigned(checksum));
}
