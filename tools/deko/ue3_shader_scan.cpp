// Offline shader collection from Unreal Engine 3 packages: decompress every compressed chunk
// of each package (tag 0x9E2A83C1; 128 KB blocks compressed with LZO1X, CompressionFlags = 2, which is
// what Mass Effect's packages use), scan the result for 2008 shader
// containers (0x102A11xx with coherent virtual/physical sizes) and write each distinct one
// as {vs,ps}_<fnv64>.bin, the same naming as app/src/me_shader_dump.cpp, so both sources merge.
// A match whose header fails the runtime's layout checks (container_check.h) is random package data, not a
// shader: it is counted as a false match and not written.
//   usage: ue3_shader_scan <output folder> <package files...>
// No dependencies: the LZO1X decoder is below.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

#include "container_check.h"

#ifdef SCAN_ZLIB
// Dishonored (2026-11-12): UE3's shader cache passes appDecompressMemory flags 20 (LZX) or 17 (ZLIB | bias memory)
// (sub_823A94E8): zlib streams inside the decompressed packages are inflated and scanned too (SCAN_ZLIB=1 at run time).
#include <zlib.h>
#endif

#ifdef SCAN_LZX
// Dishonored (2026-11-08): its packages compress with XMem LZX (CompressionFlags = 4), not LZO. Built with the SDK's
// libmspack (lzxd.c, system.c; collect_shaders.sh adds them): the same frame format and decoder call as ME2's native
// LZX (app/me2_src/hot/n/n_8279B210.h).
extern "C" {
#include "mspack.h"
#include "lzx.h"
}
#endif

namespace {

#ifdef SCAN_LZX
struct MemFile {
  mspack_system sys;  // first member: lzxd hands the file back as an mspack_file *
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

// XMemCompress frames ([0xFF, u16 uncompressed, u16 compressed] or [u16 compressed] for a full 32 KB frame,
// big-endian, then its LZX bits; compressed size 0 ends) -> one LZX stream, window 2^17, decoded by lzxd.
bool Lzx(const uint8_t* src, size_t len, uint8_t* out, size_t out_len) {
  std::vector<uint8_t> stream;
  stream.reserve(len);
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
#endif

// LZO1X decompression (the lzo1x_decompress_safe algorithm), bounds-checked.
// Mass Effect's packages use COMPRESS_LZO (CompressionFlags = 2).
bool Lzo(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
  const uint8_t* ip = in;
  const uint8_t* const ie = in + in_len;
  uint8_t* op = out;
  uint8_t* const oe = out + out_len;
  size_t t = 0;
  const uint8_t* m;
  auto need_in = [&](size_t n) { return size_t(ie - ip) >= n; };
  auto need_out = [&](size_t n) { return size_t(oe - op) >= n; };
  auto lit = [&](size_t n) {
    if (!need_in(n) || !need_out(n)) return false;
    std::memcpy(op, ip, n); op += n; ip += n; return true;
  };
  auto copy = [&](const uint8_t* from, size_t n) {
    if (from < out || !need_out(n)) return false;
    for (size_t i = 0; i < n; ++i) op[i] = from[i];
    op += n; return true;
  };
  auto run = [&](size_t base) -> bool {
    size_t v = base;
    while (need_in(1) && *ip == 0) { v += 255; ++ip; }
    if (!need_in(1)) return false;
    t = v + *ip++;
    return true;
  };
  if (!need_in(1)) return false;
  bool first = false;
  if (*ip > 17) {
    t = *ip++ - 17;
    if (t < 4) goto match_next;
    if (!lit(t)) return false;
    first = true;
  }
  for (;;) {
    if (!first) {
      if (!need_in(1)) return false;
      t = *ip++;
      if (t >= 16) goto match;
      if (t == 0 && !run(15)) return false;
      if (!lit(t + 3)) return false;
    }
    first = false;
    if (!need_in(1)) return false;
    t = *ip++;
    if (t < 16) {
      if (!need_in(1)) return false;
      m = op - (1 + 0x0800) - (t >> 2) - (size_t(*ip++) << 2);
      if (!copy(m, 3)) return false;
      goto match_done;
    }
  match:
    for (;;) {
      if (t >= 64) {
        if (!need_in(1)) return false;
        m = op - 1 - ((t >> 2) & 7) - (size_t(*ip++) << 3);
        t = (t >> 5) - 1;
        if (!copy(m, t + 2)) return false;
      } else if (t >= 32) {
        t &= 31;
        if (t == 0 && !run(31)) return false;
        if (!need_in(2)) return false;
        m = op - 1 - ((ip[0] >> 2) + (size_t(ip[1]) << 6));
        ip += 2;
        if (!copy(m, t + 2)) return false;
      } else if (t >= 16) {
        m = op - (size_t(t & 8) << 11);
        t &= 7;
        if (t == 0 && !run(7)) return false;
        if (!need_in(2)) return false;
        m -= (ip[0] >> 2) + (size_t(ip[1]) << 6);
        ip += 2;
        if (m == op) return op == oe;  // end of stream
        m -= 0x4000;
        if (!copy(m, t + 2)) return false;
      } else {
        if (!need_in(1)) return false;
        m = op - 1 - (t >> 2) - (size_t(*ip++) << 2);
        if (!copy(m, 2)) return false;
      }
    match_done:
      t = ip[-2] & 3;
      if (t == 0) break;
    match_next:
      if (!lit(t)) return false;
      if (!need_in(1)) return false;
      t = *ip++;
    }
  }
}

uint32_t BE(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

uint64_t Fnv(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: ue3_shader_scan <out> <packages...>\n");
    return 1;
  }
  const std::filesystem::path outdir = argv[1];
  std::filesystem::create_directories(outdir);
  std::set<uint64_t> seen;
  size_t packages = 0, chunks = 0, bad_blocks = 0, found = 0, written = 0, false_matches = 0;
  size_t nested_blocks = 0, nested_bad = 0, nested_relaxed = 0, zlib_blocks = 0, zlib_bytes = 0;  // Dishonored: LZX blocks inside the packages (shader code cache)
  for (int a = 2; a < argc; ++a) {
    std::ifstream f(argv[a], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    ++packages;
    std::vector<uint8_t> plain;
    // Compressed chunks: each starts with the tag twice, then total compressed and
    // uncompressed sizes, then (compressed, uncompressed) per 128 KB block, then the blocks.
    // Seen in Mass Effect's summaries with CompressionFlags = 2 (LZO). Chunks are found by
    // that header pattern (the chunk table offsets vary with the summary's name/generation
    // tables).
    // Dishonored (2026-11-12): from offset 0. A fully compressed package (Startup.xxx, DishonoredGame.xxx, Engine.xxx
    // and the TU's patched ones) is one chunk that starts at 0; from 4 the whole file was never decompressed.
    for (size_t off = 0; off + 16 <= d.size(); ++off) {
      // ME2: the second word is the block size (0x20000), standard UE3; Mass Effect 1 repeats the tag there.
      if (BE(&d[off]) != 0x9E2A83C1u || (BE(&d[off + 4]) != 0x9E2A83C1u && BE(&d[off + 4]) != 0x20000u)) continue;
      const uint32_t csize = BE(&d[off + 8]), usize = BE(&d[off + 12]);
      if (usize == 0 || usize > 0x4000000 || csize == 0 || csize > d.size()) continue;
      const size_t nblocks = (usize + 0x1FFFF) / 0x20000;
      const size_t hdr = off + 16;
      size_t data = hdr + nblocks * 8;
      if (data > d.size()) continue;
      uint64_t sum_c = 0, sum_u = 0;
      for (size_t k = 0; k < nblocks; ++k) {
        sum_c += BE(&d[hdr + k * 8]);
        sum_u += BE(&d[hdr + k * 8 + 4]);
      }
      if (sum_c != csize || sum_u != usize || data + csize > d.size()) continue;
      std::vector<uint8_t> out(usize);
      size_t produced = 0;
      bool ok = true;
      size_t good = 0;
      for (size_t k = 0; k < nblocks; ++k) {
        const uint32_t bc = BE(&d[hdr + k * 8]), bu = BE(&d[hdr + k * 8 + 4]);
        bool block_ok = Lzo(&d[data], bc, out.data() + produced, bu);
#ifdef SCAN_LZX
        if (!block_ok) {  // Lzo's partial output out, so a failed Lzx leaves its decoded frames and zeros after them
          std::fill(out.begin() + produced, out.begin() + produced + bu, uint8_t(0));
          block_ok = Lzx(&d[data], bc, out.data() + produced, bu);
        }
#endif
        if (block_ok) ++good;
        if (!block_ok) {
          ++bad_blocks;
          // Dishonored (2026-11-12): a block lzxd refuses no longer drops its whole chunk (up to 1.4 MB of a package:
          // e.g. L_Ovrsr_Kennel_Script, 1 MB with possibly the Overseer level shader caches): the blocks are independent
          // streams, so only this one is lost, and only from the frame lzxd stops at (lzxd writes each frame it
          // finishes; the rest stays zero). A header match whose blocks all fail is still dropped.
          std::printf("BAD %s chunk at %zu: block %zu of %zu (%u -> %u bytes), chunk %u -> %u bytes\n", argv[a], off, k,
                      nblocks, bc, bu, csize, usize);
        }
        produced += bu;
        data += bc;
      }
      ok = good > 0;
      if (!ok) continue;
      ++chunks;
      plain.insert(plain.end(), out.begin(), out.end());
      off = data - 1;
    }
    // Dishonored (2026-11-08): UE3's compressed shader code cache keeps the material shaders compressed a second
    // time inside the (already decompressed) package: a byte array of XMem LZX frames with the uncompressed size
    // next to it. Found by shape: a big-endian count n, then frames that use exactly n bytes, with the frames'
    // uncompressed total stored just before the count or just after the array. Each one is decoded and scanned too.
    std::vector<std::vector<uint8_t>> nested;
    // Dishonored (2026-11-12): a file without compressed chunks (GlobalShaderCache-Xbox360.bin, the cooker data) is
    // searched for nested blocks as it is.
    if (plain.empty()) plain = d;
    // SCAN_DUMP_PLAIN=<folder>: the decompressed package as <folder>/<file name> (for a fully compressed package, one
    // chunk from offset 0, exactly the package the title update's BSDIFF40 patches apply to).
    if (const char* dump = std::getenv("SCAN_DUMP_PLAIN")) {
      std::ofstream o(std::filesystem::path(dump) / std::filesystem::path(argv[a]).filename(), std::ios::binary);
      o.write(reinterpret_cast<const char*>(plain.data()), std::streamsize(plain.size()));
    }
    // SCAN_FIND=<hex>,<hex>...: where these byte strings (e.g. the first 16 bytes of a compressed chunk dumped by the
    // game's dh_shader_trace) are in the decompressed package, with the 16 bytes before and after the match.
    if (const char* find = std::getenv("SCAN_FIND")) {
      static const std::vector<std::vector<uint8_t>> needles = [find] {
        std::vector<std::vector<uint8_t>> v;
        std::string s(find);
        size_t at = 0;
        while (at < s.size()) {
          size_t end = s.find(',', at);
          if (end == std::string::npos) end = s.size();
          std::vector<uint8_t> n;
          for (size_t k = at; k + 1 < end; k += 2) n.push_back(uint8_t(std::stoul(s.substr(k, 2), nullptr, 16)));
          if (!n.empty()) v.push_back(n);
          at = end + 1;
        }
        return v;
      }();
      auto hex = [](const uint8_t* p, size_t n) {
        std::string o;
        char b[4];
        for (size_t k = 0; k < n; ++k) { std::snprintf(b, sizeof(b), "%02x", p[k]); o += b; }
        return o;
      };
      for (const auto& n : needles) {
        for (auto it = plain.begin(); (it = std::search(it, plain.end(), n.begin(), n.end())) != plain.end(); ++it) {
          const size_t at = size_t(it - plain.begin());
          const size_t lo = at >= 16 ? at - 16 : 0;
          std::printf("FIND %s in %s at %zu (plain %zu, chunks %zu): before %s\n", hex(n.data(), 4).c_str(), argv[a],
                      at, plain.size(), chunks, hex(&plain[lo], at - lo).c_str());
        }
      }
    }
#ifdef SCAN_LZX
    for (size_t i = 4; i + 8 <= plain.size(); ++i) {
      const uint32_t n = BE(&plain[i]);
      if (n < 16 || n > 0x800000 || i + 4 + n > plain.size()) continue;
      const uint8_t* s = &plain[i + 4];
      // The frames must fill the array exactly (the end marker, if any, inside it).
      size_t k = 0;
      uint64_t total = 0;
      bool ok = true;
      while (k < n) {
        uint32_t u = 32768, c;
        if (s[k] == 0xFF) {
          if (k + 5 > n) { ok = false; break; }
          u = (uint32_t(s[k + 1]) << 8) | s[k + 2];
          c = (uint32_t(s[k + 3]) << 8) | s[k + 4];
          k += 5;
        } else {
          if (k + 2 > n) { ok = false; break; }
          c = (uint32_t(s[k]) << 8) | s[k + 1];
          k += 2;
        }
        if (c == 0) {  // end marker: only zero padding may follow
          while (k < n && s[k] == 0) ++k;
          // Dishonored (2026-11-12, the shaders no file seemed to hold): the game's compressed shader chunks end
          // with a few non-zero bytes after the end marker (the compressor's buffer slack, kept in the array:
          // e.g. 00 00 | 59 29 11 2B). Dropping such arrays lost whole chunks (traced at run time through
          // sub_823A94E8). Up to 16 bytes of anything are accepted; lzxd and the adjacent size still decide.
          if (n - k <= 16) k = n;
          break;
        }
        if (c > 0x9000 || u == 0 || k + c > n) { ok = false; break; }
        k += c;
        total += u;
      }
      if (!ok || k != n || total == 0 || total > 0x4000000) continue;
      const bool before = BE(&plain[i - 4]) == total;
      const bool after = i + 8 + n <= plain.size() && BE(&plain[i + 4 + n]) == total;
      // Dishonored (2026-11-12, the shaders no file seemed to hold): SCAN_RELAXED=1 also tries arrays whose
      // uncompressed size is not stored next to them (their frames give it); lzxd decides. Such a block that does not
      // decode is not counted as failed (most are not LZX at all).
      static const bool relaxed = std::getenv("SCAN_RELAXED") != nullptr;
      if (!before && !after && !relaxed) continue;
      const bool adjacent = before || after;
      std::vector<uint8_t> out(total);
      if (Lzx(s, n, out.data(), out.size())) {
        nested.push_back(std::move(out));
        ++nested_blocks;
        if (!adjacent) ++nested_relaxed;
        i += 3 + n;  // past the array
      } else if (!adjacent) {
        continue;
      } else {
        ++nested_bad;
        // SCAN_DUMP_FAILED=<folder>: the blocks lzxd refuses (the count, the array and 8 bytes around), to study.
        if (const char* dump = std::getenv("SCAN_DUMP_FAILED")) {
          char name[96];
          std::snprintf(name, sizeof(name), "/failed_%zu_%zu_%s.bin", packages, i, before ? "before" : "after");
          std::ofstream o(std::string(dump) + name, std::ios::binary);
          o.write(reinterpret_cast<const char*>(&plain[i - 4]), std::streamsize(std::min(plain.size() - (i - 4), size_t(n) + 16)));
        }
      }
    }
#endif
#ifdef SCAN_ZLIB
    static const bool zlib_on = std::getenv("SCAN_ZLIB") != nullptr;
    if (zlib_on) {
      std::vector<uint8_t> inflated;
      for (size_t i = 0; i + 8 <= plain.size(); ++i) {
        const uint8_t cmf = plain[i], flg = plain[i + 1];
        if (cmf != 0x78 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) continue;
        z_stream z{};
        if (inflateInit(&z) != Z_OK) continue;
        z.next_in = const_cast<Bytef*>(&plain[i]);
        z.avail_in = uInt(std::min<size_t>(plain.size() - i, 0x1000000));
        inflated.resize(0x10000);
        size_t have = 0;
        int rc = Z_OK;
        while (rc == Z_OK) {
          if (have == inflated.size()) {
            if (inflated.size() >= 0x2000000) break;
            inflated.resize(inflated.size() * 2);
          }
          z.next_out = inflated.data() + have;
          z.avail_out = uInt(inflated.size() - have);
          rc = inflate(&z, Z_NO_FLUSH);
          have = inflated.size() - z.avail_out;
        }
        const size_t used = z.total_in;
        inflateEnd(&z);
        if (rc != Z_STREAM_END || have < 64) continue;
        inflated.resize(have);
        nested.push_back(inflated);
        ++zlib_blocks;
        zlib_bytes += have;
        i += used - 1;
      }
    }
#endif
    // Scan the uncompressed data (and the raw file, for uncompressed packages), then the nested blocks.
    std::vector<const std::vector<uint8_t>*> buffers{&plain, &d};
    for (const auto& b : nested) buffers.push_back(&b);
    for (const std::vector<uint8_t>* buf : buffers) {
      const auto& b = *buf;
      for (size_t i = 0; i + 24 <= b.size(); ++i) {
        const uint32_t sig = BE(&b[i]);
        if ((sig & 0xFFFFFF00u) != 0x102A1100u) continue;
        const uint32_t vs = BE(&b[i + 4]), ps = BE(&b[i + 8]);
        if (vs < 24 || vs > 0x40000 || ps == 0 || ps > 0x40000 || i + vs + ps > b.size()) continue;
        if (shader_container::LayoutProblem(&b[i], vs + ps)) {
          ++false_matches;
          continue;
        }
        ++found;
        const uint64_t h = Fnv(&b[i], vs + ps);
        if (!seen.insert(h).second) continue;
        char name[64];
        std::snprintf(name, sizeof(name), "%s_%016llx.bin", (sig & 1) ? "vs" : "ps",
                      static_cast<unsigned long long>(h));
        const auto path = outdir / name;
        if (!std::filesystem::exists(path)) {
          std::ofstream o(path, std::ios::binary);
          o.write(reinterpret_cast<const char*>(&b[i]), vs + ps);
          ++written;
        }
      }
    }
  }
  if (zlib_blocks) std::printf("SCAN_ZLIB: %zu zlib streams inflated (%zu MB)\n", zlib_blocks, zlib_bytes >> 20);
  if (nested_relaxed) std::printf("SCAN_RELAXED: %zu nested blocks decoded without a size next to them\n", nested_relaxed);
  std::printf("%zu packages, %zu chunks, %zu bad blocks, %zu nested LZX blocks (%zu failed), %zu containers seen, "
              "%zu distinct, %zu new files, %zu false matches dropped\n",
              packages, chunks, bad_blocks, nested_blocks, nested_bad, found, seen.size(), written, false_matches);
  return 0;
}
