#pragma once

#include <filesystem>
#include <string>

namespace rareplay::plugin
{
    // 启动时在游戏目录 debug/RAReplayPlugin.log 打开单一追加日志。
    bool OpenBootstrapDiagnostics(const std::filesystem::path& gameDirectory,
        std::string& outError);
    void Log(const char* message) noexcept;
    void CloseDiagnostics() noexcept;
}
