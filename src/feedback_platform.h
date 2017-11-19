#ifndef FEEDBACK_PLATFORM_H
#define FEEDBACK_PLATFORM_H

#include "nps_common_defs.h"

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "imgui.h"

#define PLATFORM_PRINTF(Name) void Name(char *Format, ...)
typedef PLATFORM_PRINTF(platform_printf);

#define PLATFORM_LOG(Name) void Name(char *Format, ...)
typedef PLATFORM_LOG(platform_log);

#define APP_INITIALIZE(Name) bool32 Name(void *Memory, uint64_t MemorySize, \
                                         uint32_t WindowWidth, uint32_t WindowHeight, \
                                         float TimeBetweenFrames, \
                                         ImGuiContext *ImGuiContext, \
                                         platform_printf *PlatformPrintf, \
                                         platform_log *PlatformLog)
typedef APP_INITIALIZE(app_initialize);

#define APP_UPDATE_AND_RENDER(Name) void Name(void *Memory, bool32 CodeReload, float CurrentTime, struct app_input *Input)
typedef APP_UPDATE_AND_RENDER(app_update_and_render);

enum app_key
{
    // NOTE: Letters MUST be first (see KeyToChar()).
    AppKey_A,
    AppKey_B,
    AppKey_C,
    AppKey_D,
    AppKey_E,
    AppKey_F,
    AppKey_G,
    AppKey_H,
    AppKey_I,
    AppKey_J,
    AppKey_K,
    AppKey_L,
    AppKey_M,
    AppKey_N,
    AppKey_O,
    AppKey_P,
    AppKey_Q,
    AppKey_R,
    AppKey_S,
    AppKey_T,
    AppKey_U,
    AppKey_V,
    AppKey_W,
    AppKey_X,
    AppKey_Y,
    AppKey_Z,

    AppKey_TAB,
    AppKey_LEFT,
    AppKey_RIGHT,
    AppKey_UP,
    AppKey_DOWN,
    AppKey_PAGEUP,
    AppKey_PAGEDOWN,
    AppKey_HOME,
    AppKey_END,
    AppKey_DELETE,
    AppKey_BACKSPACE,
    AppKey_ENTER,
    AppKey_ESCAPE,
    AppKey_SHIFT,
};

struct app_input
{
    bool32 MousePressed;
    bool32 MouseDown;
    bool32 MouseReleased;
    bool32 ZoomInPressed;
    bool32 ZoomOutPressed;

    int32_t MouseX;
    int32_t MouseY;
    int32_t FlippedMouseY;

    int32_t MouseDeltaX;
    int32_t MouseDeltaY;
};

#endif
