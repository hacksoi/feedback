#include <Windows.h>
#include <Windowsx.h>
#include <stdint.h>

#include "GL/glew.h"
#include "GL/wglew.h"

#include "feedback_platform.h"

struct app_dll
{
    HMODULE Handle;
    FILETIME LastWriteTime;

    char *Filename, *TempFilename;

    app_code_initialize *Initialize;
    app_code_handle_event *HandleEvent;
};

global uint64 GlobalPerfCountFrequency;
global char GeneralBuffer[512];

/* api to application */
//{
inline internal uint64
Win32GetCounter()
{
    LARGE_INTEGER LargeInteger;
    QueryPerformanceCounter(&LargeInteger);

    uint64 Result = LargeInteger.QuadPart;
    return Result;
}

inline internal real32
Win32GetSecondsElapsed(uint64 StartCounter)
{
    real32 Result = (real32)(Win32GetCounter() - StartCounter) / (real32)GlobalPerfCountFrequency;
    return Result;
}

inline
PLATFORM_PRINTF(PlatformPrintf)
{
    va_list VarArgs;
    va_start(VarArgs, Format);

    vsprintf(GeneralBuffer, Format, VarArgs);
    OutputDebugString(GeneralBuffer);

    va_end(VarArgs);
}
//}

internal FILETIME
Win32GetLastWriteTime(char *Filename)
{
    FILETIME LastWriteTime = {};

    WIN32_FILE_ATTRIBUTE_DATA Data; 
    if(GetFileAttributesEx(Filename, GetFileExInfoStandard, &Data))
    {
        LastWriteTime = Data.ftLastWriteTime;
    }

    return LastWriteTime;
}

internal void
Win32PrintError(char *FunctionName)
{
    DWORD ErrorCode = GetLastError();

    char *Buffer;
    if(FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                     FORMAT_MESSAGE_FROM_SYSTEM,
                     NULL, 
                     ErrorCode, 
                     0, 
                     (LPTSTR)&Buffer, 
                     0, 
                     NULL) == 0)
    {
        // TODO: logging
        PlatformPrintf("Error: Win32PrintError()\n");
    }

    // TODO: logging
    PlatformPrintf("%s() failed: %s\n", FunctionName, Buffer);
}

LRESULT CALLBACK
WindowProc(HWND hwnd,
           UINT uMsg,
           WPARAM wParam,
           LPARAM lParam)
{
    LRESULT Result = 0;

    switch(uMsg)
    {
        case WM_CLOSE:
        {
            PostQuitMessage(0);
        } break;

        default:
        {
            Result = DefWindowProc(hwnd, uMsg, wParam, lParam);
        } break;
    }

    return Result;
}

internal void *
LoadFunction(HMODULE Module, char *FunctionName)
{
    void *FunctionAddress = (void *)GetProcAddress(Module, FunctionName);
    if(FunctionAddress == NULL)
    {
        Win32PrintError("GetProcAddress");
    }
    return FunctionAddress;
}

internal void
LoadAppCode(app_dll *AppDLL)
{
    if(CopyFile(AppDLL->Filename, AppDLL->TempFilename, false))
    {
        AppDLL->Handle = LoadLibrary(AppDLL->TempFilename);
        AppDLL->Initialize = (app_code_initialize *)LoadFunction(AppDLL->Handle, "AppInitialize");
        AppDLL->HandleEvent = (app_code_handle_event *)LoadFunction(AppDLL->Handle, "AppHandleEvent");

        if(AppDLL->Initialize && AppDLL->HandleEvent)
        {
            AppDLL->LastWriteTime = Win32GetLastWriteTime(AppDLL->Filename);
        }
    }
    else
    {
        Win32PrintError("CopyFile");
    }
}

internal app_key
ConvertToAppKey(WPARAM wParam)
{
    return (app_key)0;
}

int CALLBACK
WinMain(HINSTANCE hInstance,
        HINSTANCE hPrevInstance,
        LPSTR lpCmdLine,
        int nCmdShow)
{
    app_dll AppDLL = {};
    AppDLL.Filename = "feedback.dll";
    AppDLL.TempFilename = "feedback_temp.dll";
    LoadAppCode(&AppDLL);

    uint32 WindowWidth = 540;
    uint32 WindowHeight = 960;

    uint32 ScreenWidth = 1920;
    uint32 ScreenHeight = 1080;

    LARGE_INTEGER QueryPerformanceFrequencyLargeInteger;
    QueryPerformanceFrequency(&QueryPerformanceFrequencyLargeInteger);
    GlobalPerfCountFrequency = QueryPerformanceFrequencyLargeInteger.QuadPart;

    WNDCLASS WindowClass = {};
    WindowClass.style = CS_OWNDC;
    WindowClass.lpfnWndProc = WindowProc;
    WindowClass.hInstance = hInstance;
    WindowClass.lpszClassName = "Window Class";
    WindowClass.hCursor = LoadCursor(0, IDC_ARROW);

    if(!RegisterClass(&WindowClass))
    {
        // TODO: logging
        Win32PrintError("RegisterClass");
        return 1;
    }

    HWND WindowHandle = CreateWindowEx(0,
                                       WindowClass.lpszClassName,
                                       NULL,
                                       0,
                                       CW_USEDEFAULT,
                                       CW_USEDEFAULT,
                                       CW_USEDEFAULT,
                                       CW_USEDEFAULT,
                                       NULL,
                                       NULL,
                                       hInstance,
                                       NULL);

    if(WindowHandle == NULL)
    {
        // TODO: logging
        Win32PrintError("Dummy CreateWindowEx");
        return 1;
    }

    HDC DeviceContextHandle = GetDC(WindowHandle);
    if(DeviceContextHandle == NULL)
    {
        // TODO: logging
        Win32PrintError("Dummy GetDC");
        return 1;
    }

    PIXELFORMATDESCRIPTOR PixelFormatDescriptor = {};
    PixelFormatDescriptor.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    PixelFormatDescriptor.nVersion = 1;
    PixelFormatDescriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    PixelFormatDescriptor.iPixelType = PFD_TYPE_RGBA;
    PixelFormatDescriptor.cColorBits = 32;
    PixelFormatDescriptor.cDepthBits = 0;
    PixelFormatDescriptor.cStencilBits = 0;
    PixelFormatDescriptor.cAuxBuffers = 0;
    PixelFormatDescriptor.iLayerType = PFD_MAIN_PLANE;

    int PixelFormat = ChoosePixelFormat(DeviceContextHandle, &PixelFormatDescriptor);
    if(!PixelFormat)
    {
        // TODO: logging
        Win32PrintError("ChoosePixelFormat");
        return 1;
    }

    if(SetPixelFormat(DeviceContextHandle, PixelFormat, &PixelFormatDescriptor) == FALSE)
    {
        // TODO: logging
        Win32PrintError("SetPixelFormat");
        return 1;
    }

    HGLRC GLContextHandle = wglCreateContext(DeviceContextHandle);
    if(GLContextHandle == NULL)
    {
        // TODO: logging
        Win32PrintError("wglCreateContext");
        return 1;
    }

    if(wglMakeCurrent(DeviceContextHandle, GLContextHandle) == FALSE)
    {
        // TODO: logging
        Win32PrintError("wglMakeCurrent");
        return 1;
    }

    GLenum GlewInitStatusCode = glewInit();
    if(GlewInitStatusCode != GLEW_OK)
    {
        // TODO: logging
        PlatformPrintf("glewInit", (char *)glewGetErrorString(GlewInitStatusCode));
        return 1;
    }

    if(!WGLEW_ARB_pixel_format)
    {
        // TODO: logging
        PlatformPrintf("WGL_ARB_pixel_format not supported");
        return 1;
    }

    if(!WGLEW_ARB_create_context)
    {
        // TODO: logging
        PlatformPrintf("WGL_ARB_create_context not supported");
        return 1;
    }

    DestroyWindow(WindowHandle);

    RECT ClientRect = {0, 0, (LONG)WindowWidth, (LONG)WindowHeight};
    AdjustWindowRect(&ClientRect, WS_OVERLAPPEDWINDOW, FALSE);

    uint32 ActualWindowWidth = ClientRect.right - ClientRect.left + 1;
    uint32 ActualWindowHeight = ClientRect.bottom - ClientRect.top + 1;

    WindowHandle = CreateWindowEx(0,
                                  WindowClass.lpszClassName,
                                  "Feedback",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  (ScreenWidth - ActualWindowWidth) / 2,
                                  (ScreenHeight - ActualWindowHeight) / 2,
                                  ActualWindowWidth,
                                  ActualWindowHeight,
                                  NULL,
                                  NULL,
                                  hInstance,
                                  NULL);

    if(WindowHandle == NULL)
    {
        // TODO: logging
        Win32PrintError("CreateWindowEx");
        return 1;
    }

    DeviceContextHandle = GetDC(WindowHandle);
    if(DeviceContextHandle == NULL)
    {
        // TODO: logging
        Win32PrintError("GetDC");
        return 1;
    }

    int PixelFormatAttributeList[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        0
    };

    uint32 NumPixelFormats;
    wglChoosePixelFormatARB(DeviceContextHandle, PixelFormatAttributeList, 
                            NULL, 1, &PixelFormat, &NumPixelFormats);

    if(SetPixelFormat(DeviceContextHandle, PixelFormat, &PixelFormatDescriptor) == FALSE)
    {
        // TODO: logging
        Win32PrintError("SetPixelFormat");
        return 1;
    }

    HGLRC DummyGLContextHandle = GLContextHandle;

    int ContextAttributeList[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        0
    };

    GLContextHandle = wglCreateContextAttribsARB(DeviceContextHandle, NULL, ContextAttributeList);
    if(GLContextHandle == NULL)
    {
        // TODO: logging
        Win32PrintError("wglCreateContextAttribsARB");
        return 1;
    }

    if(wglDeleteContext(DummyGLContextHandle) == FALSE)
    {
        // TODO: logging
        Win32PrintError("wglDeleteContext");
        return 1;
    }

    if(wglMakeCurrent(DeviceContextHandle, GLContextHandle) == FALSE)
    {
        // TODO: logging
        Win32PrintError("wglMakeCurrent");
        return 1;
    }

    // platform/opengl initialization done

    uint32 FPS = 60;
    uint32 TimeBetweenFramesMillis = 1000 / FPS;

    uint64 AppMemorySize = Kilobytes(1) + Megabytes(1);
    void *AppMemory = calloc(AppMemorySize, 1);

    ImGuiContext *ImGuiContext = ImGui::CreateContext(malloc, free);

    AppDLL.Initialize(AppMemory, AppMemorySize, WindowWidth, WindowHeight, 
                      TimeBetweenFramesMillis, ImGuiContext, PlatformPrintf);

    const uint32 RenderTimerId = 0, RecheckDLLTimerId = 1;
    SetTimer(WindowHandle, RenderTimerId, TimeBetweenFramesMillis, NULL);
    SetTimer(WindowHandle, RecheckDLLTimerId, 500, NULL);

    uint64 StartCounter = Win32GetCounter();

    app_event Event;
    MSG Message;
    while(GetMessage(&Message, NULL, 0, 0))
    {
        TranslateMessage(&Message);

        switch(Message.message)
        {
            case WM_LBUTTONDOWN:
            {
                Event.Type = AppEventType_TouchDown;
                Event.TouchX = GET_X_LPARAM(Message.lParam);
                Event.TouchY = GET_Y_LPARAM(Message.lParam);

                AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
            } break;

            case WM_LBUTTONUP:
            {
                Event.Type = AppEventType_TouchUp;
                Event.TouchX = GET_X_LPARAM(Message.lParam);
                Event.TouchY = GET_Y_LPARAM(Message.lParam);

                AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
            } break;

            case WM_MOUSEMOVE:
            {
                Event.TouchX = GET_X_LPARAM(Message.lParam);
                Event.TouchY = GET_Y_LPARAM(Message.lParam);

                if(Message.wParam & MK_LBUTTON)
                {
                    Event.Type = AppEventType_TouchMovement;
                    AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
                }
                else
                {
                    Event.Type = AppEventType_NonTouchMovement;
                    AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
                }
            } break;

            case WM_MOUSEWHEEL:
            {
                int WheelDelta = GET_WHEEL_DELTA_WPARAM(Message.wParam);
                if(WheelDelta > 0)
                {
                    Event.Type = AppEventType_ZoomInPressed;
                    AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
                }
                else if(WheelDelta < 0)
                {
                    Event.Type = AppEventType_ZoomOutPressed;
                    AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
                }
            } break;

            case WM_KEYDOWN:
            {
            } break;

            case WM_KEYUP:
            {
            } break;

            case WM_TIMER:
            {
                switch(Message.wParam)
                {
                    case RecheckDLLTimerId:
                    {
                        FILETIME LatestAppCodeDLLWriteTime = Win32GetLastWriteTime(AppDLL.Filename);
                        if(CompareFileTime(&AppDLL.LastWriteTime, &LatestAppCodeDLLWriteTime) != 0)
                        {
                            if(GetFileAttributes("lock.tmp") == INVALID_FILE_ATTRIBUTES)
                            {
                                FreeLibrary(AppDLL.Handle);
                                LoadAppCode(&AppDLL);

                                Event.Type = AppEventType_CodeReload;
                                AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
                            }
                        }
                    } break;

                    case RenderTimerId:
                    {
                        Event.Type = AppEventType_UpdateAndRender;
                        AppDLL.HandleEvent(AppMemory, Win32GetSecondsElapsed(StartCounter), &Event);
                        SwapBuffers(DeviceContextHandle);
                    } break;
                }
            } break;

            default:
            {
                DispatchMessage(&Message);
            } break;
        }
    }

    return 0;
}
