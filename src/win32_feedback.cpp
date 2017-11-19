#include <Windows.h>
#include <Windowsx.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "glew.h"
#include "wglew.h"

#include "feedback_platform.h"

struct app_dll
{
    HMODULE Handle;
    FILETIME LastWriteTime;

    char *Filename, *TempFilename;

    app_initialize *Initialize;
    app_update_and_render *UpdateAndRender;
};

global HANDLE LogFile;
global uint64_t GlobalPerfCountFrequency;
global char GeneralBuffer[512];
global bool32 GlobalRunning = true;

/* api to application */
//{
internal inline
PLATFORM_PRINTF(Printf)
{
    va_list VarArgs;
    va_start(VarArgs, Format);

    vsprintf(GeneralBuffer, Format, VarArgs);
    OutputDebugString(GeneralBuffer);

    va_end(VarArgs);
}

internal inline
PLATFORM_LOG(Log)
{
    va_list VarArgs;
    va_start(VarArgs, Format);

    vsprintf(GeneralBuffer, Format, VarArgs);

    OutputDebugString(GeneralBuffer);

    if(LogFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD BytesWritten;
    DWORD BytesToWrite = (uint32_t)strlen(GeneralBuffer);
    if(WriteFile(LogFile, GeneralBuffer, BytesToWrite, &BytesWritten, NULL) == FALSE ||
       BytesWritten != BytesToWrite)
    {
        Printf("failed to write to log file\n");
    }

    va_end(VarArgs);
}
//}

internal void
Win32LogError(char *PreMessageFormat, ...)
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
        Log("Error: Win32LogError()\n");
    }

    va_list VarArgs;
    va_start(VarArgs, PreMessageFormat);

    vsprintf(GeneralBuffer, PreMessageFormat, VarArgs);
    Log("%s: %s\n", GeneralBuffer, Buffer);

    va_end(VarArgs);
}

inline internal uint64_t
Win32GetCounter()
{
    LARGE_INTEGER LargeInteger;
    QueryPerformanceCounter(&LargeInteger);

    uint64_t Result = LargeInteger.QuadPart;
    return Result;
}

inline internal float
Win32GetSecondsElapsed(uint64_t EndCounter, uint64_t StartCounter)
{
    float Result = (float)(EndCounter - StartCounter) / (float)GlobalPerfCountFrequency;
    return Result;
}

inline internal float
Win32GetSecondsElapsed(uint64_t StartCounter)
{
    float Result = Win32GetSecondsElapsed(Win32GetCounter(), StartCounter);
    return Result;
}

internal bool32
Win32GetLastWriteTime(char *Filename, FILETIME *LastWriteTime)
{
    WIN32_FILE_ATTRIBUTE_DATA Data; 
    if(GetFileAttributesEx(Filename, GetFileExInfoStandard, &Data) == 0)
    {
        Log("GetFileAttributesEx() failed for file %s", Filename);
        Assert(0);
        return false;
    }

    *LastWriteTime = Data.ftLastWriteTime;

    return true;
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
            GlobalRunning = false;
        } break;

        default:
        {
            Result = DefWindowProc(hwnd, uMsg, wParam, lParam);
        } break;
    }

    return Result;
}

internal void *
Win32LoadFunction(HMODULE Module, char *FunctionName)
{
    void *FunctionAddress = (void *)GetProcAddress(Module, FunctionName);

    if(FunctionAddress == NULL)
    {
        Win32LogError("GetProcAddress() failed for function %s", FunctionName);
        Assert(0);
        return NULL;
    }

    return FunctionAddress;
}

internal bool32
LoadAppCode(app_dll *AppDLL)
{
    if(CopyFile(AppDLL->Filename, AppDLL->TempFilename, false) == 0)
    {
        Win32LogError("CopyFile() failed");
        Assert(0);
        return false;
    }

    if((AppDLL->Handle = LoadLibrary(AppDLL->TempFilename)) == NULL)
    {
        Win32LogError("LoadLibrary() failed");
        Assert(0);
        return false;
    }

    AppDLL->Initialize = (app_initialize *)Win32LoadFunction(AppDLL->Handle, "AppInitialize");
    AppDLL->UpdateAndRender = (app_update_and_render *)Win32LoadFunction(AppDLL->Handle, "AppUpdateAndRender");

    if(!(AppDLL->Initialize && AppDLL->UpdateAndRender))
    {
        return false;
    }

    if(!Win32GetLastWriteTime(AppDLL->Filename, &AppDLL->LastWriteTime))
    {
        return false;
    }

    return true;
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
    LogFile = CreateFile("log.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if(LogFile == INVALID_HANDLE_VALUE)
    {
        Printf("failed to create log file\n");
        Assert(0);
        return 1;
    }

    if(timeBeginPeriod(1) == TIMERR_NOCANDO)
    {
        Log("failed to set timer resolution\n");
        Assert(0);
        return 1;
    }

    app_dll AppDLL = {}; 
    AppDLL.Filename = "feedback.dll";
    AppDLL.TempFilename = "feedback_temp.dll";

    uint32_t WindowWidth = 540;
    uint32_t WindowHeight = 960;

    uint32_t ScreenWidth = 1920;
    uint32_t ScreenHeight = 1080;

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
        Win32LogError("RegisterClass() for dummy window failed");
        Assert(0);
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
        Win32LogError("CreateWindowEx() for dummy window failed");
        Assert(0);
        return 1;
    }

    HDC DeviceContextHandle = GetDC(WindowHandle);
    if(DeviceContextHandle == NULL)
    {
        Win32LogError("GetDC() for dummy window failed");
        Assert(0);
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
        Win32LogError("ChoosePixelFormat() for dummy window failed");
        Assert(0);
        return 1;
    }

    if(SetPixelFormat(DeviceContextHandle, PixelFormat, &PixelFormatDescriptor) == FALSE)
    {
        Win32LogError("SetPixelFormat() for dummy window failed");
        Assert(0);
        return 1;
    }

    HGLRC GLContextHandle = wglCreateContext(DeviceContextHandle);
    if(GLContextHandle == NULL)
    {
        Win32LogError("failed to create the dummy opengl context");
        Assert(0);
        return 1;
    }

    if(wglMakeCurrent(DeviceContextHandle, GLContextHandle) == FALSE)
    {
        Win32LogError("failed to make the dummy opengl context the current context");
        Assert(0);
        return 1;
    }

    GLenum GlewInitStatusCode = glewInit();
    if(GlewInitStatusCode != GLEW_OK)
    {
        Log("glewInit() failed\n", (char *)glewGetErrorString(GlewInitStatusCode));
        Assert(0);
        return 1;
    }

    if(!WGLEW_ARB_pixel_format)
    {
        Log("WGL_ARB_pixel_format not supported");
        Assert(0);
        return 1;
    }

    if(!WGLEW_ARB_create_context)
    {
        Log("WGL_ARB_create_context not supported");
        Assert(0);
        return 1;
    }

    DestroyWindow(WindowHandle);

    RECT ClientRect = {0, 0, (LONG)WindowWidth, (LONG)WindowHeight};
    AdjustWindowRect(&ClientRect, WS_OVERLAPPEDWINDOW, FALSE);

    uint32_t ActualWindowWidth = ClientRect.right - ClientRect.left + 1;
    uint32_t ActualWindowHeight = ClientRect.bottom - ClientRect.top + 1;

    WindowHandle = CreateWindowEx(0,
                                  WindowClass.lpszClassName,
                                  "Feedback",
                                  WS_OVERLAPPEDWINDOW/* | WS_VISIBLE*/,
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
        Win32LogError("failed to create window");
        Assert(0);
        return 1;
    }

    DeviceContextHandle = GetDC(WindowHandle);
    if(DeviceContextHandle == NULL)
    {
        Win32LogError("failed to get DC");
        Assert(0);
        return 1;
    }

    int PixelFormatAttributeList[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        //WGL_DEPTH_BITS_ARB, 8,
#if 0
        WGL_SAMPLE_BUFFERS_ARB, 1,
        WGL_SAMPLES_ARB, 4,
#endif
        0
    };

    uint32_t NumPixelFormats;
    wglChoosePixelFormatARB(DeviceContextHandle, PixelFormatAttributeList, 
                            NULL, 1, &PixelFormat, &NumPixelFormats);

    if(SetPixelFormat(DeviceContextHandle, PixelFormat, &PixelFormatDescriptor) == FALSE)
    {
        Win32LogError("SetPixelFormat() failed");
        Assert(0);
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
        Win32LogError("wglCreateContextAttribsARB() failed");
        Assert(0);
        return 1;
    }

    if(wglDeleteContext(DummyGLContextHandle) == FALSE)
    {
        Win32LogError("wglDeleteContext() failed");
        Assert(0);
        return 1;
    }

    if(wglMakeCurrent(DeviceContextHandle, GLContextHandle) == FALSE)
    {
        Win32LogError("wglMakeCurrent() failed");
        Assert(0);
        return 1;
    }

    // platform/opengl initialization done

    float FPS = 30;
    float TimeBetweenFrames = 1.0f / FPS;

    uint64_t AppMemorySize = Kilobytes(1) + Megabytes(3);
    void *AppMemory = calloc(AppMemorySize, 1);

    ImGuiContext *ImGuiContext = ImGui::CreateContext(malloc, free);

    uint64_t StartCounter = Win32GetCounter();

    MSG Message;
    app_input Input = {};
    bool32 CodeReload = false;
    bool32 IsFirstFrame = true;
    while(GlobalRunning)
    {
        uint64_t StartOfFrameCounter = Win32GetCounter();

        CodeReload = false;
        Input.MousePressed = false;
        Input.MouseReleased = false;
        Input.ZoomInPressed = false;
        Input.ZoomOutPressed = false;

        // check DLL
        {
            FILETIME LatestAppCodeDLLWriteTime;
            if(!Win32GetLastWriteTime(AppDLL.Filename, &LatestAppCodeDLLWriteTime))
            {
                return 1;
            }

            if(CompareFileTime(&AppDLL.LastWriteTime, &LatestAppCodeDLLWriteTime) != 0)
            {
                if(GetFileAttributes("lock.tmp") == INVALID_FILE_ATTRIBUTES)
                {
                    if(AppDLL.Handle != NULL && FreeLibrary(AppDLL.Handle) == 0)
                    {
                        Win32LogError("FreeLibrary() failed");
                        Assert(0);
                        return 1;
                    }

                    if(!LoadAppCode(&AppDLL))
                    {
                        return 1;
                    }

                    CodeReload = true;
                }
            }
        }

        while(PeekMessage(&Message, WindowHandle, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            switch(Message.message)
            {
                case WM_LBUTTONDOWN:
                {
                    Input.MousePressed = true;
                    Input.MouseDown = true;
                } break;

                case WM_LBUTTONUP:
                {
                    Input.MouseReleased = true;
                    Input.MouseDown = false;
                } break;

                case WM_MOUSEWHEEL:
                {
                    int WheelDelta = GET_WHEEL_DELTA_WPARAM(Message.wParam);
                    if(WheelDelta > 0)
                    {
                        Input.ZoomInPressed = true;
                    }
                    else if(WheelDelta < 0)
                    {
                        Input.ZoomOutPressed = true;
                    }
                } break;

                default:
                {
                    DispatchMessage(&Message);
                } break;
            }
        }

        // mouse pos
        {
            POINT CursorPos;
            GetCursorPos(&CursorPos);
            ScreenToClient(WindowHandle, &CursorPos);

            int32_t NewMouseX = CursorPos.x;
            int32_t NewMouseY = WindowHeight - CursorPos.y;

            Input.MouseDeltaX = NewMouseX - Input.MouseX;
            Input.MouseDeltaY = NewMouseY - Input.MouseY;

            Input.MouseX = NewMouseX;
            Input.MouseY = NewMouseY;
            Input.FlippedMouseY = CursorPos.y;
        }

        if(IsFirstFrame)
        {
            if(!AppDLL.Initialize(AppMemory, AppMemorySize, WindowWidth, WindowHeight, 
                                  TimeBetweenFrames, ImGuiContext, Printf, Log))
            {
                return 1;
            }
        }

        AppDLL.UpdateAndRender(AppMemory, CodeReload, Win32GetSecondsElapsed(StartCounter), &Input);
        if(SwapBuffers(DeviceContextHandle) == FALSE)
        {
            Win32LogError("SwapBuffers() failed");
            Assert(0);
        }

        if(IsFirstFrame)
        {
            ShowWindow(WindowHandle, SW_SHOW);
        }

        // frame rate sync
        {
            float FrameTime = Win32GetSecondsElapsed(StartOfFrameCounter);
            float TimeRemaining = TimeBetweenFrames - FrameTime;

            if(TimeRemaining > 0)
            {
                uint32_t TimeToSleepMillis = (uint32_t)((1000.0f * TimeRemaining) - 1);
                Sleep(TimeToSleepMillis);

                do
                {
                    FrameTime = Win32GetSecondsElapsed(StartOfFrameCounter);
                } while(FrameTime < TimeBetweenFrames);
            }
            else
            {
                Printf("Running behind!\n");
            }
        }

        IsFirstFrame = false;
    }

    if(timeEndPeriod(1) == TIMERR_NOCANDO)
    {
        Log("timeEndPeriod() failed??\n");
        Assert(0);
        return 1;
    }

    return 0;
}
