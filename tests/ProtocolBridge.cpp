#include "../src/plugin/ReplayConfig.h"
#include "../src/storage/FrameWriter.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
bool ReadFile(const std::filesystem::path& path, std::vector<std::uint8_t>& outBytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }
    outBytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad();
}

bool Run(const std::filesystem::path& directory, std::string& error)
{
    rareplay::plugin::ReplayRequest request;
    std::vector<std::uint8_t> rawRequest;
    if (!rareplay::plugin::ReadReplayRequest(directory / L"request.bin", request,
        rawRequest, error))
    {
        return false;
    }
    std::vector<std::uint8_t> expectedManifest;
    if (!ReadFile(directory / L"manifest.bin", expectedManifest) ||
        request.mMode != rareplay::plugin::ReplayMode::Record ||
        request.mReplayPath != directory / L"bridge.yrrep" ||
        request.mManifest != expectedManifest)
    {
        error = "C# request or opaque manifest did not survive C++ parsing";
        return false;
    }
    if (request.mRequestId == std::array<std::uint8_t, 16>{})
    {
        error = "C# request ID is empty";
        return false;
    }

    std::array<std::uint8_t, 32> targetHash{};
    for (std::size_t index = 0; index < targetHash.size(); ++index)
    {
        targetHash[index] = static_cast<std::uint8_t>(index + 1);
    }
    rareplay::replay::ReplaySessionInfo sessionInfo{3, 1, 0, 6, 0x12345678};
    rareplay::storage::FrameWriter writer;
    if (!writer.Open(request.mReplayPath.wstring(), targetHash, request.mManifest,
        sessionInfo, error))
    {
        return false;
    }
    rareplay::replay::ReplayFrame empty;
    if (!writer.AppendFrame(empty, error))
    {
        writer.Abort();
        return false;
    }
    rareplay::replay::ReplayFrame eventFrame;
    eventFrame.mFrameNumber = 1;
    eventFrame.mChecksum = 0xdeadbeef;
    eventFrame.mHasChecksum = true;
    rareplay::replay::RecordedEvent event{};
    for (std::size_t index = 0; index < event.size(); ++index)
    {
        event[index] = static_cast<std::uint8_t>(index);
    }
    eventFrame.mEvents.push_back(event);
    rareplay::replay::RecordedInputEffect effect;
    effect.mAnimationTypeIndex = 17;
    effect.mX = -23;
    effect.mY = 45;
    effect.mZ = -67;
    effect.mLoopDelay = 8;
    effect.mLoopCount = 2;
    effect.mFlags = 0x12345678;
    effect.mForceZAdjust = -9;
    effect.mReverse = true;
    eventFrame.mInputEffects.push_back(effect);
    if (!writer.AppendFrame(eventFrame, error) || !writer.Finish(error))
    {
        writer.Abort();
        return false;
    }
    return true;
}
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: ProtocolBridge <temporary directory>\n";
        return 2;
    }
    std::string error;
    if (!Run(argv[1], error))
    {
        std::cerr << "protocol bridge failed: " << error << '\n';
        return 1;
    }
    std::cout << "C++ request parsing and frame writing passed\n";
    return 0;
}
