#include "../src/plugin/ReplayConfig.h"

#include <array>
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
void AddU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (int index = 0; index < 4; ++index)
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
}

void AddU64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (int index = 0; index < 8; ++index)
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
}

std::vector<std::uint8_t> MakeRequest(std::int64_t now, const std::string& path)
{
    std::vector<std::uint8_t> bytes;
    AddU32(bytes, 0x54525052);
    AddU32(bytes, 2);
    AddU32(bytes, 2);
    AddU32(bytes, 0);
    AddU64(bytes, now);
    for (int index = 0; index < 16; ++index)
        bytes.push_back(static_cast<std::uint8_t>(index + 1));
    AddU32(bytes, static_cast<std::uint32_t>(path.size()));
    AddU32(bytes, 0);
    bytes.insert(bytes.end(), path.begin(), path.end());
    return bytes;
}
}

int main()
{
    constexpr std::int64_t NOW = 1'800'000'000'000;
    const auto bytes = MakeRequest(NOW, "C:\\replays\\\xE6\xB5\x8B\xE8\xAF\x95.rpl");
    rareplay::plugin::ReplayRequest request;
    std::string error;
    assert(rareplay::plugin::ParseReplayRequest(bytes, NOW, request, error));
    assert(request.mMode == rareplay::plugin::ReplayMode::Play);
    assert(request.mReplayPath.filename() == L"测试.rpl");
    const std::array<std::uint8_t, 3> archiveManifest{1, 2, 3};
    assert(rareplay::plugin::MatchesArchiveManifest(request, archiveManifest));
    request.mManifest.assign(archiveManifest.begin(), archiveManifest.end());
    assert(rareplay::plugin::MatchesArchiveManifest(request, archiveManifest));
    request.mManifest[1] = 4;
    assert(!rareplay::plugin::MatchesArchiveManifest(request, archiveManifest));

    auto changed = bytes;
    changed[12] = 1;
    assert(!rareplay::plugin::ParseReplayRequest(changed, NOW, request, error));
    changed = bytes;
    changed[48 + 11] = 0xFF;
    assert(!rareplay::plugin::ParseReplayRequest(changed, NOW, request, error));
    assert(!rareplay::plugin::ParseReplayRequest(bytes, NOW + 301'000, request, error));
    changed = bytes;
    changed[40] = 0xFF;
    assert(!rareplay::plugin::ParseReplayRequest(changed, NOW, request, error));

    const auto path = std::filesystem::temp_directory_path() / L"rareplay-request-test.bin";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    changed = bytes;
    changed[24] ^= 1;
    assert(!rareplay::plugin::ConsumeReplayRequest(path, changed, error));
    assert(std::filesystem::exists(path));
    std::filesystem::remove(path);
}
