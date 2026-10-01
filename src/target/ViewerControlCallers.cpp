#include "ViewerControlCallers.h"

#include <array>

namespace rareplay::target
{
namespace
{
struct AddressRange
{
    std::uintptr_t mBegin;
    std::uintptr_t mEnd;
};

constexpr std::array<AddressRange, 12> VIEWER_CONTROL_CALLERS{{
    {0x417CC0, 0x417F74},
    {0x417F80, 0x417FD0},
    {0x447540, 0x447778},
    {0x51E3B0, 0x51F18A},
    {0x51F800, 0x51FAED},
    {0x692610, 0x692B5F},
    {0x6FFEC0, 0x7005F2},
    {0x700600, 0x700C32},
    {0x700C40, 0x700D08},
    {0x730AF0, 0x730D5D},
    {0x73FD50, 0x7404B0},
    {0x7404B0, 0x74080F}
}};
}

bool IsViewerControlCaller(std::uintptr_t caller) noexcept
{
    for (const auto& range : VIEWER_CONTROL_CALLERS)
    {
        if (caller >= range.mBegin && caller < range.mEnd)
            return true;
    }
    return false;
}
}
