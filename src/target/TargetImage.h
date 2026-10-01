#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace rareplay::target
{
    /** 只在游戏进程启动时验证固定目标映像。 */
    class TargetImage
    {
    public:
        bool LoadCurrentProcess(std::string& outError);
        const std::filesystem::path& GetPath() const { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    constexpr std::uintptr_t TARGET_IMAGE_BASE = 0x00400000;
    constexpr const char* TARGET_SHA256 =
        "7cd005d263fde203d9c84548200a057a8df61d724da3c6bd1e521eeb61cd0747";
    constexpr std::size_t TARGET_FILE_SIZE = 4816864;
    constexpr std::uint32_t TARGET_PE_TIMESTAMP = 0x3BDF544E;
    constexpr std::uint32_t TARGET_CRC32 = 0x1B499086;
    constexpr std::array<std::uint8_t, 32> TARGET_SHA256_BYTES = {
        0x7c,0xd0,0x05,0xd2,0x63,0xfd,0xe2,0x03,
        0xd9,0xc8,0x45,0x48,0x20,0x0a,0x05,0x7a,
        0x8d,0xf6,0x1d,0x72,0x4d,0xa3,0xc6,0xbd,
        0x1e,0x52,0x1e,0xeb,0x61,0xcd,0x07,0x47};
    bool VerifyTargetExecutableFile(const std::filesystem::path& path, std::string& outError);
}
