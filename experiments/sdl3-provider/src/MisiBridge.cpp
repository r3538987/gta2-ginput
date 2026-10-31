/* MISI v0.5.0 / Lua 5.3.4 bridge. Replaces only its three XInput callbacks. */
#include "GInputSDL3.h"
extern "C" {
#include <lua.h>
}

namespace {
GInputSDL3State ReadSelectedPad() {
    GInputSDL3_Update();
    GInputSDL3State state{};
    const uint32_t slot = GInputSDL3_GetSelectedSlot();
    if (GInputSDL3_GetState(slot, &state, sizeof(state)) && state.connected) return state;
    return {};
}
}

int l_GetXInputConnectedDevicesNumber(lua_State* state) {
    GInputSDL3_Update();
    lua_pushinteger(state, GInputSDL3_GetConnectedCount());
    return 1;
}

int l_IsXInputKeyPress(lua_State* state) {
    int numeric = 0;
    const lua_Integer button = lua_tointegerx(state, 1, &numeric);
    const auto pad = ReadSelectedPad();
    lua_pushboolean(state, numeric && button >= 0 && button < 16 &&
        (pad.buttons & (1u << static_cast<unsigned int>(button))) != 0);
    return 1;
}

int l_GetXInputAxes(lua_State* state) {
    const auto pad = ReadSelectedPad();
    lua_pushnumber(state, pad.leftX);
    lua_pushnumber(state, pad.leftY);
    lua_pushnumber(state, pad.rightX);
    lua_pushnumber(state, pad.rightY);
    return 4;
}
