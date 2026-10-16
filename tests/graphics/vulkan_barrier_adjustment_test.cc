#include "mocktail/graphics/vulkan_barrier_adjustment.h"

#include <array>
#include <cstring>
#include <string>
#include <type_traits>
#include <gtest/gtest.h>

namespace mocktail::graphics::internal {
namespace {

template <class Barrier>
Barrier MakeBarrier(std::uint32_t source, std::uint32_t destination) {
  Barrier barrier{};
  barrier.srcQueueFamilyIndex = source;
  barrier.dstQueueFamilyIndex = destination;
  barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  return barrier;
}

template <class Barrier>
class VulkanBarrierAdjustment : public ::testing::Test {};
using BarrierTypes = ::testing::Types<VkImageMemoryBarrier, VkImageMemoryBarrier2>;
struct BarrierNames {
  template <class Barrier>
  static std::string GetName(int) {
    return std::is_same_v<Barrier, VkImageMemoryBarrier> ? "Legacy" : "Synchronization2";
  }
};
TYPED_TEST_SUITE(VulkanBarrierAdjustment, BarrierTypes, BarrierNames);

TYPED_TEST(VulkanBarrierAdjustment, NoTransferForwardsOriginalArrayAndMetadata) {
  auto barriers = std::array<TypeParam, 5>{MakeBarrier<TypeParam>(3, 3),
      MakeBarrier<TypeParam>(VK_QUEUE_FAMILY_IGNORED, 3),
      MakeBarrier<TypeParam>(3, VK_QUEUE_FAMILY_FOREIGN_EXT),
      MakeBarrier<TypeParam>(VK_QUEUE_FAMILY_EXTERNAL, 4),
      MakeBarrier<TypeParam>(VK_QUEUE_FAMILY_FOREIGN_EXT, VK_QUEUE_FAMILY_EXTERNAL)};
  const auto original = barriers;
  int callbacks = 0;
  WithDesktopQueueBarriers(barriers.size(), barriers.data(),
      [&](unsigned, TypeParam*) { ++callbacks; },
      [&](const TypeParam* forwarded) { EXPECT_EQ(forwarded, barriers.data()); });
  EXPECT_EQ(callbacks, 0);
  EXPECT_EQ(std::memcmp(barriers.data(), original.data(), sizeof(barriers)), 0);
}

TYPED_TEST(VulkanBarrierAdjustment, EmptyArrayIsForwardedWithoutCallback) {
  int callbacks = 0;
  WithDesktopQueueBarriers<TypeParam>(0, nullptr,
      [&](unsigned, TypeParam*) { ++callbacks; },
      [&](const TypeParam* forwarded) { EXPECT_EQ(forwarded, nullptr); });
  EXPECT_EQ(callbacks, 0);
}

TYPED_TEST(VulkanBarrierAdjustment, MixedTransfersAreBatchedWithoutMutatingInput) {
  for (const unsigned count : {1u, 8u, 9u, 33u}) {
    std::vector<TypeParam> barriers(count, MakeBarrier<TypeParam>(3, 3));
    barriers[count - 1] = MakeBarrier<TypeParam>(3, 4);
    VkMemoryBarrier chained{};
    barriers[count - 1].pNext = &chained;
    const auto original = barriers;
    int callbacks = 0;
    WithDesktopQueueBarriers(count, barriers.data(),
        [&](unsigned received, TypeParam* adjusted) {
          ++callbacks;
          EXPECT_EQ(received, count);
          for (unsigned i = 0; i < received; ++i)
            if (NeedsDesktopQueueNormalization(adjusted[i].srcQueueFamilyIndex,
                                               adjusted[i].dstQueueFamilyIndex)) {
              adjusted[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
              adjusted[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            }
        }, [&](const TypeParam* forwarded) {
          EXPECT_NE(forwarded, barriers.data());
          EXPECT_EQ(forwarded[count - 1].srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
          EXPECT_EQ(forwarded[count - 1].pNext, &chained);
          EXPECT_EQ(forwarded[count - 1].oldLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
          EXPECT_EQ(forwarded[count - 1].newLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
          for (unsigned i = 0; i + 1 < count; ++i)
            EXPECT_EQ(std::memcmp(&forwarded[i], &original[i], sizeof(TypeParam)), 0);
        });
    EXPECT_EQ(callbacks, 1);
    EXPECT_EQ(std::memcmp(barriers.data(), original.data(), count * sizeof(TypeParam)), 0);
  }
}

}  // namespace
}  // namespace mocktail::graphics::internal
