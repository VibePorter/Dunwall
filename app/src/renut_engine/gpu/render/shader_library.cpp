/**
 * @file    gpu/render/shader_library.cpp
 */
#include "renut_engine/gpu/render/shader_library.h"

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <smolv.h>
#include <xxhash.h>
#include <zstd.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"
#if defined(PLUME_DEKO3D)
#include "renut_engine/gpu/banjo_renut_deko3d.h"  // banjo-nx (2026-10-17): the deko3d back end's GLSL package
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <cstring>
#if defined(__SWITCH__) && defined(__aarch64__)
extern "C" char _start;  // the image's base (d3d/hooks/draw.cpp, rex_profile.log "image+0x...")
#endif
#endif

REXCVAR_DEFINE_STRING(renut_shader_capture_dir, "shader_capture", "Nuts&Bolts/Graphics",
                      "Where shaders missing from the shader cache are written; the next build "
                      "recompiles them into the cache");

REXCVAR_DEFINE_STRING(dh_shader_trace, "", "Dishonored/Diagnostics",
                      "Shader hashes (hex, comma separated) whose creation is logged with the guest call chain "
                      "([shader-trace]), whether the shader table has them or not")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace renut::gpu {

namespace {

struct DecompressedCache {
  std::once_flag once;
  std::vector<uint8_t> bytes;
};

DecompressedCache g_spirv;
#if defined(_WIN32)
DecompressedCache g_dxil;
#endif
std::atomic<uint64_t> g_decompressed_bytes{0};  // ShaderCacheHostBytes

const uint8_t *Decompress(DecompressedCache &cache, const uint8_t *compressed,
                          size_t compressed_size, size_t decompressed_size, const char *name) {
  std::call_once(cache.once, [&] {
    cache.bytes.resize(decompressed_size);
    const size_t n = ZSTD_decompress(cache.bytes.data(), decompressed_size, compressed,
                                     compressed_size);
    if (ZSTD_isError(n) || n != decompressed_size) {
      RGPU_ERROR("{} shader cache decompression failed", name);
      cache.bytes.clear();
      cache.bytes.shrink_to_fit();
    }
    g_decompressed_bytes.fetch_add(cache.bytes.capacity(), std::memory_order_relaxed);
  });
  return cache.bytes.empty() ? nullptr : cache.bytes.data();
}

std::mutex g_capture_mutex;
std::unordered_set<uint64_t> g_captured;  // CaptureMissingShader

} // namespace

uint64_t HashShaderContainer(const uint8_t *container) {
  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  return XXH3_64bits(container, size);
}

const ShaderCacheEntry *FindShaderEntry(uint64_t hash) {
  if (g_shaderCacheLayout != kShaderCacheLayout) {
    static bool logged = false;
    if (!logged) {
      RGPU_ERROR("Shader cache layout {} does not match the renderer's {}; rebuild it",
                 g_shaderCacheLayout, kShaderCacheLayout);
      logged = true;
    }
    return nullptr;
  }
  const ShaderCacheEntry *begin = g_shaderCacheEntries;
  const ShaderCacheEntry *end = begin + g_shaderCacheEntryCount;
  const ShaderCacheEntry *it = std::lower_bound(
      begin, end, hash, [](const ShaderCacheEntry &e, uint64_t h) { return e.hash < h; });
  return (it != end && it->hash == hash) ? it : nullptr;
}

void ShaderBoolSpecMask(const ShaderCacheEntry &entry, uint32_t mask[8]) {
  std::fill(mask, mask + 8, 0u);
#if defined(PLUME_DEKO3D)
  // banjo-nx: the GLSL package lists them (the SPIR-V cache need not be unpacked for deko3d).
  if (IsDeko3D()) {
    banjo::renut_gpu::deko3d::BoolSpecMask(entry.hash, mask);
    return;
  }
#endif
  if (!IsVulkan() || entry.spirvSize == 0)
    return;
  const uint8_t *cache = Decompress(g_spirv, g_compressedSpirvCache, g_spirvCacheCompressedSize,
                                    g_spirvCacheDecompressedSize, "SPIR-V");
  if (!cache)
    return;
  const uint8_t *smol = cache + entry.spirvOffset;
  std::vector<uint8_t> spirv(smolv::GetDecodedBufferSize(smol, entry.spirvSize));
  if (spirv.size() < 20 || !smolv::Decode(smol, entry.spirvSize, spirv.data(), spirv.size()))
    return;
  // After the five header words, each instruction's first word is (word count << 16) | opcode. OpDecorate (71):
  // target, decoration, literals; SpecId is decoration 1.
  const auto *words = reinterpret_cast<const uint32_t *>(spirv.data());
  const size_t count = spirv.size() / 4;
  for (size_t i = 5; i < count;) {
    const uint32_t length = words[i] >> 16, opcode = words[i] & 0xFFFF;
    if (length == 0)
      break;
    if (opcode == 71 && length >= 4 && i + 3 < count && words[i + 2] == 1) {
      const uint32_t id = words[i + 3];
      if (id >= 1000 && id < 1256)
        mask[(id - 1000) / 32] |= 1u << ((id - 1000) % 32);
    }
    i += length;
  }
}

std::unique_ptr<plume::RenderShader> CreateCachedShader(plume::RenderDevice *device,
                                                        const ShaderCacheEntry &entry) {
  if (!device)
    return nullptr;

#if defined(PLUME_DEKO3D)
  // banjo-nx (2026-10-17): the deko3d back end's GLSL for uam, from the package in the NRO.
  if (IsDeko3D())
    return banjo::renut_gpu::deko3d::CreateGameShader(device, entry.hash);
#endif

  if (IsVulkan()) {
    const uint8_t *cache = Decompress(g_spirv, g_compressedSpirvCache, g_spirvCacheCompressedSize,
                                      g_spirvCacheDecompressedSize, "SPIR-V");
    if (!cache || entry.spirvSize == 0)
      return nullptr;
    const uint8_t *smol = cache + entry.spirvOffset;
    std::vector<uint8_t> spirv(smolv::GetDecodedBufferSize(smol, entry.spirvSize));
    if (spirv.empty() || !smolv::Decode(smol, entry.spirvSize, spirv.data(), spirv.size())) {
      RGPU_ERROR("SPIR-V decode failed for shader {:016X}", entry.hash);
      return nullptr;
    }
    return device->createShader(spirv.data(), spirv.size(), "main",
                                plume::RenderShaderFormat::SPIRV);
  }

#if defined(_WIN32)
  const uint8_t *cache = Decompress(g_dxil, g_compressedDxilCache, g_dxilCacheCompressedSize,
                                    g_dxilCacheDecompressedSize, "DXIL");
  if (!cache || entry.dxilSize == 0)
    return nullptr;
  return device->createShader(cache + entry.dxilOffset, entry.dxilSize, "main",
                              plume::RenderShaderFormat::DXIL);
#else
  return nullptr;
#endif
}

uint64_t ShaderCacheHostBytes() {
  return g_decompressed_bytes.load(std::memory_order_relaxed);
}

// Dishonored (2026-11-12, ahead-of-time shaders): who made a shader no game file has. The host return addresses up the
// frame chain as image offsets (symbolise with the build's ELF in out/deployments/<NRO sha256>/: the generated guest
// functions are sub_XXXXXXXX), innermost first: the hook, the host-to-guest wrapper, then the game's D3D call and its
// callers.
static std::string CallerChain() {
#if defined(__SWITCH__) && defined(__aarch64__)
  const uintptr_t base = reinterpret_cast<uintptr_t>(&_start);
  uintptr_t fp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  std::string out;
  for (int i = 0; i < 14 && fp; ++i) {
    const uintptr_t *frame = reinterpret_cast<const uintptr_t *>(fp);
    const uintptr_t next = frame[0], lr = frame[1];
    if (!lr || lr < base)
      break;
    out += fmt::format(" +{:x}", lr - base);
    if (next <= fp || next - fp > 0x10000 || (next & 7))
      break;
    fp = next;
  }
  return out;
#elif defined(_WIN32)
  // The PC build: the return addresses as module+offset (symbolise sa2.exe's with its PDB, e.g. llvm-symbolizer
  // --obj=out/pc/sa2.exe 0x<offset + image base 0x140000000>).
  void *frames[24] = {};
  const USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
  std::string out;
  for (USHORT i = 0; i < n; ++i) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(frames[i]), &module) && module) {
      GetModuleFileNameA(module, name, sizeof(name));
      const char *slash = std::strrchr(name, '\\');
      out += fmt::format(" {}+{:x}", slash ? slash + 1 : name,
                         uintptr_t(frames[i]) - reinterpret_cast<uintptr_t>(module));
    } else {
      out += fmt::format(" {:x}", uintptr_t(frames[i]));
    }
  }
  return out;
#else
  return " (not on this platform)";
#endif
}

// Dishonored (2026-11-12): the shader cache record of the next shader this thread creates (SetShaderSourceNote).
namespace {
struct ShaderSourceNote {
  std::string text;
  const uint8_t *chunk = nullptr;
  uint32_t chunk_size = 0;
};
thread_local ShaderSourceNote t_source_note;

// " from <note>", and the compressed chunk written to <capture dir>/trace/<hash>_chunk.bin (once per hash). Clears the
// note: it belongs to one creation.
std::string TakeSourceNote(uint64_t hash) {
  ShaderSourceNote note = std::move(t_source_note);
  t_source_note = {};
  if (note.text.empty())
    return {};
  std::string out = " from " + note.text;
  if (note.chunk && note.chunk_size) {
    const std::filesystem::path dir =
        banjo::renut_gpu::NextToExecutable(REXCVAR_GET(renut_shader_capture_dir)) / "trace";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / fmt::format("{:016x}_chunk.bin", hash);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(note.chunk), std::streamsize(note.chunk_size));
    out += file ? fmt::format(" (compressed chunk: {})", path.string()) : std::string(" (chunk not written)");
  }
  return out;
}
}  // namespace

void SetShaderSourceNote(std::string note, const uint8_t *chunk, uint32_t chunk_size) {
  t_source_note = {std::move(note), chunk, chunk_size};
}

// Dishonored (2026-11-12): dh_shader_trace, the creator of the listed shaders (found or not), once each.
void TraceShaderCreation(uint64_t hash, bool pixel_shader) {
  static const std::unordered_set<uint64_t> wanted = [] {
    std::unordered_set<uint64_t> set;
    const std::string list = REXCVAR_GET(dh_shader_trace);
    size_t at = 0;
    while (at < list.size()) {
      size_t end = list.find_first_of(", ", at);
      if (end == std::string::npos)
        end = list.size();
      if (end > at)
        set.insert(std::strtoull(list.substr(at, end - at).c_str(), nullptr, 16));
      at = end + 1;
    }
    RGPU_INFO("[shader-trace] watching {} shader hashes (dh_shader_trace: {} characters)", set.size(), list.size());
    return set;
  }();
  if (wanted.empty() || !wanted.count(hash))
    return;  // the source note stays for CaptureMissingShader (same creation), the next lookup replaces it
  static std::mutex mutex;
  static std::unordered_set<uint64_t> logged;
  {
    std::lock_guard lock(mutex);
    if (!logged.insert(hash).second)
      return;
  }
  RGPU_INFO("[shader-trace] {} {:016X} made by{}{}", pixel_shader ? "PS" : "VS", hash, CallerChain(),
            TakeSourceNote(hash));
}

void CaptureMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash) {
  std::lock_guard lock(g_capture_mutex);
  if (!g_captured.insert(hash).second)
    return;

  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  // banjo-nx: next to the executable (on the Switch, the NRO's folder), not the working directory.
  const std::filesystem::path dir = banjo::renut_gpu::NextToExecutable(REXCVAR_GET(renut_shader_capture_dir));
  const std::filesystem::path path =
      dir / fmt::format("{}_{:016x}.bin", pixel_shader ? "ps" : "vs", hash);

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(path, std::ios::binary);
  if (out)
    out.write(reinterpret_cast<const char *>(container), std::streamsize(size));
  if (!out) {
    RGPU_WARN("Shader {:016X} is not in the shader cache and could not be captured to {}", hash,
              path.string());
    return;
  }
  RGPU_WARN("Shader {:016X} is not in the shader cache: captured to {}, the next build adds it; made by{}{}",
            hash, path.string(), CallerChain(), TakeSourceNote(hash));
}

} // namespace renut::gpu
