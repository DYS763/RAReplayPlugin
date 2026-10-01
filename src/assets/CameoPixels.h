#pragma once

#include "../viewer/ReplayViewModel.h"

#include <cstdint>
#include <span>

namespace rareplay::assets
{
struct Rgb16Format
{
    std::uint16_t mRedMask = 0;
    std::uint16_t mGreenMask = 0;
    std::uint16_t mBlueMask = 0;
};

/** 仅接受不重叠的连续 RGB 位域；不认识的原生像素布局应拒绝。 */
bool IsSupportedRgb16(Rgb16Format format);

/** 输入是逐行排列的 16 位小端像素，pitch 以字节计。 */
bool DecodeCameoPixels(std::span<const std::uint8_t> pixels, int width, int height,
    int pitchBytes, Rgb16Format format, std::uint16_t transparentKey, viewer::IconImage& outImage);

/** 两次光栅化仅背景不同；像素不同时视为透明。 */
bool DecodeCameoPixelsWithBackgrounds(std::span<const std::uint8_t> darkPixels,
    std::span<const std::uint8_t> lightPixels, int width, int height, int pitchBytes,
    Rgb16Format format, viewer::IconImage& outImage);
}
