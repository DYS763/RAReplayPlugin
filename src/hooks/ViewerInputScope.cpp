#include "ViewerInputScope.h"

#include "../target/ViewerControlCallers.h"

namespace rareplay::hooks
{
namespace
{
ViewerInputScope g_ViewerInputScope;
}

void ViewerInputScope::Enter() noexcept
{
    ++mDepth;
}

void ViewerInputScope::Leave() noexcept
{
    if (mDepth > 0)
        --mDepth;
}

void ViewerInputScope::Reset() noexcept
{
    mDepth = 0;
}

bool ViewerInputScope::IsActive() const noexcept
{
    return mDepth > 0;
}

ViewerInputScope& GetViewerInputScope() noexcept
{
    return g_ViewerInputScope;
}

bool ShouldSuppressViewerControl(const ViewerInputScope& scope,
    bool isPlaybackActive, std::uintptr_t caller) noexcept
{
    return scope.IsActive() && isPlaybackActive &&
        target::IsViewerControlCaller(caller);
}
}
