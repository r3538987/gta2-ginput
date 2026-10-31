#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef GINPUT_SDL3_BUILD
#define GINPUT_API __declspec(dllexport)
#else
#define GINPUT_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Stable C ABI; no SDL or Plugin-SDK types cross the DLL boundary. */
#define GINPUT_SDL3_API_VERSION 1
#define GINPUT_SDL3_MAX_PADS 4
#define GINPUT_SDL3_NO_PAD UINT32_MAX

enum GInputSDL3Feature {
    GINPUT_GAME_INPUT = 1u,
    GINPUT_GAME_VIBRATION = 2u
};

/* Button bits match the button numbers used by TwinStick's MISI functions. */
enum GInputSDL3Button {
    GINPUT_DPAD_UP, GINPUT_DPAD_DOWN, GINPUT_DPAD_LEFT, GINPUT_DPAD_RIGHT,
    GINPUT_START, GINPUT_BACK, GINPUT_LEFT_STICK, GINPUT_RIGHT_STICK,
    GINPUT_LEFT_SHOULDER, GINPUT_RIGHT_SHOULDER,
    GINPUT_A, GINPUT_B, GINPUT_X, GINPUT_Y,
    GINPUT_LEFT_TRIGGER, GINPUT_RIGHT_TRIGGER
};

typedef struct GInputSDL3State {
    uint32_t apiVersion;
    uint32_t connected;
    uint32_t instanceId;
    uint32_t buttons;
    float leftX, leftY, rightX, rightY; /* -1..1; up is positive */
    float leftTrigger, rightTrigger;  /* 0..1 */
    uint64_t sampleNumber;
} GInputSDL3State;

GINPUT_API uint32_t __cdecl GInputSDL3_GetAPIVersion(void);
/* Initialize/Shutdown: main thread, outside DllMain. NULL selects the sidecar INI. */
GINPUT_API int __cdecl GInputSDL3_Initialize(const wchar_t* iniPath);
GINPUT_API void __cdecl GInputSDL3_Shutdown(void);
/* Update and getters are serialized and may be called by MISI script threads. */
GINPUT_API int __cdecl GInputSDL3_Update(void);
GINPUT_API int __cdecl GInputSDL3_GetState(uint32_t slot, GInputSDL3State* state, uint32_t size);
GINPUT_API uint32_t __cdecl GInputSDL3_GetConnectedCount(void);
/* Same selection policy for TwinStick input and game vibration. Does not poll. */
GINPUT_API uint32_t __cdecl GInputSDL3_GetSelectedSlot(void);
GINPUT_API int __cdecl GInputSDL3_Rumble(uint32_t slot, float low, float high, uint32_t milliseconds);
GINPUT_API void __cdecl GInputSDL3_GetLastError(char* message, uint32_t capacity);
GINPUT_API int __cdecl GInputSDL3_AreGameInputHooksEnabled(void);
GINPUT_API int __cdecl GInputSDL3_IsGameVibrationEnabled(void);

#ifdef __cplusplus
}
static_assert(sizeof(GInputSDL3State) == 48, "Unexpected provider ABI layout");
#endif
