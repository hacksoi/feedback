#include <Windows.h>
#include <Windowsx.h>
#include <stdint.h>

#include <stdio.h>
#include <string.h>

#include "GL/glew.h"
#include "GL/wglew.h"

#define Kilobytes(NumberOfKbs) (NumberOfKbs * 1024)
#define Megabytes(NumberOfMbs) (NumberOfMbs * 1024 * 1024)
#define Gigabytes(NumberOfGbs) (NumberOfGbs * 1024 * 1024 * 1024)

#define Printf(FormatString, ...) \
    sprintf(GeneralBuffer, FormatString, __VA_ARGS__); \
    OutputDebugString(GeneralBuffer);

#define ArrayCount(Array) (sizeof(Array)/sizeof(Array[0]))
#define Assert(Expression) if(!(Expression)) { *((int *)0) = 0; }

#define SCROLL_TIMER_ID 0

static HDC GlobalDeviceContextHandle;

struct time_point
{
    float Time, Y;
};

struct app_state
{
    uint32_t WindowWidth;
    uint32_t WindowHeight;

    uint32_t ShaderProgram;
    uint32_t Vbo;
    uint32_t Vao;

    time_point *TimePoints;
    uint32_t TimePointsCapacity;
    uint32_t TimePointsUsed;
    uint32_t TimePointsInVbo;

    float PixelsPerSecond;
    float StartTime;
    float SingleFrameTime;
    float CameraTimePos;

    uint32_t NumFramesPassedSinceLastMouseMove;
};

static char *VertexShaderSource = R"STR(
#version 330 core
layout (location = 0) in vec2 Pos;

uniform vec2 WindowDimensions;
uniform float PixelsPerSecond;
uniform float CameraTimePos;

void
main()
{
    vec2 NewPos = vec2((Pos.x - CameraTimePos) * PixelsPerSecond, Pos.y);

    // convert to clip space
    NewPos.x = 2 * NewPos.x / WindowDimensions.x - 1;
    NewPos.y = 2 * NewPos.y / WindowDimensions.y - 1;

    gl_Position = vec4(NewPos, 0.0, 1.0);
}
)STR";

static char *FragmentShaderSource = R"STR(
#version 330 core
out vec4 FragColor;

void
main()
{
    FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);
}
)STR";

static char GeneralBuffer[512];

static void
ErrorExit(char *Message)
{
    Printf("%s\n", Message);
    ExitProcess(1);
}

static void
ErrorExit(char *FunctionName, char *Reason)
{
    Printf("Error in %s(): %s\n", FunctionName, Reason);
    ExitProcess(1);
}

static void
Win32PrintErrorAndExit(char *FunctionName)
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
        Printf("Error: Win32PrintErrorAndExit()\n");
        ExitProcess(1);
    }

    ErrorExit(FunctionName, Buffer);
}

static float
Win32GetCurrentTime()
{
    LARGE_INTEGER TimeLargeInteger;
    QueryPerformanceCounter(&TimeLargeInteger);

    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);

    float Time = (float)TimeLargeInteger.QuadPart / (float)Frequency.QuadPart;

    return Time;
}

static float
Win32GetElapsedTime(float StartTime)
{
    float ElapsedTime = Win32GetCurrentTime() - StartTime;
    return ElapsedTime;
}

typedef void check_function(GLuint object, GLenum name, GLint *params);
typedef void get_info_function(GLuint object, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
static void
CheckSuccess(check_function CheckFunction, get_info_function GetInfoFunction, 
             uint32_t Object, GLenum QueryName, char *ExitMessage)
{
    int Success;
    CheckFunction(Object, QueryName, &Success);
    if(!Success)
    {
        size_t ExitMessageLength = (size_t)strlen(ExitMessage);

        strcpy(GeneralBuffer, ExitMessage);
        GetInfoFunction(Object, (GLsizei)(sizeof(GeneralBuffer) - ExitMessageLength - 1), 
                        NULL, GeneralBuffer + ExitMessageLength);

        Printf("%s\n", GeneralBuffer);
        ExitProcess(1);
    }
}

static uint32_t
CreateShader(GLenum ShaderType, char *ShaderSource)
{
    uint32_t Shader = glCreateShader(ShaderType);
    glShaderSource(Shader, 1, &ShaderSource, NULL);
    glCompileShader(Shader);

    if(ShaderType == GL_VERTEX_SHADER)
    {
        CheckSuccess(glGetShaderiv, glGetShaderInfoLog, Shader, 
                     GL_COMPILE_STATUS, "Error compiling vertex shader: \n");
    }
    else/* if(ShaderType == GL_FRAGMENT_SHADER)*/
    {
        CheckSuccess(glGetShaderiv, glGetShaderInfoLog, Shader, 
                     GL_COMPILE_STATUS, "Error compiling fragment shader: \n");
    }

    return Shader;
}

static int32_t
GetUniformLocation(uint32_t ShaderProgram, char *UniformName)
{
    int32_t UniformLocation = glGetUniformLocation(ShaderProgram, UniformName);

    if(UniformLocation == -1)
    {
        Printf("Error: could not find uniform '%s'.\n", UniformName);
        ExitProcess(1);
    }

    return UniformLocation;
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

static void
PushTimePoint(app_state *AppState, float Time, float Y)
{
    if(AppState->TimePointsUsed < AppState->TimePointsCapacity)
    {
        time_point *NewTimePoint = &AppState->TimePoints[AppState->TimePointsUsed++];

        NewTimePoint->Time = Time;
        NewTimePoint->Y = Y;
    }
}

static void
PushTimePoint(app_state *AppState, float Y)
{
    PushTimePoint(AppState, Win32GetElapsedTime(AppState->StartTime), Y);
}

static void
PushLastTimePoint(app_state *AppState, float Time)
{
    if(AppState->TimePointsUsed > 0)
    {
        PushTimePoint(AppState, Time, AppState->TimePoints[AppState->TimePointsUsed-1].Y);
    }
}

static void
PopTimePoint(app_state *AppState)
{
    AppState->TimePointsUsed--;
}

static void
HandleTraceModeMovement(app_state *AppState, int FlippedY)
{
    if(AppState->NumFramesPassedSinceLastMouseMove > 1)
    {
        PushLastTimePoint(AppState, Win32GetElapsedTime(AppState->StartTime) - AppState->SingleFrameTime);
    }

    PushTimePoint(AppState, (float)(AppState->WindowHeight - FlippedY));

    AppState->NumFramesPassedSinceLastMouseMove = 0;
}

static void
DrawTimePoints(app_state *AppState)
{
    float CameraTimePosCentered = AppState->CameraTimePos - (AppState->WindowWidth / 2 / AppState->PixelsPerSecond);
    glUniform1f(GetUniformLocation(AppState->ShaderProgram, "CameraTimePos"), CameraTimePosCentered);

    glUniform1f(GetUniformLocation(AppState->ShaderProgram, "PixelsPerSecond"), AppState->PixelsPerSecond);

    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(AppState->ShaderProgram);
    glBindVertexArray(AppState->Vao);
    glDrawArrays(GL_LINE_STRIP, 0, AppState->TimePointsInVbo + 1);

    SwapBuffers(GlobalDeviceContextHandle);
}

int CALLBACK
WinMain(HINSTANCE hInstance,
        HINSTANCE hPrevInstance,
        LPSTR lpCmdLine,
        int nCmdShow)
{
    uint32_t WindowWidth = 540;
    uint32_t WindowHeight = 960;

    uint32_t ScreenWidth = 1920;
    uint32_t ScreenHeight = 1080;

    WNDCLASS WindowClass = {};
    WindowClass.style = CS_OWNDC;
    WindowClass.lpfnWndProc = WindowProc;
    WindowClass.hInstance = hInstance;
    WindowClass.lpszClassName = "Window Class";
    WindowClass.hCursor = LoadCursor(0, IDC_ARROW);

    if(!RegisterClass(&WindowClass))
    {
        Win32PrintErrorAndExit("RegisterClass");
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
        Win32PrintErrorAndExit("Dummy CreateWindowEx");
    }

    HDC DeviceContextHandle = GetDC(WindowHandle);
    if(DeviceContextHandle == NULL)
    {
        Win32PrintErrorAndExit("Dummy GetDC");
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
        Win32PrintErrorAndExit("ChoosePixelFormat");
    }

    if(SetPixelFormat(DeviceContextHandle, PixelFormat, &PixelFormatDescriptor) == FALSE)
    {
        Win32PrintErrorAndExit("SetPixelFormat");
    }

    HGLRC GLContextHandle = wglCreateContext(DeviceContextHandle);
    if(GLContextHandle == NULL)
    {
        Win32PrintErrorAndExit("wglCreateContext");
    }

    if(wglMakeCurrent(DeviceContextHandle, GLContextHandle) == FALSE)
    {
        Win32PrintErrorAndExit("wglMakeCurrent");
    }

    GLenum GlewInitStatusCode = glewInit();
    if(GlewInitStatusCode != GLEW_OK)
    {
        ErrorExit("glewInit", (char *)glewGetErrorString(GlewInitStatusCode));
    }

    if(!WGLEW_ARB_pixel_format)
    {
        ErrorExit("WGL_ARB_pixel_format not supported");
    }

    if(!WGLEW_ARB_multisample)
    {
        ErrorExit("WGL_ARB_multisample not supported");
    }

    if(!WGLEW_ARB_create_context)
    {
        ErrorExit("WGL_ARB_create_context not supported");
    }

    DestroyWindow(WindowHandle);

    RECT ClientRect = {0, 0, (LONG)WindowWidth, (LONG)WindowHeight};
    AdjustWindowRect(&ClientRect, WS_OVERLAPPEDWINDOW, FALSE);

    uint32_t ActualWindowWidth = ClientRect.right - ClientRect.left + 1;
    uint32_t ActualWindowHeight = ClientRect.bottom - ClientRect.top + 1;

    WindowHandle = CreateWindowEx(0,
                                  WindowClass.lpszClassName,
                                  "Window Text",
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
        Win32PrintErrorAndExit("CreateWindowEx");
    }

    DeviceContextHandle = GetDC(WindowHandle);
    if(DeviceContextHandle == NULL)
    {
        Win32PrintErrorAndExit("GetDC");
    }

    int PixelFormatAttributeList[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        0
    };

    uint32_t NumPixelFormats;
    wglChoosePixelFormatARB(DeviceContextHandle, PixelFormatAttributeList, 
                            NULL, 1, &PixelFormat, &NumPixelFormats);

    if(SetPixelFormat(DeviceContextHandle, PixelFormat, &PixelFormatDescriptor) == FALSE)
    {
        Win32PrintErrorAndExit("SetPixelFormat");
    }

    HGLRC DummyGLContextHandle = GLContextHandle;

    int ContextAttributeList[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        0
    };

    GLContextHandle = wglCreateContextAttribsARB(DeviceContextHandle, NULL, ContextAttributeList);

    wglDeleteContext(DummyGLContextHandle);

    if(wglMakeCurrent(DeviceContextHandle, GLContextHandle) == FALSE)
    {
        Win32PrintErrorAndExit("wglMakeCurrent");
    }

    GlobalDeviceContextHandle = DeviceContextHandle;

    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Begin application code. */

    app_state AppState = {};
    AppState.WindowWidth = WindowWidth;
    AppState.WindowHeight = WindowHeight;

    AppState.TimePointsCapacity = Megabytes(1) / sizeof(time_point);
    AppState.TimePoints = (time_point *)malloc(AppState.TimePointsCapacity * sizeof(time_point));

    AppState.PixelsPerSecond = (float)WindowWidth / 5.0f;
    float PixelsPerSecondDelta = 5.0f;

    glViewport(0, 0, WindowWidth, WindowHeight);

    uint32_t VertexShader = CreateShader(GL_VERTEX_SHADER, VertexShaderSource);
    uint32_t FragmentShader = CreateShader(GL_FRAGMENT_SHADER, FragmentShaderSource);

    AppState.ShaderProgram = glCreateProgram();
    glAttachShader(AppState.ShaderProgram, VertexShader);
    glAttachShader(AppState.ShaderProgram, FragmentShader);
    glLinkProgram(AppState.ShaderProgram);
    CheckSuccess(glGetProgramiv, glGetProgramInfoLog, AppState.ShaderProgram, 
                 GL_LINK_STATUS, "Error linking shader program: \n");

    glDeleteShader(VertexShader);
    glDeleteShader(FragmentShader);

    glUseProgram(AppState.ShaderProgram);
    glUniform2f(GetUniformLocation(AppState.ShaderProgram, "WindowDimensions"), (float)WindowWidth, (float)WindowHeight);

    glGenBuffers(1, &AppState.Vbo);
    glBindBuffer(GL_ARRAY_BUFFER, AppState.Vbo);
    glBufferData(GL_ARRAY_BUFFER, AppState.TimePointsCapacity * sizeof(time_point), NULL, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &AppState.Vao);
    glBindVertexArray(AppState.Vao);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(0);

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    SwapBuffers(GlobalDeviceContextHandle);

    glLineWidth(2.0f);

    uint32_t RenderDelayMillis = 16;
    SetTimer(WindowHandle, SCROLL_TIMER_ID, RenderDelayMillis, NULL);

    float SingleFrameTime = (float)RenderDelayMillis / 1000.0f;

    bool IsTraceMode = false;
    float TimeWhenExitedTraceMode = 0;

    int LastMousePosX = 0;

    AppState.StartTime = Win32GetCurrentTime();

    MSG Message;
    while(GetMessage(&Message, NULL, 0, 0))
    {
        TranslateMessage(&Message);

        switch(Message.message)
        {
            case WM_LBUTTONDOWN:
            {
                AppState.StartTime = Win32GetCurrentTime() - TimeWhenExitedTraceMode;

                IsTraceMode = true;

                if(IsTraceMode)
                {
                    HandleTraceModeMovement(&AppState, GET_Y_LPARAM(Message.lParam));
                }
            } break;

            case WM_LBUTTONUP:
            {
                TimeWhenExitedTraceMode = Win32GetElapsedTime(AppState.StartTime);

                IsTraceMode = false;
            } break;

            case WM_MOUSEWHEEL:
            {
                int WheelDelta = GET_WHEEL_DELTA_WPARAM(Message.wParam);

                if(WheelDelta > 0)
                {
                    AppState.PixelsPerSecond += PixelsPerSecondDelta;
                }
                else if(WheelDelta < 0)
                {
                    AppState.PixelsPerSecond -= PixelsPerSecondDelta;
                }

                if(AppState.PixelsPerSecond < 1)
                {
                    AppState.PixelsPerSecond = 1;
                }

                Printf("%f\n", AppState.PixelsPerSecond);

                DrawTimePoints(&AppState);
            } break;

            case WM_MOUSEMOVE:
            {
                int MousePosX = GET_X_LPARAM(Message.lParam);

                if(IsTraceMode)
                {
                    HandleTraceModeMovement(&AppState, GET_Y_LPARAM(Message.lParam));
                }
                else//if(!IsTraceMode)
                {
                    if(Message.wParam & MK_RBUTTON)
                    {
                        // move the camera position

                        int MouseDeltaX = MousePosX - LastMousePosX;
                        AppState.CameraTimePos -= ((float)MouseDeltaX / 100.0f);

                        DrawTimePoints(&AppState);
                    }
                }

                LastMousePosX = MousePosX;
            } break;

            case WM_TIMER:
            {
                if(IsTraceMode && 
                   Message.wParam == SCROLL_TIMER_ID)
                {
                    if(AppState.TimePointsUsed > 0)
                    {
                        // add dummy time point
                        PushTimePoint(&AppState, Win32GetElapsedTime(AppState.StartTime), AppState.TimePoints[AppState.TimePointsUsed - 1].Y);

                        uint32_t CopyOffset = AppState.TimePointsInVbo * sizeof(time_point);
                        uint32_t NumBytesToCopy = (AppState.TimePointsUsed - AppState.TimePointsInVbo + 1) * sizeof(time_point);

                        glBindBuffer(GL_ARRAY_BUFFER, AppState.Vbo);
                        glBufferSubData(GL_ARRAY_BUFFER, CopyOffset, NumBytesToCopy, &AppState.TimePoints[AppState.TimePointsInVbo]);

                        // remove dummy time point
                        PopTimePoint(&AppState);

                        AppState.TimePointsInVbo = AppState.TimePointsUsed;
                    }

                    AppState.NumFramesPassedSinceLastMouseMove++;

                    AppState.CameraTimePos = Win32GetElapsedTime(AppState.StartTime);

                    DrawTimePoints(&AppState);
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
