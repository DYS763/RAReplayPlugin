#include "../src/render/IconTextureBudget.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
}

int main()
{
    try
    {
        rareplay::render::IconTextureBudget budget;
        constexpr std::size_t ICON_BYTES = 16U * 1024U;
        for (std::size_t index = 0; index < budget.MAX_TEXTURES; ++index)
        {
            Require(budget.CanAdd(ICON_BYTES), "All 1024 small icons fit the type budget");
            budget.Commit(ICON_BYTES);
        }
        Require(budget.GetCount() == 1024 && !budget.CanAdd(1),
                "Type limit prevents an extra texture without evicting an in-frame reference");
        budget.Reset();
        Require(budget.GetCount() == 0 && budget.GetUsedBytes() == 0,
                "Session/reset cleanup releases budget counters");

        constexpr std::size_t HALF_BUDGET = 16U * 1024U * 1024U;
        Require(budget.CanAdd(HALF_BUDGET), "First half budget fits");
        budget.Commit(HALF_BUDGET);
        Require(budget.CanAdd(HALF_BUDGET), "Exact 32 MiB RGBA budget fits");
        budget.Commit(HALF_BUDGET);
        Require(budget.GetUsedBytes() == budget.MAX_BYTES && !budget.CanAdd(1),
                "RGBA byte limit prevents excess upload");
        budget.Reset();
        Require(!budget.CanAdd(budget.MAX_BYTES + 1) && budget.CanAdd(1),
                "Oversized image is rejected without poisoning later uploads");
        std::cout << "GPU icon texture budget tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
