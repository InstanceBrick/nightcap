#include <FidelityFX/host/backends/vk/ffx_vk.h>

// The upscaler does not replace a Vulkan swapchain. OpenXR owns presentation;
// reject SDK frame-interpolation swapchain configuration explicitly.
FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(
    const FfxFrameGenerationConfig*) {
    return FFX_ERROR_INVALID_ARGUMENT;
}
