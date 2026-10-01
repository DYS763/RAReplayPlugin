#include "CameoPixels.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <utility>

namespace rareplay::assets
{
namespace
{
constexpr int MAX_ICON_DIMENSION = 256;

bool IsContiguous(std::uint16_t mask)
{
    if (!mask)
        return false;
    const unsigned shifted = static_cast<unsigned>(mask) >> std::countr_zero(mask);
    return (shifted & (shifted + 1)) == 0;
}

bool HasValidExtent(std::span<const std::uint8_t> pixels, int width, int height, int pitchBytes)
{
    return width > 0 && width <= MAX_ICON_DIMENSION && height > 0 && height <= MAX_ICON_DIMENSION &&
        pitchBytes >= width * 2 && pitchBytes <= MAX_ICON_DIMENSION * 4 &&
        pixels.size() >= static_cast<std::size_t>(pitchBytes) * (height - 1) + width * 2;
}

std::uint16_t ReadPixel(std::span<const std::uint8_t> pixels, std::size_t offset)
{
    return static_cast<std::uint16_t>(pixels[offset]) |
        static_cast<std::uint16_t>(pixels[offset + 1] << 8);
}

std::uint8_t Channel(std::uint16_t pixel, std::uint16_t mask)
{
    const unsigned shift = std::countr_zero(mask);
    const unsigned bits = std::popcount(mask);
    const unsigned maximum = (1U << bits) - 1;
    const unsigned value = (pixel & mask) >> shift;
    return static_cast<std::uint8_t>((value * 255U + maximum / 2U) / maximum);
}

void WriteRgba(std::uint16_t pixel, Rgb16Format format, std::uint8_t alpha, std::uint8_t* out)
{
    out[0] = Channel(pixel, format.mRedMask);
    out[1] = Channel(pixel, format.mGreenMask);
    out[2] = Channel(pixel, format.mBlueMask);
    out[3] = alpha;
}
}

bool IsSupportedRgb16(Rgb16Format format)
{
    const unsigned combined = format.mRedMask | format.mGreenMask | format.mBlueMask;
    const unsigned overlap = (format.mRedMask & format.mGreenMask) |
        (format.mRedMask & format.mBlueMask) | (format.mGreenMask & format.mBlueMask);
    const int redBits = std::popcount(format.mRedMask);
    const int greenBits = std::popcount(format.mGreenMask);
    const int blueBits = std::popcount(format.mBlueMask);
    return !overlap && combined <= 0xFFFFU && IsContiguous(format.mRedMask) &&
        IsContiguous(format.mGreenMask) && IsContiguous(format.mBlueMask) &&
        redBits == 5 && (greenBits == 5 || greenBits == 6) && blueBits == 5;
}

bool DecodeCameoPixels(std::span<const std::uint8_t> pixels, int width, int height,
    int pitchBytes, Rgb16Format format, std::uint16_t transparentKey, viewer::IconImage& outImage)
{
    if (!IsSupportedRgb16(format) || !HasValidExtent(pixels, width, height, pitchBytes))
        return false;
    viewer::IconImage image;
    image.mKey = outImage.mKey;
    image.mWidth = width;
    image.mHeight = height;
    image.mRgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::uint16_t pixel = ReadPixel(pixels, static_cast<std::size_t>(y) * pitchBytes + x * 2);
            WriteRgba(pixel, format, pixel == transparentKey ? 0 : 255,
                image.mRgba.data() + (static_cast<std::size_t>(y) * width + x) * 4);
        }
    }
    outImage = std::move(image);
    return true;
}

bool DecodeCameoPixelsWithBackgrounds(std::span<const std::uint8_t> darkPixels,
    std::span<const std::uint8_t> lightPixels, int width, int height, int pitchBytes,
    Rgb16Format format, viewer::IconImage& outImage)
{
    if (!IsSupportedRgb16(format) || !HasValidExtent(darkPixels, width, height, pitchBytes) ||
        !HasValidExtent(lightPixels, width, height, pitchBytes))
        return false;
    viewer::IconImage image;
    image.mKey = outImage.mKey;
    image.mWidth = width;
    image.mHeight = height;
    image.mRgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t offset = static_cast<std::size_t>(y) * pitchBytes + x * 2;
            const std::uint16_t dark = ReadPixel(darkPixels, offset);
            const std::uint16_t light = ReadPixel(lightPixels, offset);
            WriteRgba(dark, format, dark == light ? 255 : 0,
                image.mRgba.data() + (static_cast<std::size_t>(y) * width + x) * 4);
        }
    }
    outImage = std::move(image);
    return true;
}
}
