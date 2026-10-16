#include "runtime/luau_codegen_switch.h"

#include <elf.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace mocktail {
namespace runtime {
namespace {

constexpr std::size_t kNameSlotOffset = 8;
constexpr std::size_t kRecordTailSize = 16;

struct ProcessMapping {
  std::uintptr_t first = 0, end = 0;
  bool writable = false;
};

std::vector<ProcessMapping> ReadableMappings() {
  std::ifstream maps("/proc/self/maps");
  std::vector<ProcessMapping> ranges;
  std::string line;
  while (std::getline(maps, line)) {
    unsigned long long first = 0, end = 0;
    char permissions[5]{};
    if (std::sscanf(line.c_str(), "%llx-%llx %4s", &first, &end, permissions) != 3 ||
        permissions[0] != 'r' || first >= end ||
        end > std::numeric_limits<std::uintptr_t>::max()) continue;
    ranges.push_back({static_cast<std::uintptr_t>(first),
                      static_cast<std::uintptr_t>(end), permissions[1] == 'w'});
  }
  return ranges;
}

bool ReadableRange(const std::vector<ProcessMapping>& maps,
                   std::uintptr_t address, std::size_t size) {
  if (!size || size > std::numeric_limits<std::uintptr_t>::max() - address)
    return false;
  const auto end = address + size;
  for (const auto& mapping : maps) {
    if (mapping.end <= address) continue;
    if (mapping.first > address) return false;
    address = std::min(end, mapping.end);
    if (address == end) return true;
  }
  return false;
}

std::string_view SegmentBytes(const LoadedImageSegment& segment) {
  return {reinterpret_cast<const char*>(segment.address), segment.size};
}

std::vector<std::uintptr_t> NameAddresses(
    const std::vector<LoadedImageSegment>& segments, std::string_view name) {
  std::string needle(name);
  needle.push_back('\0');
  std::vector<std::uintptr_t> addresses;
  for (const LoadedImageSegment& segment : segments) {
    const std::string_view bytes = SegmentBytes(segment);
    for (std::size_t at = bytes.find(needle); at != std::string_view::npos;
         at = bytes.find(needle, at + 1)) {
      addresses.push_back(segment.address + at);
    }
  }
  return addresses;
}

}  // namespace

std::vector<LoadedImageSegment> LoadedImageSegments(std::uintptr_t image_base) {
  std::vector<LoadedImageSegment> segments;
  if (image_base == 0) {
    return segments;
  }
  const auto mappings = ReadableMappings();
  if (!ReadableRange(mappings, image_base, sizeof(Elf64_Ehdr))) return segments;
  Elf64_Ehdr header{};
  std::memcpy(&header, reinterpret_cast<const void*>(image_base), sizeof(header));
  if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
      header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
      header.e_phoff == 0 || header.e_phentsize != sizeof(Elf64_Phdr) ||
      header.e_phnum == 0 || header.e_phnum > 1024 ||
      header.e_phoff > std::numeric_limits<std::uintptr_t>::max() - image_base) {
    return segments;
  }
  const auto table = image_base + static_cast<std::uintptr_t>(header.e_phoff);
  if (!ReadableRange(mappings, table, header.e_phnum * sizeof(Elf64_Phdr)))
    return segments;
  for (std::size_t index = 0; index < header.e_phnum; ++index) {
    Elf64_Phdr program{};
    std::memcpy(&program, reinterpret_cast<const void*>(table + index * sizeof(program)), sizeof(program));
    if (program.p_type != PT_LOAD || program.p_memsz == 0) {
      continue;
    }
    if (program.p_vaddr > std::numeric_limits<std::uintptr_t>::max() - image_base)
      return {};
    const auto first = image_base + program.p_vaddr;
    if (program.p_memsz > std::numeric_limits<std::uintptr_t>::max() - first)
      return {};
    const auto end = first + program.p_memsz;
    // PT_LOAD PF_W can include pages made read-only by GNU_RELRO. Only actual
    // readable mappings may be scanned, and actual writable pages may be edited.
    for (const auto& mapping : mappings) {
      const auto begin = std::max(first, mapping.first);
      const auto limit = std::min(end, mapping.end);
      if (begin < limit)
        segments.push_back({begin, static_cast<std::size_t>(limit - begin),
                            (program.p_flags & PF_W) != 0 && mapping.writable});
    }
  }
  return segments;
}

std::uint8_t* FindFastFlagValue(const std::vector<LoadedImageSegment>& segments,
                                std::string_view name) {
  const std::vector<std::uintptr_t> names = NameAddresses(segments, name);
  if (names.empty()) {
    return nullptr;
  }
  std::uint8_t* found = nullptr;
  int matches = 0;
  for (const LoadedImageSegment& segment : segments) {
    if (!segment.writable || segment.size < kNameSlotOffset + kRecordTailSize ||
        segment.size > std::numeric_limits<std::uintptr_t>::max() - segment.address ||
        segment.address > std::numeric_limits<std::uintptr_t>::max() - kNameSlotOffset - 7) {
      continue;
    }
    const std::uintptr_t first =
        (segment.address + kNameSlotOffset + 7) & ~std::uintptr_t{7};
    const std::uintptr_t end = segment.address + segment.size;
    for (std::uintptr_t slot = first; slot <= end - kRecordTailSize;
         slot += 8) {
      std::uintptr_t pointer = 0;
      std::memcpy(&pointer, reinterpret_cast<const void*>(slot),
                  sizeof(pointer));
      if (std::find(names.begin(), names.end(), pointer) == names.end()) {
        continue;
      }
      std::uint64_t length = 0;
      std::memcpy(&length, reinterpret_cast<const void*>(slot + 8),
                  sizeof(length));
      if (length != name.size()) {
        continue;
      }
      found = reinterpret_cast<std::uint8_t*>(slot - kNameSlotOffset);
      ++matches;
    }
  }
  return matches == 1 && __atomic_load_n(found, __ATOMIC_RELAXED) <= 1
      ? found : nullptr;
}

bool ClientSettingsSetLuauCodegenAll(std::string_view overrides_json) {
  const nlohmann::json overrides =
      nlohmann::json::parse(overrides_json, nullptr, false, true);
  return overrides.is_object() &&
         overrides.contains(std::string(kLuauCodegenAllClientSetting));
}

bool LuauCodegenAllForPhase(RobloxExperiencePresencePhase phase) {
  return phase == RobloxExperiencePresencePhase::kBrowsing;
}

LuauCodegenMenuSwitch::LuauCodegenMenuSwitch(
    LocateFlagFn locate_flag, RobloxExperiencePresenceObserver next)
    : locate_flag_(std::move(locate_flag)), next_(next) {}

bool LuauCodegenMenuSwitch::PrepareForMenus() {
  std::lock_guard<std::mutex> lock(mutex_);
  return SetEnabledLocked(true);
}

RobloxExperiencePresenceObserver LuauCodegenMenuSwitch::observer() {
  return {this, &LuauCodegenMenuSwitch::Notify};
}

void LuauCodegenMenuSwitch::OnPhase(
    RobloxExperiencePresencePhase phase,
    const RobloxExperienceLaunchRequest* request) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)SetEnabledLocked(LuauCodegenAllForPhase(phase));
  }
  if (next_.valid()) {
    next_.notify(next_.context, phase, request);
  }
}

bool LuauCodegenMenuSwitch::SetEnabledLocked(bool enabled) {
  if (!located_) {
    located_ = true;
    flag_ = locate_flag_ ? locate_flag_() : nullptr;
  }
  if (flag_ == nullptr) return false;
  const std::uint8_t current = __atomic_load_n(flag_, __ATOMIC_RELAXED);
  if (current > 1) {
    flag_ = nullptr;
    return false;
  }
  const std::uint8_t wanted = enabled ? 1 : 0;
  if (current != wanted) {
    __atomic_store_n(flag_, wanted, __ATOMIC_RELAXED);
    std::cout << "  [luau-codegen] native code for all scripts "
              << (wanted != 0 ? "on" : "off") << '\n';
  }
  return true;
}

void LuauCodegenMenuSwitch::Notify(
    void* context, RobloxExperiencePresencePhase phase,
    const RobloxExperienceLaunchRequest* request) {
  static_cast<LuauCodegenMenuSwitch*>(context)->OnPhase(phase, request);
}

}  // namespace runtime
}  // namespace mocktail
