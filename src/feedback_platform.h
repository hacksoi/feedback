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

#define PLATFORM_PRINTF(Name) void Name(char *Format, ...)
typedef PLATFORM_PRINTF(platform_printf);

#define APP_CODE_INITIALIZE(Name) void Name(void *Memory, uint64 MemorySize, \
                                            uint32 WindowWidth, uint32 WindowHeight, \
                                            uint32 TimeBetweenFramesMillis, \
                                            ImGuiContext *ImGuiContext, \
                                            platform_printf *PlatformPrintf)
typedef APP_CODE_INITIALIZE(app_code_initialize);

#define APP_CODE_HANDLE_EVENT(Name) void Name(void *Memory, real32 CurrentTime, struct app_event *Event)
typedef APP_CODE_HANDLE_EVENT(app_code_handle_event);

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

enum app_event_type
{
    AppEventType_Initialize,
    AppEventType_CodeReload,
    AppEventType_UpdateAndRender,
    AppEventType_TouchUp,
    AppEventType_TouchDown,
    AppEventType_TouchMovement,
    AppEventType_NonTouchMovement,
    AppEventType_ZoomInPressed,
    AppEventType_ZoomOutPressed,
};

struct app_event
{
    app_event_type Type;

    union
    {
        // touch down
        struct
        {
            int TouchX, TouchY;
        };
    };
};

#endif
