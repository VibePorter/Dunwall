// Dishonored (2026-11-10): which LZX window decodes the nested blocks ue3_shader_scan could not (SCAN_DUMP_FAILED files:
// 4 bytes before the array, the count n, the n bytes of XMem frames, 8 bytes after). Tries window sizes 2^15..2^21 and
// a frame-by-frame reset, and prints a tally.
//   lzx_probe <failed files...>
// Built like ue3_shader_scan (-DSCAN_LZX not needed: libmspack's lzxd.c / system.c).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

extern "C" {
#include "mspack.h"
#include "lzx.h"
}

namespace {
uint32_t BE(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
struct MemFile {
  mspack_system sys;
  const uint8_t* in = nullptr;
  uint8_t* out = nullptr;
  size_t size = 0, off = 0;
};
int FileRead(mspack_file* f, void* buf, int n) {
  auto* m = reinterpret_cast<MemFile*>(f);
  const size_t t = std::min<size_t>(size_t(n), m->size - m->off);
  std::memcpy(buf, m->in + m->off, t);
  m->off += t;
  return int(t);
}
int FileWrite(mspack_file* f, void* buf, int n) {
  auto* m = reinterpret_cast<MemFile*>(f);
  const size_t t = std::min<size_t>(size_t(n), m->size - m->off);
  std::memcpy(m->out + m->off, buf, t);
  m->off += t;
  return int(t);
}
void* Alloc(mspack_system*, size_t n) { return std::calloc(1, n); }
void Free(void* p) { std::free(p); }
void Copy(void* src, void* dst, size_t n) { std::memcpy(dst, src, n); }

// The frames as one stream (as ue3_shader_scan), decoded with window 2^bits.
bool Lzx(const uint8_t* src, size_t len, uint8_t* out, size_t out_len, int bits) {
  std::vector<uint8_t> stream;
  size_t i = 0;
  while (i + 2 <= len) {
    uint32_t c;
    if (src[i] == 0xFF) {
      if (i + 5 > len) return false;
      c = (uint32_t(src[i + 3]) << 8) | src[i + 4];
      i += 5;
    } else {
      c = (uint32_t(src[i]) << 8) | src[i + 1];
      i += 2;
    }
    if (c == 0) break;
    if (i + c > len) return false;
    stream.insert(stream.end(), src + i, src + i + c);
    i += c;
  }
  if (stream.empty()) return false;
  mspack_system sys{};
  sys.read = &FileRead;
  sys.write = &FileWrite;
  sys.alloc = &Alloc;
  sys.free = &Free;
  sys.copy = &Copy;
  MemFile in, dst;
  in.sys = sys;
  in.in = stream.data();
  in.size = stream.size();
  dst.sys = sys;
  dst.out = out;
  dst.size = out_len;
  lzxd_stream* s = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&in), reinterpret_cast<mspack_file*>(&dst), bits, 0,
                             0x8000, off_t(out_len), 0);
  if (!s) return false;
  const int rc = lzxd_decompress(s, off_t(out_len));
  lzxd_free(s);
  return rc == MSPACK_ERR_OK && dst.off == out_len;
}
}  // namespace

int main(int argc, char** argv) {
  std::map<int, int> ok;
  int none = 0, total = 0;
  std::map<uint32_t, int> first_bytes;
  for (int a = 1; a < argc; ++a) {
    std::ifstream f(argv[a], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    if (d.size() < 12) continue;
    ++total;
    const uint32_t n = BE(&d[4]);
    if (8 + size_t(n) > d.size()) continue;
    const bool after_named = std::strstr(argv[a], "after") != nullptr;
    const uint32_t out_len = after_named && 8 + size_t(n) + 4 <= d.size() ? BE(&d[8 + n]) : BE(&d[0]);
    ++first_bytes[d[8]];
    std::vector<uint8_t> out(out_len);
    int got = 0;
    for (int bits = 15; bits <= 21 && !got; ++bits)
      if (Lzx(&d[8], n, out.data(), out.size(), bits)) got = bits;
    if (got)
      ++ok[got];
    else
      ++none;
    if (a <= 4)
      std::printf("%s: n %u, out %u, first frame bytes %02X %02X %02X %02X %02X -> %s %d\n", argv[a], n, out_len, d[8],
                  d[9], d[10], d[11], d[12], got ? "window 2^" : "no window", got);
  }
  std::printf("%d failed blocks: ", total);
  for (auto& kv : ok) std::printf("window 2^%d decodes %d, ", kv.first, kv.second);
  std::printf("none %d\n", none);
  std::printf("first bytes:");
  for (auto& kv : first_bytes) std::printf(" %02X x%d", kv.first, kv.second);
  std::printf("\n");
  return 0;
}
