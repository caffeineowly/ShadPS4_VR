//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <span>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Frame;
}

namespace Vulkan::HostPasses {

class PostProcessingPass {
public:
    struct Settings {
        float gamma = 1.0f;
        u32 hdr = 0;
        u32 srgb_input = 0;
        // VR additions (from bigmak94/AstroQuest), in the order post_process.frag has them:
        /// 0..1: how much the picture is sharpened on the way (host_shaders/post_process.frag).
        float sharpen = 0.0f;
        /// For an output image of an sRGB format, which does the encoding for display itself.
        u32 linear_out = 0;
        /// The input is read as the bytes it holds, already encoded for display (an sRGB image
        /// seen through a view of its UNORM sibling, with `gamma` 1): nothing to encode.
        u32 encoded_in = 0;
        /// Within `centre_radius` of the middle of the picture (a share of the way to its edge,
        /// on both axes) it is sharpened by `sharpen_centre` instead. 0: no such part.
        float sharpen_centre = 0.0f;
        float centre_radius = 0.0f;
        /// The part of the input that is drawn: u, v, width, height. Set per Region by Render.
        std::array<float, 4> uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
    };

    void Create(vk::Device device, vk::Format surface_format);

    void Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                Frame& output, Settings settings);

    /// An input image and the part of the output frame it is drawn into.
    struct Region {
        vk::ImageView input;
        vk::Rect2D area;
        std::optional<vk::Rect2D> clip;
        /// The part of `input` drawn into `area`: u, v, width, height.
        std::array<float, 4> uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
    };

    // Frame marker: a row of black and white blocks stamped along the top-left edge of a frame.
    // Two sync blocks (white, black) are followed by a 16 bit frame id and an 8 bit check value,
    // least significant bit first. It lets a host compositor recognise which emulated frame it
    // is looking at after the image has travelled through a window system.
    static constexpr u32 MarkerBlockSize = 8;
    static constexpr u32 MarkerSyncBlocks = 2;
    static constexpr u32 MarkerIdBits = 16;
    static constexpr u32 MarkerCheckBits = 8;
    static constexpr u32 MarkerBlocks = MarkerSyncBlocks + MarkerIdBits + MarkerCheckBits;
    static constexpr u32 MarkerCheck(u32 id) {
        return ((id & 0xff) ^ ((id >> 8) & 0xff) ^ 0x5a) & 0xff;
    }

    /// Draws each region's input into its part of the output (the two eyes of a headset's frame
    /// side by side), and stamps `marker` if given.
    void Render(vk::CommandBuffer cmdbuf, std::span<const Region> regions, Frame& output,
                Settings settings, std::optional<u32> marker = std::nullopt);

private:
    vk::UniquePipeline pipeline{};
    vk::UniquePipelineLayout pipeline_layout{};
    vk::UniqueDescriptorSetLayout desc_set_layout{};
    vk::UniqueSampler sampler{};
};

} // namespace Vulkan::HostPasses
