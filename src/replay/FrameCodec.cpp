#include "FrameCodec.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

namespace rareplay::replay
{
namespace
{
constexpr std::uint32_t STREAM_MAGIC = 0x32465052;
constexpr std::uint32_t STREAM_VERSION = 2;
constexpr std::uint32_t EVENT_BYTES = 111;
constexpr std::uint32_t CHECKSUM_PRESENT = 1;
constexpr std::size_t FRAME_HEADER_BYTES = 16;
constexpr std::size_t EFFECT_COUNT_BYTES = 4;
constexpr std::size_t EFFECT_BYTES = 40;

void StoreU32(std::uint8_t* destination, std::uint32_t value)
{
    for (unsigned int index = 0; index < 4; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

std::uint32_t LoadU32(const std::uint8_t* source)
{
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8);
    }
    return value;
}
}

bool FrameCodec::EncodeHeader(const ReplaySessionInfo& info,
    std::array<std::uint8_t, HEADER_BYTES>& outBytes, std::string& error)
{
    if (info.mFirstFrame != 0)
    {
        error = "First replay frame must be zero";
        return false;
    }
    outBytes.fill(0);
    StoreU32(outBytes.data(), STREAM_MAGIC);
    StoreU32(outBytes.data() + 4, STREAM_VERSION);
    StoreU32(outBytes.data() + 8, EVENT_BYTES);
    StoreU32(outBytes.data() + 12, info.mGameMode);
    StoreU32(outBytes.data() + 16, info.mLocalHouseIndex);
    StoreU32(outBytes.data() + 20, info.mFirstFrame);
    StoreU32(outBytes.data() + 24, info.mFrameSendRate);
    StoreU32(outBytes.data() + 28, info.mRandomSeed);
    error.clear();
    return true;
}

bool FrameCodec::DecodeHeader(std::istream& stream, ReplaySessionInfo& outInfo, std::string& error)
{
    std::array<std::uint8_t, HEADER_BYTES> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size()))
    {
        error = "Truncated replay stream header";
        return false;
    }
    const std::uint32_t version = LoadU32(bytes.data() + 4);
    if (LoadU32(bytes.data()) != STREAM_MAGIC || (version != 1 && version != STREAM_VERSION) ||
        LoadU32(bytes.data() + 8) != EVENT_BYTES || LoadU32(bytes.data() + 20) != 0)
    {
        error = "Invalid or unsupported replay stream header";
        return false;
    }
    outInfo.mGameMode = LoadU32(bytes.data() + 12);
    outInfo.mLocalHouseIndex = LoadU32(bytes.data() + 16);
    outInfo.mFirstFrame = 0;
    outInfo.mFrameSendRate = LoadU32(bytes.data() + 24);
    outInfo.mRandomSeed = LoadU32(bytes.data() + 28);
    outInfo.mStreamVersion = version;
    error.clear();
    return true;
}

bool FrameCodec::EncodeFrame(const ReplayFrame& frame, std::vector<std::uint8_t>& outBytes,
    std::string& error)
{
    if (frame.mEvents.size() > MAX_EVENTS_PER_FRAME)
    {
        error = "Replay frame exceeds the event limit";
        return false;
    }
    if (frame.mInputEffects.size() > MAX_INPUT_EFFECTS_PER_FRAME)
    {
        error = "Replay frame exceeds the input effect limit";
        return false;
    }
    for (const RecordedInputEffect& effect : frame.mInputEffects)
    {
        if (effect.mKind != PlayerInputEffectKind::MovementFeedback)
        {
            error = "Replay input effect kind is invalid";
            return false;
        }
    }
    const std::size_t eventsOffset = FRAME_HEADER_BYTES + EFFECT_COUNT_BYTES;
    const std::size_t effectsOffset = eventsOffset + frame.mEvents.size() * EVENT_BYTES;
    outBytes.resize(effectsOffset + frame.mInputEffects.size() * EFFECT_BYTES);
    StoreU32(outBytes.data(), frame.mFrameNumber);
    StoreU32(outBytes.data() + 4, frame.mChecksum);
    StoreU32(outBytes.data() + 8, static_cast<std::uint32_t>(frame.mEvents.size()));
    StoreU32(outBytes.data() + 12, frame.mHasChecksum ? CHECKSUM_PRESENT : 0);
    StoreU32(outBytes.data() + FRAME_HEADER_BYTES,
        static_cast<std::uint32_t>(frame.mInputEffects.size()));
    for (std::size_t index = 0; index < frame.mEvents.size(); ++index)
    {
        std::memcpy(outBytes.data() + eventsOffset + index * EVENT_BYTES,
            frame.mEvents[index].data(), EVENT_BYTES);
    }
    for (std::size_t index = 0; index < frame.mInputEffects.size(); ++index)
    {
        const RecordedInputEffect& effect = frame.mInputEffects[index];
        std::uint8_t* destination = outBytes.data() + effectsOffset + index * EFFECT_BYTES;
        StoreU32(destination, static_cast<std::uint32_t>(effect.mKind));
        StoreU32(destination + 4, effect.mAnimationTypeIndex);
        StoreU32(destination + 8, static_cast<std::uint32_t>(effect.mX));
        StoreU32(destination + 12, static_cast<std::uint32_t>(effect.mY));
        StoreU32(destination + 16, static_cast<std::uint32_t>(effect.mZ));
        StoreU32(destination + 20, static_cast<std::uint32_t>(effect.mLoopDelay));
        StoreU32(destination + 24, static_cast<std::uint32_t>(effect.mLoopCount));
        StoreU32(destination + 28, effect.mFlags);
        StoreU32(destination + 32, static_cast<std::uint32_t>(effect.mForceZAdjust));
        StoreU32(destination + 36, effect.mReverse ? 1 : 0);
    }
    error.clear();
    return true;
}

ReplayReadResult FrameCodec::ReadFrame(std::istream& stream, ReplayFrame& outFrame,
    std::string& error, std::uint32_t streamVersion)
{
    if (streamVersion != 1 && streamVersion != STREAM_VERSION)
    {
        error = "Unsupported replay stream version";
        return ReplayReadResult::Error;
    }
    std::array<std::uint8_t, FRAME_HEADER_BYTES> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (stream.gcount() == 0 && stream.eof())
    {
        error.clear();
        return ReplayReadResult::End;
    }
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size()))
    {
        error = "Truncated replay frame header";
        return ReplayReadResult::Error;
    }
    const std::uint32_t eventCount = LoadU32(bytes.data() + 8);
    const std::uint32_t flags = LoadU32(bytes.data() + 12);
    if (eventCount > MAX_EVENTS_PER_FRAME || (flags & ~CHECKSUM_PRESENT) != 0)
    {
        error = "Replay frame count or flags are invalid";
        return ReplayReadResult::Error;
    }
    std::uint32_t effectCount = 0;
    if (streamVersion == STREAM_VERSION)
    {
        std::array<std::uint8_t, EFFECT_COUNT_BYTES> countBytes{};
        stream.read(reinterpret_cast<char*>(countBytes.data()), countBytes.size());
        if (stream.gcount() != static_cast<std::streamsize>(countBytes.size()))
        {
            error = "Truncated replay input effect count";
            return ReplayReadResult::Error;
        }
        effectCount = LoadU32(countBytes.data());
        if (effectCount > MAX_INPUT_EFFECTS_PER_FRAME)
        {
            error = "Replay frame exceeds the input effect limit";
            return ReplayReadResult::Error;
        }
    }
    ReplayFrame frame;
    frame.mFrameNumber = LoadU32(bytes.data());
    frame.mChecksum = LoadU32(bytes.data() + 4);
    frame.mHasChecksum = (flags & CHECKSUM_PRESENT) != 0;
    frame.mEvents.resize(eventCount);
    for (auto& event : frame.mEvents)
    {
        stream.read(reinterpret_cast<char*>(event.data()), event.size());
        if (stream.gcount() != static_cast<std::streamsize>(event.size()))
        {
            error = "Truncated replay event";
            return ReplayReadResult::Error;
        }
    }
    frame.mInputEffects.reserve(effectCount);
    for (std::uint32_t index = 0; index < effectCount; ++index)
    {
        std::array<std::uint8_t, EFFECT_BYTES> effectBytes{};
        stream.read(reinterpret_cast<char*>(effectBytes.data()), effectBytes.size());
        if (stream.gcount() != static_cast<std::streamsize>(effectBytes.size()))
        {
            error = "Truncated replay input effect";
            return ReplayReadResult::Error;
        }
        const std::uint32_t kind = LoadU32(effectBytes.data());
        const std::uint32_t reverse = LoadU32(effectBytes.data() + 36);
        if (kind != static_cast<std::uint32_t>(PlayerInputEffectKind::MovementFeedback) || reverse > 1)
        {
            error = "Replay input effect kind or boolean is invalid";
            return ReplayReadResult::Error;
        }
        RecordedInputEffect effect;
        effect.mKind = static_cast<PlayerInputEffectKind>(kind);
        effect.mAnimationTypeIndex = LoadU32(effectBytes.data() + 4);
        effect.mX = std::bit_cast<std::int32_t>(LoadU32(effectBytes.data() + 8));
        effect.mY = std::bit_cast<std::int32_t>(LoadU32(effectBytes.data() + 12));
        effect.mZ = std::bit_cast<std::int32_t>(LoadU32(effectBytes.data() + 16));
        effect.mLoopDelay = std::bit_cast<std::int32_t>(LoadU32(effectBytes.data() + 20));
        effect.mLoopCount = std::bit_cast<std::int32_t>(LoadU32(effectBytes.data() + 24));
        effect.mFlags = LoadU32(effectBytes.data() + 28);
        effect.mForceZAdjust = std::bit_cast<std::int32_t>(LoadU32(effectBytes.data() + 32));
        effect.mReverse = reverse == 1;
        frame.mInputEffects.push_back(effect);
    }
    outFrame = std::move(frame);
    error.clear();
    return ReplayReadResult::Frame;
}
}
