#include "../src/assets/CameoPixels.h"
#include "../src/assets/IconLoadCache.h"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <vector>

using rareplay::assets::DecodeCameoPixels;
using rareplay::assets::DecodeCameoPixelsWithBackgrounds;
using rareplay::assets::IconLoadCache;
using rareplay::assets::Rgb16Format;
using rareplay::viewer::IconImage;

void Require(bool condition)
{
    if (!condition)
        std::abort();
}

int main()
{
    constexpr Rgb16Format RGB565{0xF800, 0x07E0, 0x001F};
    constexpr Rgb16Format RGB555{0x7C00, 0x03E0, 0x001F};
    IconImage image;
    image.mKey = "test";

    // 第二行起点由 pitch 决定，透明色不能泄漏成不透明品红。
    const std::array<std::uint8_t, 12> pitched565{
        0x00, 0xF8, 0x1F, 0xF8, 0xAA, 0xAA,
        0xE0, 0x07, 0x1F, 0x00, 0xBB, 0xBB};
    Require(DecodeCameoPixels(pitched565, 2, 2, 6, RGB565, 0xF81F, image));
    Require(image.mKey == "test" && image.mWidth == 2 && image.mHeight == 2);
    Require((image.mRgba == std::vector<std::uint8_t>{
        255, 0, 0, 255, 255, 0, 255, 0,
        0, 255, 0, 255, 0, 0, 255, 255}));

    const std::array<std::uint8_t, 4> pixels555{0x00, 0x7C, 0xE0, 0x03};
    Require(DecodeCameoPixels(pixels555, 2, 1, 4, RGB555, 0xFFFF, image));
    Require((image.mRgba == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255}));
    Require(!DecodeCameoPixels(pixels555, 2, 1, 3, RGB555, 0xFFFF, image));
    Require(!DecodeCameoPixels(pixels555, 2, 1, 4, {0xF800, 0xF800, 0x001F}, 0xFFFF, image));

    // 两张底色图同一位置都为品红时仍是不透明的 SHP 像素。
    const std::array<std::uint8_t, 4> dark{0x1F, 0xF8, 0x00, 0x00};
    const std::array<std::uint8_t, 4> light{0x1F, 0xF8, 0xFF, 0xFF};
    Require(DecodeCameoPixelsWithBackgrounds(dark, light, 2, 1, 4, RGB565, image));
    Require(image.mRgba[3] == 255 && image.mRgba[7] == 0);

    IconLoadCache cache(2, 1, 16);
    int loads = 0;
    bool atCapacity = false;
    auto missing = [&]() -> std::shared_ptr<const IconImage> { ++loads; return {}; };
    cache.BeginSample();
    Require(!cache.Resolve("missing", missing, atCapacity));
    Require(!cache.Resolve("missing", missing, atCapacity) && loads == 1);
    Require(!cache.Resolve("deferred", missing, atCapacity) && loads == 1);
    cache.BeginSample();
    Require(!cache.Resolve("deferred", missing, atCapacity) && loads == 2);
    Require(!cache.Resolve("overflow", missing, atCapacity) && atCapacity && loads == 2);
    cache.Clear();
    cache.BeginSample();
    Require(!cache.Resolve("missing", missing, atCapacity) && loads == 3);

    IconLoadCache byteCache(4, 4, 8);
    auto large = [&]() -> std::shared_ptr<const IconImage> {
        ++loads;
        auto result = std::make_shared<IconImage>();
        result->mRgba.resize(12);
        return result;
    };
    byteCache.BeginSample();
    Require(!byteCache.Resolve("large", large, atCapacity) && atCapacity);
    Require(!byteCache.Resolve("large", large, atCapacity) && loads == 4);
    auto fitting = [&]() -> std::shared_ptr<const IconImage> {
        ++loads;
        auto result = std::make_shared<IconImage>();
        result->mRgba.resize(8);
        return result;
    };
    Require(byteCache.Resolve("fitting", fitting, atCapacity) != nullptr && loads == 5);
    Require(!byteCache.Resolve("after_full", large, atCapacity) && atCapacity && loads == 5);
    Require(!byteCache.Resolve("after_full", large, atCapacity) && loads == 5);
}
