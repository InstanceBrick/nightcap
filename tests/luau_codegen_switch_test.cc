#include "runtime/luau_codegen_switch.h"

#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace mocktail::runtime {
namespace {

constexpr char kStrings[] = "\0FFlagOther\0DebugLuauCodegenAll\0tail";

struct alignas(8) FlagRecord {
  std::uint8_t value = 1;
  std::uint8_t padding[7] = {};
  const char* name = nullptr;
  std::uint64_t name_length = 0;
  std::uint64_t next = 0;
};

const char* NameInStrings() {
  return kStrings + 12;
}

std::vector<LoadedImageSegment> Segments(std::vector<FlagRecord>& records) {
  return {
      {reinterpret_cast<std::uintptr_t>(kStrings), sizeof(kStrings), false},
      {reinterpret_cast<std::uintptr_t>(records.data()),
       records.size() * sizeof(FlagRecord), true},
  };
}

TEST(LuauCodegenSwitchTest, FindsTheFlagRecordByName) {
  ASSERT_EQ(std::string(NameInStrings()), "DebugLuauCodegenAll");
  std::vector<FlagRecord> records(3);
  records[1].name = NameInStrings();
  records[1].name_length = 19;
  const auto segments = Segments(records);
  EXPECT_EQ(FindFastFlagValue(segments, kLuauCodegenAllFastFlagName),
            &records[1].value);
}

TEST(LuauCodegenSwitchTest, RejectsWrongLengthMissingOrDuplicateRecords) {
  std::vector<FlagRecord> records(3);
  records[1].name = NameInStrings();
  records[1].name_length = 18;
  auto segments = Segments(records);
  EXPECT_EQ(FindFastFlagValue(segments, kLuauCodegenAllFastFlagName), nullptr);

  records[1].name_length = 19;
  EXPECT_EQ(FindFastFlagValue(segments, "DebugLuauCodegen"), nullptr);

  records[2].name = NameInStrings();
  records[2].name_length = 19;
  EXPECT_EQ(FindFastFlagValue(segments, kLuauCodegenAllFastFlagName), nullptr);
}

TEST(LuauCodegenSwitchTest, IgnoresRecordsInReadOnlySegments) {
  std::vector<FlagRecord> records(1);
  records[0].name = NameInStrings();
  records[0].name_length = 19;
  auto segments = Segments(records);
  segments[1].writable = false;
  EXPECT_EQ(FindFastFlagValue(segments, kLuauCodegenAllFastFlagName), nullptr);
}

TEST(LuauCodegenSwitchTest, ReadsLoadSegmentsFromTheElfHeader) {
  alignas(8) std::uint8_t image[0x400] = {};
  auto* header = reinterpret_cast<Elf64_Ehdr*>(image);
  std::memcpy(header->e_ident, ELFMAG, SELFMAG);
  header->e_ident[EI_CLASS] = ELFCLASS64;
  header->e_ident[EI_DATA] = ELFDATA2LSB;
  header->e_phoff = sizeof(Elf64_Ehdr);
  header->e_phentsize = sizeof(Elf64_Phdr);
  header->e_phnum = 3;
  auto* program = reinterpret_cast<Elf64_Phdr*>(image + header->e_phoff);
  program[0] = {PT_LOAD, PF_R | PF_X, 0, 0, 0, 0x200, 0x200, 0x1000};
  program[1] = {PT_DYNAMIC, PF_R | PF_W, 0, 0x200, 0, 0x10, 0x10, 8};
  program[2] = {PT_LOAD, PF_R | PF_W, 0, 0x200, 0, 0x80, 0x180, 0x1000};
  const auto base = reinterpret_cast<std::uintptr_t>(image);
  const auto segments = LoadedImageSegments(base);
  ASSERT_EQ(segments.size(), 2u);
  EXPECT_EQ(segments[0].address, base);
  EXPECT_EQ(segments[0].size, 0x200u);
  EXPECT_FALSE(segments[0].writable);
  EXPECT_EQ(segments[1].address, base + 0x200);
  EXPECT_EQ(segments[1].size, 0x180u);
  EXPECT_TRUE(segments[1].writable);

  std::memset(image, 0, SELFMAG);
  EXPECT_TRUE(LoadedImageSegments(base).empty());
}

TEST(LuauCodegenSwitchTest, DetectsAUserSetting) {
  EXPECT_FALSE(ClientSettingsSetLuauCodegenAll("{}"));
  EXPECT_FALSE(ClientSettingsSetLuauCodegenAll(R"({"FIntOther":1})"));
  EXPECT_FALSE(ClientSettingsSetLuauCodegenAll("not-json"));
  EXPECT_TRUE(
      ClientSettingsSetLuauCodegenAll(R"({"FFlagDebugLuauCodegenAll":true})"));
  EXPECT_TRUE(
      ClientSettingsSetLuauCodegenAll(R"({"FFlagDebugLuauCodegenAll":"False"})"));
}

TEST(LuauCodegenSwitchTest, EnablesCodegenOnlyWhileBrowsing) {
  EXPECT_TRUE(LuauCodegenAllForPhase(RobloxExperiencePresencePhase::kBrowsing));
  EXPECT_FALSE(LuauCodegenAllForPhase(RobloxExperiencePresencePhase::kJoining));
  EXPECT_FALSE(LuauCodegenAllForPhase(RobloxExperiencePresencePhase::kPlaying));
}

struct Forwarded {
  int count = 0;
  RobloxExperiencePresencePhase phase = RobloxExperiencePresencePhase::kBrowsing;
};

void RecordForward(void* context, RobloxExperiencePresencePhase phase,
                   const RobloxExperienceLaunchRequest*) {
  auto* forwarded = static_cast<Forwarded*>(context);
  ++forwarded->count;
  forwarded->phase = phase;
}

TEST(LuauCodegenSwitchTest, WritesTheFlagPerPhaseAndForwards) {
  std::uint8_t flag = 1;
  int locate_calls = 0;
  Forwarded forwarded;
  LuauCodegenMenuSwitch menu_switch(
      [&] {
        ++locate_calls;
        return &flag;
      },
      {&forwarded, &RecordForward});
  const RobloxExperiencePresenceObserver observer = menu_switch.observer();
  ASSERT_TRUE(observer.valid());

  observer.notify(observer.context, RobloxExperiencePresencePhase::kJoining,
                  nullptr);
  EXPECT_EQ(flag, 0);
  observer.notify(observer.context, RobloxExperiencePresencePhase::kPlaying,
                  nullptr);
  EXPECT_EQ(flag, 0);
  observer.notify(observer.context, RobloxExperiencePresencePhase::kBrowsing,
                  nullptr);
  EXPECT_EQ(flag, 1);
  EXPECT_EQ(locate_calls, 1);
  EXPECT_EQ(forwarded.count, 3);
  EXPECT_EQ(forwarded.phase, RobloxExperiencePresencePhase::kBrowsing);
}

TEST(LuauCodegenSwitchTest, LeavesUnexpectedValuesAndMissingFlagsAlone) {
  std::uint8_t flag = 7;
  LuauCodegenMenuSwitch menu_switch([&] { return &flag; }, {});
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kJoining, nullptr);
  EXPECT_EQ(flag, 7);

  LuauCodegenMenuSwitch missing([] { return nullptr; }, {});
  missing.OnPhase(RobloxExperiencePresencePhase::kJoining, nullptr);
  EXPECT_TRUE(missing.observer().valid());
}

TEST(LuauCodegenSwitchTest, PrepareEnablesMenusWithoutForwardingPresence) {
  std::uint8_t flag = 0;
  int locate_calls = 0;
  Forwarded forwarded;
  LuauCodegenMenuSwitch menu_switch([&] { ++locate_calls; return &flag; },
                                    {&forwarded, &RecordForward});
  EXPECT_TRUE(menu_switch.PrepareForMenus());
  EXPECT_EQ(flag, 1);
  EXPECT_EQ(forwarded.count, 0);
  EXPECT_TRUE(menu_switch.PrepareForMenus());
  EXPECT_EQ(locate_calls, 1);
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kJoining, nullptr);
  EXPECT_EQ(flag, 0);
  EXPECT_EQ(forwarded.phase, RobloxExperiencePresencePhase::kJoining);
}

TEST(LuauCodegenSwitchTest, FailedJoinRestoresMenusAndNextJoinDisablesBeforeForwarding) {
  std::uint8_t flag = 0;
  struct Probe { std::uint8_t* flag; std::vector<int> values; } probe{&flag, {}};
  LuauCodegenMenuSwitch menu_switch([&] { return &flag; },
      {&probe, [](void* p, RobloxExperiencePresencePhase, const RobloxExperienceLaunchRequest*) {
        auto* probe = static_cast<Probe*>(p); probe->values.push_back(*probe->flag);
      }});
  ASSERT_TRUE(menu_switch.PrepareForMenus());
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kJoining, nullptr);
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kBrowsing, nullptr); // failed join
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kJoining, nullptr);
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kPlaying, nullptr);
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kBrowsing, nullptr);
  EXPECT_EQ(probe.values, (std::vector<int>{0, 1, 0, 0, 1}));
}

TEST(LuauCodegenSwitchTest, PreparationFailureNeverEnablesAndDoesNotRepeatedlyScan) {
  int locate_calls = 0;
  Forwarded forwarded;
  LuauCodegenMenuSwitch menu_switch([&]() -> std::uint8_t* { ++locate_calls; return nullptr; },
                                    {&forwarded, &RecordForward});
  EXPECT_FALSE(menu_switch.PrepareForMenus());
  EXPECT_FALSE(menu_switch.PrepareForMenus());
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kJoining, nullptr);
  menu_switch.OnPhase(RobloxExperiencePresencePhase::kBrowsing, nullptr);
  EXPECT_EQ(locate_calls, 1);
  EXPECT_EQ(forwarded.count, 2);
  std::uint8_t invalid = 7;
  LuauCodegenMenuSwitch rejected([&] { return &invalid; }, {});
  EXPECT_FALSE(rejected.PrepareForMenus());
  EXPECT_EQ(invalid, 7);
}

TEST(LuauCodegenSwitchTest, RejectsUniqueNamedRecordWithNonBooleanValue) {
  std::vector<FlagRecord> records(1);
  records[0].value = 7;
  records[0].name = NameInStrings();
  records[0].name_length = 19;
  EXPECT_EQ(FindFastFlagValue(Segments(records), kLuauCodegenAllFastFlagName), nullptr);
}

TEST(LuauCodegenSwitchTest, MalformedOrUnreadableElfHeadersAreRejected) {
  alignas(8) std::uint8_t image[0x200]{};
  auto* header = reinterpret_cast<Elf64_Ehdr*>(image);
  std::memcpy(header->e_ident, ELFMAG, SELFMAG);
  header->e_ident[EI_CLASS] = ELFCLASS64;
  header->e_ident[EI_DATA] = ELFDATA2LSB;
  header->e_phoff = sizeof(Elf64_Ehdr);
  header->e_phentsize = sizeof(Elf64_Phdr);
  header->e_phnum = 1;
  const auto base = reinterpret_cast<std::uintptr_t>(image);
  header->e_phentsize = 1;
  EXPECT_TRUE(LoadedImageSegments(base).empty());
  header->e_phentsize = sizeof(Elf64_Phdr);
  header->e_phnum = 2048;
  EXPECT_TRUE(LoadedImageSegments(base).empty());
  header->e_phnum = 1;
  header->e_phoff = std::numeric_limits<std::uint64_t>::max();
  EXPECT_TRUE(LoadedImageSegments(base).empty());
  EXPECT_TRUE(LoadedImageSegments(1).empty());
}

TEST(LuauCodegenSwitchTest, ActualReadOnlyRelroPagesAreNeverAdvertisedWritable) {
  const auto size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* image = static_cast<std::uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  ASSERT_NE(image, MAP_FAILED);
  auto* header = reinterpret_cast<Elf64_Ehdr*>(image);
  std::memcpy(header->e_ident, ELFMAG, SELFMAG);
  header->e_ident[EI_CLASS] = ELFCLASS64; header->e_ident[EI_DATA] = ELFDATA2LSB;
  header->e_phoff = sizeof(Elf64_Ehdr); header->e_phentsize = sizeof(Elf64_Phdr); header->e_phnum = 1;
  auto* program = reinterpret_cast<Elf64_Phdr*>(image + header->e_phoff);
  *program = {PT_LOAD, PF_R | PF_W, 0, 0, 0, size, size, size};
  std::memcpy(image + 0x100, "DebugLuauCodegenAll", 20);
  auto* record = reinterpret_cast<FlagRecord*>(image + 0x200);
  *record = {}; record->name = reinterpret_cast<char*>(image + 0x100); record->name_length = 19;
  const auto base = reinterpret_cast<std::uintptr_t>(image);
  EXPECT_EQ(FindFastFlagValue(LoadedImageSegments(base), kLuauCodegenAllFastFlagName), &record->value);
  ASSERT_EQ(mprotect(image, size, PROT_READ), 0);
  const auto segments = LoadedImageSegments(base);
  ASSERT_FALSE(segments.empty());
  for (const auto& segment : segments) EXPECT_FALSE(segment.writable);
  EXPECT_EQ(FindFastFlagValue(segments, kLuauCodegenAllFastFlagName), nullptr);
  EXPECT_EQ(munmap(image, size), 0);
}

}  // namespace
}  // namespace mocktail::runtime
