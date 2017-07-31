#include <Windows.h>
#include <Windowsx.h>
#include <stdint.h>

#include <stdio.h>
#include <string.h>

#include "GL/glew.h"
#include "GL/wglew.h"

#include "imgui/imgui.h"

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
static HWND GlobalWindowHandle;

struct color
{
    float R, G, B, A;
};

struct time_point
{
    float Time, Y;
};

struct app_state
{
    uint32_t WindowWidth;
    uint32_t WindowHeight;

    color ClearColor;

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

/* IMGUI */
static GLuint       g_FontTexture = 0;
static int          g_ShaderHandle = 0, g_VertHandle = 0, g_FragHandle = 0;
static int          g_AttribLocationTex = 0, g_AttribLocationProjMtx = 0;
static int          g_AttribLocationPosition = 0, g_AttribLocationUV = 0, g_AttribLocationColor = 0;
static unsigned int g_VboHandle = 0, g_VaoHandle = 0, g_ElementsHandle = 0;

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
Win32Init(HINSTANCE hInstance, 
          uint32_t WindowWidth, uint32_t WindowHeight, 
          uint32_t ScreenWidth, uint32_t ScreenHeight)
{
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
    GlobalWindowHandle = WindowHandle;
}

inline static void
glClearColor(color Color)
{
    glClearColor(Color.R, Color.G, Color.B, Color.A);
}

static void
SetColor(color *Color, float R, float G, float B, float A)
{
    Color->R = R;
    Color->G = G;
    Color->B = B;
    Color->A = A;
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
RedrawScreen(app_state *AppState)
{
    float CameraTimePosCentered = AppState->CameraTimePos - (AppState->WindowWidth / 2 / AppState->PixelsPerSecond);
    glUniform1f(GetUniformLocation(AppState->ShaderProgram, "CameraTimePos"), CameraTimePosCentered);
    glUniform1f(GetUniformLocation(AppState->ShaderProgram, "PixelsPerSecond"), AppState->PixelsPerSecond);

    glClearColor(AppState->ClearColor);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(AppState->ShaderProgram);
    glBindVertexArray(AppState->Vao);
    glDrawArrays(GL_LINE_STRIP, 0, AppState->TimePointsInVbo + 1);

    ImGui::NewFrame();
    ImGui::ShowTestWindow();
    ImGui::Render();

    SwapBuffers(GlobalDeviceContextHandle);
}

static void
ModifyPixelsPerSecond(app_state *AppState, float Delta)
{
    AppState->PixelsPerSecond += Delta;

    if(AppState->PixelsPerSecond < 1)
    {
        AppState->PixelsPerSecond = 1;
    }

    Printf("%f\n", AppState->PixelsPerSecond);
}

static void
ModifyCameraTimePos(app_state *AppState, float Delta)
{
    AppState->CameraTimePos += Delta;
}

void ImGui_CreateDeviceObjects()
{
    // Backup GL state
    GLint last_texture, last_array_buffer, last_vertex_array;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

    const GLchar *vertex_shader =
        "#version 330\n"
        "uniform mat4 ProjMtx;\n"
        "in vec2 Position;\n"
        "in vec2 UV;\n"
        "in vec4 Color;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "	Frag_UV = UV;\n"
        "	Frag_Color = Color;\n"
        "	gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";

    const GLchar* fragment_shader =
        "#version 330\n"
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color;\n"
        "void main()\n"
        "{\n"
        "	Out_Color = Frag_Color * texture( Texture, Frag_UV.st);\n"
        "}\n";

    g_ShaderHandle = glCreateProgram();
    g_VertHandle = glCreateShader(GL_VERTEX_SHADER);
    g_FragHandle = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(g_VertHandle, 1, &vertex_shader, 0);
    glShaderSource(g_FragHandle, 1, &fragment_shader, 0);
    glCompileShader(g_VertHandle);
    glCompileShader(g_FragHandle);
    glAttachShader(g_ShaderHandle, g_VertHandle);
    glAttachShader(g_ShaderHandle, g_FragHandle);
    glLinkProgram(g_ShaderHandle);

    g_AttribLocationTex = glGetUniformLocation(g_ShaderHandle, "Texture");
    g_AttribLocationProjMtx = glGetUniformLocation(g_ShaderHandle, "ProjMtx");
    g_AttribLocationPosition = glGetAttribLocation(g_ShaderHandle, "Position");
    g_AttribLocationUV = glGetAttribLocation(g_ShaderHandle, "UV");
    g_AttribLocationColor = glGetAttribLocation(g_ShaderHandle, "Color");

    glGenBuffers(1, &g_VboHandle);
    glGenBuffers(1, &g_ElementsHandle);

    glGenVertexArrays(1, &g_VaoHandle);
    glBindVertexArray(g_VaoHandle);
    glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle);
    glEnableVertexAttribArray(g_AttribLocationPosition);
    glEnableVertexAttribArray(g_AttribLocationUV);
    glEnableVertexAttribArray(g_AttribLocationColor);

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    glVertexAttribPointer(g_AttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
    glVertexAttribPointer(g_AttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
    glVertexAttribPointer(g_AttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

    // Load font
    uint8_t *Pixels;
    int Width, Height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

    glGenTextures(1, &g_FontTexture);
    glBindTexture(GL_TEXTURE_2D, g_FontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);

    ImGui::GetIO().Fonts->TexID = (void *)(uint64_t)g_FontTexture;

    // Restore modified GL state
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindVertexArray(last_vertex_array);
}

static void
ImGui_RenderFunction(ImDrawData* draw_data)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    // Backup GL state
    GLint last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, &last_active_texture);
    glActiveTexture(GL_TEXTURE0);
    GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    GLint last_element_array_buffer; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
    GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
    GLint last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, &last_blend_src_rgb);
    GLint last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, &last_blend_dst_rgb);
    GLint last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, &last_blend_src_alpha);
    GLint last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, &last_blend_dst_alpha);
    GLint last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, &last_blend_equation_rgb);
    GLint last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &last_blend_equation_alpha);
    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
    GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);

    // Setup viewport, orthographic projection matrix
    glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
    const float ortho_projection[4][4] =
    {
        { 2.0f/io.DisplaySize.x, 0.0f,                   0.0f, 0.0f },
        { 0.0f,                  2.0f/-io.DisplaySize.y, 0.0f, 0.0f },
        { 0.0f,                  0.0f,                  -1.0f, 0.0f },
        {-1.0f,                  1.0f,                   0.0f, 1.0f },
    };
    glUseProgram(g_ShaderHandle);
    glUniform1i(g_AttribLocationTex, 0);
    glUniformMatrix4fv(g_AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
    glBindVertexArray(g_VaoHandle);

    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx* idx_buffer_offset = 0;

        glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ElementsHandle);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
                glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
                glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset);
            }
            idx_buffer_offset += pcmd->ElemCount;
        }
    }

    // Restore modified GL state
    glUseProgram(last_program);
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glActiveTexture(last_active_texture);
    glBindVertexArray(last_vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
    glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
    glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
    if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
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

    Win32Init(hInstance, WindowWidth, WindowHeight, ScreenWidth, ScreenHeight);

    /* Begin application code. */

    uint32_t FPS = 60;
    uint32_t RenderDelayMillis = 1000 / FPS;

    app_state AppState = {};
    AppState.WindowWidth = WindowWidth;
    AppState.WindowHeight = WindowHeight;
    AppState.TimePointsCapacity = Megabytes(1) / sizeof(time_point);
    AppState.TimePoints = (time_point *)malloc(AppState.TimePointsCapacity * sizeof(time_point));
    AppState.PixelsPerSecond = (float)WindowWidth / 5.0f;
    AppState.SingleFrameTime = (float)RenderDelayMillis / 1000.0f;
    SetColor(&AppState.ClearColor, 0.2f, 0.3f, 0.3f, 1.0f);

    int LastMousePosX = 0;
    bool IsTraceMode = false;
    float TimeWhenExitedTraceMode = 0;
    float BasePixelsPerSecondDelta = 5.0f;

    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

    glLineWidth(2.0f);

    // initialize IMGUI
    ImGuiIO& ImGuiIO = ImGui::GetIO();
    {
        ImGui_CreateDeviceObjects();

        ImGuiIO.DisplaySize.x = (float)AppState.WindowWidth;
        ImGuiIO.DisplaySize.y = (float)AppState.WindowHeight;
        ImGuiIO.RenderDrawListsFn = ImGui_RenderFunction;
    }

    SetTimer(GlobalWindowHandle, SCROLL_TIMER_ID, RenderDelayMillis, NULL);
    AppState.StartTime = Win32GetCurrentTime();

    MSG Message;
    while(GetMessage(&Message, NULL, 0, 0))
    {
        TranslateMessage(&Message);

        switch(Message.message)
        {
            case WM_MOUSEMOVE:
            case WM_LBUTTONUP:
            case WM_LBUTTONDOWN:
            {
                ImGuiIO.MousePos = ImVec2((float)GET_X_LPARAM(Message.lParam), (float)GET_Y_LPARAM(Message.lParam));
            } break;
        }

        switch(Message.message)
        {
            case WM_LBUTTONDOWN:
            {
                AppState.StartTime = Win32GetCurrentTime() - TimeWhenExitedTraceMode;

                IsTraceMode = true;

                HandleTraceModeMovement(&AppState, GET_Y_LPARAM(Message.lParam));

                ImGuiIO.MouseDown[0] = true;
            } break;

            case WM_LBUTTONUP:
            {
                TimeWhenExitedTraceMode = Win32GetElapsedTime(AppState.StartTime);

                IsTraceMode = false;

                ImGuiIO.MouseDown[0] = false;
            } break;

            case WM_MOUSEWHEEL:
            {
                int WheelDelta = GET_WHEEL_DELTA_WPARAM(Message.wParam);

                float PixelsPerSecondDelta = 0;
                if(WheelDelta > 0)
                {
                    PixelsPerSecondDelta = BasePixelsPerSecondDelta;
                }
                else if(WheelDelta < 0)
                {
                    PixelsPerSecondDelta = -BasePixelsPerSecondDelta;
                }

                ModifyPixelsPerSecond(&AppState, PixelsPerSecondDelta);
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
                        int MouseDeltaX = MousePosX - LastMousePosX;
                        float CameraTimePosDelta = -((float)MouseDeltaX / 100.0f);

                        ModifyCameraTimePos(&AppState, CameraTimePosDelta);
                    }
                }

                LastMousePosX = MousePosX;
            } break;

            case WM_KEYDOWN:
            {
                switch(Message.wParam)
                {
                    case VK_UP:
                    {
                        ModifyPixelsPerSecond(&AppState, 1);
                    } break;

                    case VK_DOWN:
                    {
                        ModifyPixelsPerSecond(&AppState, -1);
                    } break;

                    case VK_LEFT:
                    {
                        ModifyCameraTimePos(&AppState, -1);
                    } break;

                    case VK_RIGHT:
                    {
                        ModifyCameraTimePos(&AppState, 1);
                    } break;
                }
            } break;

            case WM_KEYUP:
            {
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
                }

                RedrawScreen(&AppState);
            } break;

            default:
            {
                DispatchMessage(&Message);
            } break;
        }
    }

    return 0;
}
