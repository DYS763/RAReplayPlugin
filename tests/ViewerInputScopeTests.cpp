#include "../src/hooks/ViewerInputScope.h"

#include <stdexcept>

int main()
{
    rareplay::hooks::ViewerInputScope scope;
    if (rareplay::hooks::ShouldSuppressViewerControl(scope, true, 0x700650))
        throw std::runtime_error("control caller outside native input scope must retain original result");
    scope.Enter();
    scope.Enter();
    if (!rareplay::hooks::ShouldSuppressViewerControl(scope, true, 0x700650) ||
        rareplay::hooks::ShouldSuppressViewerControl(scope, false, 0x700650))
        throw std::runtime_error("only active playback input may suppress player control");
    for (const auto caller : {0x4C70C7u, 0x4DAD24u, 0x5F5280u})
    {
        if (rareplay::hooks::ShouldSuppressViewerControl(scope, true, caller))
            throw std::runtime_error("simulation and helper callers must retain original result");
    }
    scope.Leave();
    if (!scope.IsActive())
        throw std::runtime_error("nested input scope must remain active until final exit");
    scope.Leave();
    scope.Leave();
    if (scope.IsActive())
        throw std::runtime_error("scope exit must not underflow");
    scope.Enter();
    scope.Reset();
    if (scope.IsActive())
        throw std::runtime_error("session cleanup must clear unfinished input scope");
}
