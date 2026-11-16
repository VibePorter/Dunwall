// Dishonored (2026-11-10): writes a UE3 package uncompressed, for tools that read its tables (names, exports, texture
// data): the summary as it is, then every compressed chunk's data at its uncompressed offset. Chunks are found as in
// ue3_shader_scan.cpp (tag 0x9E2A83C1, block size 0x20000, then the block table; LZX for Dishonored, LZO for Mass
// Effect); each chunk's uncompressed offset comes from the summary's chunk table, the four big-endian words
// (uncompressed offset, uncompressed size, compressed offset, compressed size) whose compressed offset is the chunk's.
// The summary's compression flags and chunk count are left as they are (readers skip them).
//   usage: ue3_decompress <package> <out file>
// Built like ue3_shader_scan (tools/deko/collect_shaders.sh): -DSCAN_LZX with the SDK's libmspack lzxd.c / system.c.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

bool Lzx(const uint8_t* src, size_t len, uint8_t* out, size_t out_len) {
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
  lzxd_stream* s = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&in), reinterpret_cast<mspack_file*>(&dst), 17, 0,
                             0x8000, off_t(out_len), 0);
  if (!s) return false;
  const int rc = lzxd_decompress(s, off_t(out_len));
  lzxd_free(s);
  return rc == MSPACK_ERR_OK && dst.off == out_len;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: ue3_decompress <package> <out file>\n");
    return 1;
  }
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  if (d.size() < 64 || BE(d.data()) != 0x9E2A83C1u) {
    std::fprintf(stderr, "%s: not a big-endian UE3 package\n", argv[1]);
    return 1;
  }
  struct Chunk {
    size_t comp_off, comp_size, uoff;
    std::vector<uint8_t> data;
  };
  // A fully compressed file (Startup, GFxUI, the _LOC_ packages): one chunk at offset 0 holding the whole package.
  if (BE(&d[4]) == 0x20000u) {
    const uint32_t csize = BE(&d[8]), usize = BE(&d[12]);
    const size_t nblocks = (usize + 0x1FFFF) / 0x20000;
    size_t data = 16 + nblocks * 8;
    std::vector<uint8_t> whole(usize);
    size_t produced = 0;
    for (size_t k = 0; k < nblocks; ++k) {
      const uint32_t bc = BE(&d[16 + k * 8]), bu = BE(&d[16 + k * 8 + 4]);
      if (data + bc > d.size() || produced + bu > usize ||
          !(bc == bu ? (std::memcpy(whole.data() + produced, &d[data], bu), true)
                     : Lzx(&d[data], bc, whole.data() + produced, bu))) {
        std::fprintf(stderr, "%s: fully compressed, block %zu did not decode\n", argv[1], k);
        return 1;
      }
      produced += bu;
      data += bc;
    }
    (void)csize;
    d = std::move(whole);
    std::printf("%s: fully compressed, %zu bytes\n", argv[1], d.size());
  }
  std::vector<Chunk> chunks;
  size_t first = d.size();
  for (size_t off = 4; off + 16 <= d.size(); ++off) {
    if (BE(&d[off]) != 0x9E2A83C1u || BE(&d[off + 4]) != 0x20000u) continue;
    const uint32_t csize = BE(&d[off + 8]), usize = BE(&d[off + 12]);
    if (usize == 0 || usize > 0x4000000 || csize == 0 || csize > d.size()) continue;
    const size_t nblocks = (usize + 0x1FFFF) / 0x20000, hdr = off + 16;
    size_t data = hdr + nblocks * 8;
    if (data > d.size()) continue;
    uint64_t sum_c = 0, sum_u = 0;
    for (size_t k = 0; k < nblocks; ++k) {
      sum_c += BE(&d[hdr + k * 8]);
      sum_u += BE(&d[hdr + k * 8 + 4]);
    }
    if (sum_c != csize || sum_u != usize || data + csize > d.size()) continue;
    Chunk c;
    c.comp_off = off;
    c.comp_size = data + csize - off;
    c.data.resize(usize);
    size_t produced = 0;
    bool ok = true;
    for (size_t k = 0; k < nblocks && ok; ++k) {
      const uint32_t bc = BE(&d[hdr + k * 8]), bu = BE(&d[hdr + k * 8 + 4]);
      ok = bc == bu ? (std::memcpy(c.data.data() + produced, &d[data], bu), true)
                    : Lzx(&d[data], bc, c.data.data() + produced, bu);
      produced += bu;
      data += bc;
    }
    if (!ok) {
      std::fprintf(stderr, "chunk at %zu: a block did not decode\n", off);
      return 1;
    }
    first = std::min(first, off);
    chunks.push_back(std::move(c));
    off = data - 1;
  }
  if (chunks.empty()) {
    // Not compressed: the file as it is.
    std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    std::printf("%s: no compressed chunks, copied\n", argv[1]);
    return 0;
  }
  // Each chunk's uncompressed offset from the summary's table (before the first chunk).
  size_t end = 0;
  for (Chunk& c : chunks) {
    bool found = false;
    for (size_t p = 8; p + 8 <= first && !found; ++p) {
      if (BE(&d[p]) == c.comp_off && BE(&d[p - 4]) == c.data.size() && p >= 8) {
        c.uoff = BE(&d[p - 8]);
        found = true;
      }
    }
    if (!found) {
      std::fprintf(stderr, "chunk at %zu: not in the summary's chunk table\n", c.comp_off);
      return 1;
    }
    end = std::max(end, c.uoff + c.data.size());
  }
  std::vector<uint8_t> out(std::max(end, first), 0);
  std::memcpy(out.data(), d.data(), first);
  for (const Chunk& c : chunks) std::memcpy(out.data() + c.uoff, c.data.data(), c.data.size());
  std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
  std::printf("%s: %zu chunks, %zu bytes\n", argv[1], chunks.size(), out.size());
  return 0;
}
