#include "../src/replay/FrameCodec.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace rareplay::replay;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::istringstream Input(const std::vector<std::uint8_t>& bytes)
{
    return std::istringstream(std::string(bytes.begin(), bytes.end()), std::ios::binary);
}

void TestHeaderLayoutAndValidation()
{
    ReplaySessionInfo info{7, 2, 0, 6, 0x12345678};
    std::array<std::uint8_t, FrameCodec::HEADER_BYTES> header{};
    std::string error;
    Require(FrameCodec::EncodeHeader(info, header, error), "header should encode");
    const std::array<std::uint8_t, 32> expected = {
        0x52, 0x50, 0x46, 0x32, 2, 0, 0, 0, 111, 0, 0, 0,
        7, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0,
        0x78, 0x56, 0x34, 0x12};
    Require(header == expected, "stream header must match the V2 byte contract");
    auto stream = Input(std::vector<std::uint8_t>(header.begin(), header.end()));
    ReplaySessionInfo decoded;
    Require(FrameCodec::DecodeHeader(stream, decoded, error), "header should decode");
    Require(decoded.mGameMode == 7 && decoded.mLocalHouseIndex == 2 &&
        decoded.mFirstFrame == 0 && decoded.mFrameSendRate == 6 &&
        decoded.mRandomSeed == 0x12345678 && decoded.mStreamVersion == 2,
        "session fields should round trip");

    auto legacy = expected;
    legacy[4] = 1;
    stream = Input(std::vector<std::uint8_t>(legacy.begin(), legacy.end()));
    Require(FrameCodec::DecodeHeader(stream, decoded, error) && decoded.mStreamVersion == 1,
        "legacy header remains readable");

    auto invalid = expected;
    invalid[8] = 110;
    stream = Input(std::vector<std::uint8_t>(invalid.begin(), invalid.end()));
    Require(!FrameCodec::DecodeHeader(stream, decoded, error), "wrong event size must fail");
    stream = Input(std::vector<std::uint8_t>(expected.begin(), expected.begin() + 31));
    Require(!FrameCodec::DecodeHeader(stream, decoded, error), "short header must fail");
}

void TestFramesAndEndBoundary()
{
    ReplayFrame empty;
    empty.mFrameNumber = 0;
    std::vector<std::uint8_t> bytes;
    std::string error;
    Require(FrameCodec::EncodeFrame(empty, bytes, error), "empty frame should encode");
    Require(bytes.size() == 20 && bytes[12] == 0 && bytes[16] == 0,
        "empty frame uses a 20 byte record");

    ReplayFrame ordered;
    ordered.mFrameNumber = 1;
    ordered.mChecksum = 0x01020304;
    ordered.mHasChecksum = true;
    RecordedEvent first{};
    RecordedEvent second{};
    first[0] = 0xa1;
    second[0] = 0xb2;
    ordered.mEvents = {first, second};
    RecordedInputEffect effect;
    effect.mAnimationTypeIndex = 17;
    effect.mX = -23;
    effect.mY = 45;
    effect.mZ = -67;
    effect.mLoopDelay = 8;
    effect.mLoopCount = 2;
    effect.mFlags = 0x12345678;
    effect.mForceZAdjust = -9;
    effect.mReverse = true;
    ordered.mInputEffects.push_back(effect);
    std::vector<std::uint8_t> secondBytes;
    Require(FrameCodec::EncodeFrame(ordered, secondBytes, error), "event frame should encode");
    Require(secondBytes.size() == 20 + 2 * 111 + 40 && secondBytes[4] == 4 &&
        secondBytes[7] == 1 && secondBytes[8] == 2 && secondBytes[12] == 1 &&
        secondBytes[16] == 1 && secondBytes[20] == 0xa1 && secondBytes[131] == 0xb2 &&
        secondBytes[242] == 1 && secondBytes[246] == 17 && secondBytes[278] == 1,
        "frame bytes must preserve order, checksum and flag");
    bytes.insert(bytes.end(), secondBytes.begin(), secondBytes.end());
    auto stream = Input(bytes);
    ReplayFrame decoded;
    Require(FrameCodec::ReadFrame(stream, decoded, error) == ReplayReadResult::Frame &&
        decoded.mFrameNumber == 0 && decoded.mEvents.empty() && !decoded.mHasChecksum,
        "empty frame should round trip");
    Require(FrameCodec::ReadFrame(stream, decoded, error) == ReplayReadResult::Frame &&
        decoded.mFrameNumber == 1 && decoded.mHasChecksum &&
        decoded.mChecksum == 0x01020304 && decoded.mEvents.size() == 2 &&
        decoded.mEvents[0][0] == 0xa1 && decoded.mEvents[1][0] == 0xb2 &&
        decoded.mInputEffects.size() == 1 &&
        decoded.mInputEffects[0].mAnimationTypeIndex == 17 &&
        decoded.mInputEffects[0].mX == -23 && decoded.mInputEffects[0].mY == 45 &&
        decoded.mInputEffects[0].mZ == -67 &&
        decoded.mInputEffects[0].mLoopDelay == 8 &&
        decoded.mInputEffects[0].mLoopCount == 2 &&
        decoded.mInputEffects[0].mFlags == 0x12345678 &&
        decoded.mInputEffects[0].mForceZAdjust == -9 &&
        decoded.mInputEffects[0].mReverse,
        "ordered events should round trip");
    Require(FrameCodec::ReadFrame(stream, decoded, error) == ReplayReadResult::End,
        "complete frame boundary is clean EOF");
}

void TestInvalidRecords()
{
    ReplayFrame frame;
    frame.mFrameNumber = 4;
    frame.mEvents.resize(1);
    std::vector<std::uint8_t> bytes;
    std::string error;
    Require(FrameCodec::EncodeFrame(frame, bytes, error), "test frame should encode");
    auto truncated = bytes;
    truncated.pop_back();
    auto stream = Input(truncated);
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "truncated event must fail");

    auto invalidFlags = bytes;
    invalidFlags[12] = 2;
    stream = Input(invalidFlags);
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "reserved flags must fail");

    auto excessive = bytes;
    excessive[8] = 1;
    excessive[9] = 64;
    stream = Input(excessive);
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "event count above 16384 must fail before allocation");

    auto excessiveEffects = bytes;
    excessiveEffects[16] = 1;
    excessiveEffects[17] = 16;
    stream = Input(excessiveEffects);
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "effect count above 4096 must fail before allocation");

    ReplayFrame effectFrame;
    effectFrame.mInputEffects.push_back({});
    Require(FrameCodec::EncodeFrame(effectFrame, bytes, error), "effect frame should encode");
    auto invalidKind = bytes;
    invalidKind[20] = 2;
    stream = Input(invalidKind);
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "unknown effect kind must fail");
    auto invalidBoolean = bytes;
    invalidBoolean[56] = 2;
    stream = Input(invalidBoolean);
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "effect reverse must be zero or one");
    bytes.pop_back();
    stream = Input(bytes);
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "truncated effect must fail");

    std::vector<std::uint8_t> legacy(16, 0);
    stream = Input(legacy);
    Require(FrameCodec::ReadFrame(stream, frame, error, 1) == ReplayReadResult::Frame &&
        frame.mInputEffects.empty(), "legacy frame remains readable with empty effects");

    ReplayFrame tooMany;
    tooMany.mEvents.resize(FrameCodec::MAX_EVENTS_PER_FRAME + 1);
    Require(!FrameCodec::EncodeFrame(tooMany, bytes, error), "encoder must reject oversize frame");

    stream = Input({0x91});
    Require(FrameCodec::ReadFrame(stream, frame, error) == ReplayReadResult::Error,
        "trailing byte is not clean EOF");
}
}

int main()
{
    TestHeaderLayoutAndValidation();
    TestFramesAndEndBoundary();
    TestInvalidRecords();
}
