#include "ReplayViewExchange.h"

namespace rareplay::viewer
{
void ReplayViewExchange::Publish(std::shared_ptr<const ReplayViewSnapshot> snapshot) noexcept
{
    mSnapshot.store(std::move(snapshot));
}

std::shared_ptr<const ReplayViewSnapshot> ReplayViewExchange::Read() const noexcept
{
    return mSnapshot.load();
}

void ReplayViewExchange::Submit(ViewerCommand command)
{
    std::lock_guard lock(mCommandMutex);
    // 速度和跳转只需要最后一个值；限制队列，避免暂停或拖动期间无限积压。
    if (command.mKind == ViewerCommandKind::SetPlaybackSpeed ||
        command.mKind == ViewerCommandKind::SeekFrame)
    {
        for (auto iterator = mCommands.rbegin(); iterator != mCommands.rend(); ++iterator)
        {
            if (iterator->mSessionId == command.mSessionId && iterator->mKind == command.mKind)
            {
                *iterator = command;
                return;
            }
        }
    }
    if (mCommands.size() >= 32)
        mCommands.erase(mCommands.begin());
    mCommands.push_back(command);
}

std::vector<ViewerCommand> ReplayViewExchange::Drain()
{
    std::lock_guard lock(mCommandMutex);
    std::vector<ViewerCommand> commands;
    commands.swap(mCommands);
    return commands;
}

void ReplayViewExchange::Clear()
{
    Publish(nullptr);
    std::lock_guard lock(mCommandMutex);
    mCommands.clear();
}

ReplayViewExchange& GetReplayViewExchange() noexcept
{
    static ReplayViewExchange exchange;
    return exchange;
}
}
