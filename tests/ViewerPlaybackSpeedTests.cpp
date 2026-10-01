#include "../src/game_api/ViewerPlaybackSpeed.h"

#include <stdexcept>

int main()
{
    rareplay::game_api::ViewerPlaybackSpeed speed;
    int setting = -1;
    if (speed.TryGet(setting) || setting != -1)
        throw std::runtime_error("unset viewer speed must use native simulation speed");
    if (!speed.TrySet(0) || !speed.TryGet(setting) || setting != 0 ||
        !speed.TrySet(6) || !speed.TryGet(setting) || setting != 6)
        throw std::runtime_error("viewer speed endpoints must be retained locally");
    if (speed.TrySet(-1) || speed.TrySet(7) || !speed.TryGet(setting) || setting != 6)
        throw std::runtime_error("invalid viewer speed must preserve prior override");
    rareplay::game_api::ViewerPlaybackSpeed nextSession;
    if (nextSession.TryGet(setting))
        throw std::runtime_error("viewer speed override must not survive a new session");
}
