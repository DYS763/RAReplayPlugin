#include "ReplayViewExchange.h"
#include "../src/replay/ReplaySeek.h"

#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main()
{
    try
    {
        rareplay::viewer::ReplayViewExchange exchange;
        Require(!exchange.Read(), "Initially empty");
        auto first = std::make_shared<rareplay::viewer::ReplayViewSnapshot>();
        first->mSessionId = 1;
        first->mFrameNumber = 45;
        exchange.Publish(first);
        const auto held = exchange.Read();
        std::thread producer([&exchange]
        {
            for (std::uint32_t frame = 46; frame < 1000; ++frame)
            {
                auto snapshot = std::make_shared<rareplay::viewer::ReplayViewSnapshot>();
                snapshot->mSessionId = 1;
                snapshot->mFrameNumber = frame;
                exchange.Publish(std::move(snapshot));
            }
        });
        for (int index = 0; index < 1000; ++index)
        {
            const auto snapshot = exchange.Read();
            Require(snapshot && snapshot->mSessionId == 1, "Reader sees a complete owned snapshot");
        }
        producer.join();
        Require(held->mFrameNumber == 45, "Old snapshot outlives publication replacement");
        for (int value = 0; value < 100; ++value)
            exchange.Submit({1, rareplay::viewer::ViewerCommandKind::SetPlaybackSpeed, value});
        const auto commands = exchange.Drain();
        Require(commands.size() == 1 && commands.back().mValue == 99,
            "Playback speed keeps only the newest input");
        Require(exchange.Drain().empty(), "Draining does not replay commands");
        exchange.Submit({1, rareplay::viewer::ViewerCommandKind::SeekFrame, 0, 100});
        exchange.Submit({1, rareplay::viewer::ViewerCommandKind::SeekFrame, 0, 200});
        auto seekCommands = exchange.Drain();
        Require(seekCommands.size() == 1 &&
                    seekCommands.front().mKind == rareplay::viewer::ViewerCommandKind::SeekFrame &&
                    seekCommands.front().mFrameNumber == 200,
                "Seek commands should coalesce to the latest target frame");

        rareplay::replay::ReplayFrameIndex index(2);
        std::string indexError;
        Require(index.AddFrame(0, 32, indexError) && index.AddFrame(1, 60, indexError) &&
                    index.AddFrame(2, 88, indexError) && index.AddFrame(3, 116, indexError) &&
                    index.Finish(indexError) && index.GetFrameCount() == 4,
                "Frame index should accept a contiguous stream");
        const auto checkpoint = index.FindCheckpoint(3);
        Require(checkpoint && checkpoint->mFrameNumber == 2 && checkpoint->mStreamOffset == 88,
                "Frame index should select the nearest checkpoint");
        Require(!index.FindCheckpoint(4), "Frame index should reject targets past EOF");

        exchange.Clear();
        Require(!exchange.Read() && exchange.Drain().empty(), "Session end clears snapshot and commands");
        Require(held->mFrameNumber == 45, "Render reader remains valid during session end");
        std::cout << "Replay view exchange tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
