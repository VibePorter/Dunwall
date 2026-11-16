// Finds the shader container whose XenosRecomp hash (XXH3_64bits over virtualSize + physicalSize bytes, the same
// container walk as XenosRecomp's main.cpp) matches argv[2]. usage: find_xxh3 <folder> <hex hash>
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
static uint32_t BE(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
int main(int argc, char** argv) {
  const uint64_t want = std::strtoull(argv[2], nullptr, 16);
  for (auto& f : std::filesystem::directory_iterator(argv[1])) {
    std::ifstream in(f.path(), std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
    for (size_t i = 0; d.size() > 0x24 && i < d.size() - 0x24 - 1;) {
      const uint8_t* c = d.data() + i;
      const size_t size = size_t(BE(c + 4)) + BE(c + 8);
      if ((BE(c) & 0xFFFFFF00u) == 0x102A1100u && size <= d.size() - i && BE(c + 0x1C) == 0 && BE(c + 0x20) == 0) {
        if (XXH3_64bits(c, size) == want) {
          std::printf("found %s at offset %zu, %zu bytes\n", f.path().string().c_str(), i, size);
          return 0;
        }
        i += size;
      } else {
        i += 4;
      }
    }
  }
  std::printf("not found\n");
  return 1;
}
