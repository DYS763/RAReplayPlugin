#pragma once

#include "../ReplayWorldReader.h"
#include "../../assets/IconLoadCache.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class TechnoTypeClass;

namespace rareplay::game_api
{
/** 仅游戏线程调用；只缓存 RGBA 副本，原生资源由引擎自己持有。 */
class CameoReader final
{
public:
    void Clear() noexcept;
    void BeginSample() noexcept;
    std::shared_ptr<const viewer::IconImage> Resolve(const TechnoTypeClass* type);
    const IconStats& GetStats() const noexcept { return mStats; }

private:
    struct PcxEntry
    {
        std::weak_ptr<const viewer::IconImage> mImage;
        bool mMissing = false;
        bool mInvalidSurface = false;
    };
    std::shared_ptr<const viewer::IconImage> Load(const TechnoTypeClass* type, const std::string& typeId);
    assets::IconLoadCache mTypes{1024, 4, 32U * 1024U * 1024U};
    std::unordered_map<std::string, PcxEntry> mPcxFiles;
    IconStats mStats;
};
}
