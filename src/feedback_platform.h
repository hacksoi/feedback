#ifndef FEEDBACK_PLATFORM_H
#define FEEDBACK_PLATFORM_H

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "imgui/imgui.h"

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float real32;
typedef double real64;
typedef int32_t bool32;

#define local static
#define global static
#define internal static

#define ArrayCount(Array) (sizeof(Array)/sizeof(Array[0]))
#define Assert(Expression) if(!(Expression)) { *((int *)0) = 0; }

#define Kilobytes(NumberOfKbs) (NumberOfKbs * 1024)
#define Megabytes(NumberOfMbs) (NumberOfMbs * 1024 * 1024)
#define Gigabytes(NumberOfGbs) (NumberOfGbs * 1024 * 1024 * 1024)

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

#define PLATFORM_GET_COUNTER(Name) uint64 Name()
#define PLATFORM_GET_SECONDS_ELAPSED(Name) real32 Name(uint64 StartCounter)
#define PLATFORM_DEBUG_PRINTF(Name) void Name(char *Format, ...)

typedef PLATFORM_GET_COUNTER(platform_get_counter);
typedef PLATFORM_GET_SECONDS_ELAPSED(platform_get_seconds_elapsed);
typedef PLATFORM_DEBUG_PRINTF(platform_debug_printf);

#define APP_CODE_INITIALIZE(Name) bool32 Name(void *Memory, uint64 MemorySize, \
                                              uint32 WindowWidth, uint32 WindowHeight, \
                                              uint32 TimeBetweenFramesMillis, \
                                              platform_get_counter *PlatformGetCounter, \
                                              platform_get_seconds_elapsed *PlatformGetSecondsElapsed, \
                                              platform_debug_printf *PlatformDebugPrintf, \
                                              ImGuiContext *ImGuiContext)
#define APP_CODE_RELOAD(Name) void Name(void *Memory)
#define APP_CODE_TOUCH_DOWN(Name) void Name(void *Memory, int32 TouchX, int32 TouchY)
#define APP_CODE_TOUCH_UP(Name) void Name(void *Memory, int32 TouchX, int32 TouchY)
#define APP_CODE_TOUCH_MOVEMENT(Name) void Name(void *Memory, int32 TouchX, int32 TouchY)
#define APP_CODE_NON_TOUCH_MOVEMENT(Name) void Name(void *Memory, int32 TouchX, int32 TouchY)
#define APP_CODE_ZOOM_IN(Name) void Name(void *Memory)
#define APP_CODE_ZOOM_OUT(Name) void Name(void *Memory)
#define APP_CODE_KEY_DOWN(Name) void Name(void *Memory, app_key Key)
#define APP_CODE_KEY_UP(Name) void Name(void *Memory, app_key Key)
#define APP_CODE_RENDER(Name) void Name(void *Memory)

typedef APP_CODE_INITIALIZE(app_code_initialize);
typedef APP_CODE_RELOAD(app_code_reload);
typedef APP_CODE_TOUCH_DOWN(app_code_touch_down);
typedef APP_CODE_TOUCH_UP(app_code_touch_up);
typedef APP_CODE_TOUCH_MOVEMENT(app_code_touch_movement);
typedef APP_CODE_NON_TOUCH_MOVEMENT(app_code_non_touch_movement);
typedef APP_CODE_ZOOM_IN(app_code_zoom_in);
typedef APP_CODE_ZOOM_OUT(app_code_zoom_out);
typedef APP_CODE_KEY_DOWN(app_code_key_down);
typedef APP_CODE_KEY_UP(app_code_key_up);
typedef APP_CODE_RENDER(app_code_render);

#endif
