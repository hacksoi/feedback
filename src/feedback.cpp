/*
    TODO:
<<<<<<< HEAD
=======
>>>>>>> 674ca1d8f33cceb57472af0d09dacb0739b138db
        -put imgui window at the bottom of the screen and decrease the opengl viewport height
        -have menu options at bottom and the CameraTimePos at the top-middle
        -compress time points to allow at least an hour of playback
*/

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "GL/glew.h"

#include "feedback_platform.h"

/* enums */
//{
enum app_mode
{
    AppMode_PRERECORD,
    AppMode_RECORD,
    AppMode_RECENT_PLAYBACK,
    AppMode_RANDOM_PLAYBACK
};
//}

/* structs */
//{
union v4
{
    real32 Values[4];

    struct
    {
        real32 X, Y, Z, W;
    };

    struct
    {
        real32 R, G, B, A;
    };
};

union v2
{
    real32 Values[2];

    struct
    {
        union
        {
            real32 X;
            real32 Time;
        };

        real32 Y;
    };
};

struct v2i
{
    int X, Y;
};

struct rectangle
{
    v2 Min, Max;
};

// TODO: rename this to app_context
struct app_state
{
    uint32 WindowWidth, WindowHeight;

    app_mode Mode;

    uint32 ShaderProgram;
    uint32 Vbo;
    uint32 Vao;

    v2 *TimePoints;
    uint32 TimePointsCapacity;
    uint32 TimePointsUsed;
    uint32 TimePointsInVbo;

    real32 DefaultPixelsPerSecond;
    real32 PixelsPerSecond, SecondsPerPixel;
    real32 PixelsPerSecondFactor;

    uint64 StartCounter;

    real32 CameraTimePos;
    real32 CameraTimePosDeltaPixels;

    int32 LastTouchX;
    real32 SingleFrameTime;
    uint64 CounterElapsedWhenExitedRecordMode;

    v4 ClearColor, LineColor, PointColor;

    // platform
    platform_get_counter *PlatformGetCounter;
    platform_get_seconds_elapsed *PlatformGetSecondsElapsed;
    platform_debug_printf *Printf;

    // debug
    bool32 ShouldDrawPoints;

    // imgui
    ImGuiContext *ImGuiContext;
    rectangle ImGuiWindowRect;

    GLuint       g_FontTexture;
    int          g_ShaderHandle, g_VertHandle, g_FragHandle;
    int          g_AttribLocationTex, g_AttribLocationProjMtx;
    int          g_AttribLocationPosition, g_AttribLocationUV, g_AttribLocationColor;
    unsigned int g_VboHandle, g_VaoHandle, g_ElementsHandle;
};
//}

/* shaders */
//{
global char *VertexShaderSource = R"STR(
#version 330 core
layout (location = 0) in vec2 Pos;

uniform vec2 WindowDimensions;
uniform float PixelsPerSecond;
uniform float CameraTimePos;

void
main()
{
    vec2 NewPos = vec2((Pos.x - CameraTimePos) * PixelsPerSecond, Pos.y);

    // map to [-1, 1] (clip space)
    NewPos.x = ((2.0f * NewPos.x) / WindowDimensions.x) - 1.0f;
    NewPos.y = ((2.0f * NewPos.y) / WindowDimensions.y) - 1.0f;

    gl_Position = vec4(NewPos, 0.0, 1.0);
}
)STR";

global char *FragmentShaderSource = R"STR(
#version 330 core
out vec4 FragColor;

uniform vec4 ShapeColor;

void
main()
{
    FragColor = ShapeColor;
}
)STR";
//}

/* global variables */
//{
global char GeneralBuffer[512];
//}

/* v2 functions */
//{
inline internal v2
V2(real32 X, real32 Y)
{
    v2 Result = {X, Y};
    return Result;
}
//}

/* v2i functions */
//{
inline internal v2i
V2I(int32 X, int32 Y)
{
    v2i Result = {X, Y};
    return Result;
}
//}

/* v4 functions */
//{
inline internal v4
V4(real32 X, real32 Y, real32 Z, real32 W)
{
    v4 Result = {X, Y, Z, W};
    return Result;
}

inline internal void
SetColor(v4 *Color, real32 R, real32 G, real32 B, real32 A)
{
    Color->R = R;
    Color->G = G;
    Color->B = B;
    Color->A = A;
}
//}

/* rectangle functions */
//{
inline internal void
RectangleResize(rectangle *Rectangle, v2 NewSize)
{
    Rectangle->Max.X = Rectangle->Min.X + NewSize.X - 1;
    Rectangle->Max.Y = Rectangle->Min.Y + NewSize.Y - 1;
}

inline internal void
RectanglePosSize(rectangle *Rectangle, v2 Pos, v2 Size)
{
    Rectangle->Min.X = Pos.X;
    Rectangle->Min.Y = Pos.Y;

    Rectangle->Max.X = Rectangle->Min.X + Size.X - 1;
    Rectangle->Max.Y = Rectangle->Min.Y + Size.Y - 1;
}

inline internal bool32
CheckInsideRectangle(v2 Point, rectangle Rectangle)
{
    bool32 Result = (Point.X >= Rectangle.Min.X && Point.X <= Rectangle.Max.X &&
                   Point.Y >= Rectangle.Min.Y && Point.Y <= Rectangle.Max.Y);
    return Result;
}

inline internal bool32
CheckInsideRectangle(v2i Point, rectangle Rectangle)
{
    bool32 Result = CheckInsideRectangle(V2((real32)Point.X, (real32)Point.Y), Rectangle);
    return Result;
}
//}

/* IMGUI */
//{ 
internal void
ImGui_CreateDeviceObjects(app_state *AppState)
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

    AppState->g_ShaderHandle = glCreateProgram();
    AppState->g_VertHandle = glCreateShader(GL_VERTEX_SHADER);
    AppState->g_FragHandle = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(AppState->g_VertHandle, 1, &vertex_shader, 0);
    glShaderSource(AppState->g_FragHandle, 1, &fragment_shader, 0);
    glCompileShader(AppState->g_VertHandle);
    glCompileShader(AppState->g_FragHandle);
    glAttachShader(AppState->g_ShaderHandle, AppState->g_VertHandle);
    glAttachShader(AppState->g_ShaderHandle, AppState->g_FragHandle);
    glLinkProgram(AppState->g_ShaderHandle);

    AppState->g_AttribLocationTex = glGetUniformLocation(AppState->g_ShaderHandle, "Texture");
    AppState->g_AttribLocationProjMtx = glGetUniformLocation(AppState->g_ShaderHandle, "ProjMtx");
    AppState->g_AttribLocationPosition = glGetAttribLocation(AppState->g_ShaderHandle, "Position");
    AppState->g_AttribLocationUV = glGetAttribLocation(AppState->g_ShaderHandle, "UV");
    AppState->g_AttribLocationColor = glGetAttribLocation(AppState->g_ShaderHandle, "Color");

    glGenBuffers(1, &AppState->g_VboHandle);
    glGenBuffers(1, &AppState->g_ElementsHandle);

    glGenVertexArrays(1, &AppState->g_VaoHandle);
    glBindVertexArray(AppState->g_VaoHandle);
    glBindBuffer(GL_ARRAY_BUFFER, AppState->g_VboHandle);
    glEnableVertexAttribArray(AppState->g_AttribLocationPosition);
    glEnableVertexAttribArray(AppState->g_AttribLocationUV);
    glEnableVertexAttribArray(AppState->g_AttribLocationColor);

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    glVertexAttribPointer(AppState->g_AttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
    glVertexAttribPointer(AppState->g_AttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
    glVertexAttribPointer(AppState->g_AttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

    // Load font
    uint8_t *Pixels;
    int Width, Height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

    glGenTextures(1, &AppState->g_FontTexture);
    glBindTexture(GL_TEXTURE_2D, AppState->g_FontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);

    ImGui::GetIO().Fonts->TexID = (void *)(uint64_t)AppState->g_FontTexture;

    // Restore modified GL state
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindVertexArray(last_vertex_array);
}

internal void
ImGui_RenderFunction(app_state *AppState, ImDrawData* draw_data)
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
    glUseProgram(AppState->g_ShaderHandle);
    glUniform1i(AppState->g_AttribLocationTex, 0);
    glUniformMatrix4fv(AppState->g_AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
    glBindVertexArray(AppState->g_VaoHandle);

    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx* idx_buffer_offset = 0;

        glBindBuffer(GL_ARRAY_BUFFER, AppState->g_VboHandle);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, AppState->g_ElementsHandle);
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
//}

/* opengl */
//{
struct create_shader_result
{
    uint32 Shader;
    bool32 DidCompileSuccessfully;
};
internal create_shader_result
CreateShader(GLenum ShaderType, char *ShaderSource)
{
    create_shader_result Result = {};

    Result.Shader = glCreateShader(ShaderType);
    glShaderSource(Result.Shader, 1, &ShaderSource, NULL);
    glCompileShader(Result.Shader);
    glGetShaderiv(Result.Shader, GL_COMPILE_STATUS, &Result.DidCompileSuccessfully);

    return Result;
}

typedef void opengl_get_error_info(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
internal void
PrintOpenGLError(app_state *AppState, uint32 OpenGLObject, opengl_get_error_info OpenGLGetErrorInfo, char *ExitMessage)
{
    size_t ExitMessageLength = strlen(ExitMessage);

    strcpy(GeneralBuffer, ExitMessage);
    OpenGLGetErrorInfo(OpenGLObject, (GLsizei)(sizeof(GeneralBuffer) - ExitMessageLength - 1), 
                       NULL, GeneralBuffer + ExitMessageLength);

    // TODO: logging
    AppState->Printf("%s\n", GeneralBuffer);
}

inline internal int32
GetUniformLocation(app_state *AppState, uint32 ShaderProgram, char *UniformName)
{
    int32 UniformLocation = glGetUniformLocation(ShaderProgram, UniformName);

    if(UniformLocation == -1)
    {
        // TODO: logging
        AppState->Printf("Error: could not find uniform '%s'.\n", UniformName);
        Assert(0);
    }

    return UniformLocation;
}
//}

/* application */
//{
inline internal void
PushTimePoint(app_state *AppState, real32 Time, real32 Y)
{
    if(AppState->TimePointsUsed < AppState->TimePointsCapacity)
    {
        v2 *NewTimePoint = &AppState->TimePoints[AppState->TimePointsUsed++];

        NewTimePoint->Time = Time;
        NewTimePoint->Y = Y;
    }
}

inline internal void
PushLastTimePoint(app_state *AppState, real32 Time)
{
    PushTimePoint(AppState, Time, AppState->TimePoints[AppState->TimePointsUsed - 1].Y);
}

inline internal void
PopTimePoint(app_state *AppState)
{
    AppState->TimePointsUsed--;
}

internal void
HandleRecordModeMovement(app_state *AppState, int FlippedY)
{
    if(AppState->TimePointsUsed > 0)
    {
        real32 TimeSinceLastTimePointAdd = (AppState->PlatformGetSecondsElapsed(AppState->StartCounter) - 
                                            AppState->TimePoints[AppState->TimePointsUsed - 1].Time);
        if(TimeSinceLastTimePointAdd >= AppState->SingleFrameTime)
        {
            PushLastTimePoint(AppState, AppState->PlatformGetSecondsElapsed(AppState->StartCounter) - AppState->SingleFrameTime);
        }
    }

    PushTimePoint(AppState, AppState->PlatformGetSecondsElapsed(AppState->StartCounter), 
                  (real32)(AppState->WindowHeight - FlippedY));
}

inline internal void
_SetPixelsPerSecond(app_state *AppState, real32 NewPixelsPerSecond)
{
    AppState->PixelsPerSecond = NewPixelsPerSecond;
    if(AppState->PixelsPerSecond < 1)
    {
        AppState->PixelsPerSecond = 1;
    }
    AppState->SecondsPerPixel = 1.0f / AppState->PixelsPerSecond;
}

inline internal void
IncreasePixelsPerSecond(app_state *AppState)
{
    real32 NewPixelsPerSecond = AppState->PixelsPerSecond * AppState->PixelsPerSecondFactor;
    _SetPixelsPerSecond(AppState, NewPixelsPerSecond);
}

inline internal void
DecreasePixelsPerSecond(app_state *AppState)
{
    real32 NewPixelsPerSecond = AppState->PixelsPerSecond / AppState->PixelsPerSecondFactor;
    _SetPixelsPerSecond(AppState, NewPixelsPerSecond);
}

inline internal void
_SetCameraTimePos(app_state *AppState, real32 NewCameraTimePos)
{
    AppState->CameraTimePos = NewCameraTimePos;
}

inline internal void
ChangeCameraTimePos(app_state *AppState, real32 CameraTimePosDeltaPixels)
{
    _SetCameraTimePos(AppState, AppState->CameraTimePos + (AppState->SecondsPerPixel * CameraTimePosDeltaPixels));
}

inline internal void
IncreaseCameraTimePos(app_state *AppState)
{
    real32 CameraTimePosDeltaSeconds = AppState->SecondsPerPixel * AppState->CameraTimePosDeltaPixels;
    _SetCameraTimePos(AppState, AppState->CameraTimePos + CameraTimePosDeltaSeconds);
}

inline internal void
DecreaseCameraTimePos(app_state *AppState)
{
    real32 CameraTimePosDeltaSeconds = AppState->SecondsPerPixel * AppState->CameraTimePosDeltaPixels;
    _SetCameraTimePos(AppState, AppState->CameraTimePos - CameraTimePosDeltaSeconds);
}

inline void
Load(app_state *AppState)
{
    FILE *File = fopen("feedback_output.fbk", "rb");

    if(File == NULL)
    {
        // TODO: logging
        Assert(0);
        return;
    }

    fread((void *)AppState->TimePoints, sizeof(v2), AppState->TimePointsCapacity, File);
    fread((void *)&AppState->TimePointsUsed, sizeof(uint32), 1, File);
    fclose(File);

    // ensure the newly loaded time points will be uploaded to the vertex buffer
    AppState->TimePointsInVbo = 0;

    AppState->CameraTimePos = 0.0f;
    AppState->PixelsPerSecond = AppState->DefaultPixelsPerSecond;
}

internal char
KeyToChar(app_key Key)
{
    char Result = (char)Key + 'A';
    return Result;
}
//}

/* api to platform */
//{
APP_CODE_INITIALIZE(AppInitialize)
{
    uint8 *CurrentMemory = (uint8 *)Memory;

    app_state *AppState = (app_state *)CurrentMemory;
    CurrentMemory += sizeof(app_state);

    AppState->TimePoints = (v2 *)CurrentMemory;
    AppState->TimePointsCapacity = Megabytes(1) / sizeof(v2);
    CurrentMemory += AppState->TimePointsCapacity * sizeof(v2);

    AppState->WindowWidth = WindowWidth;
    AppState->WindowHeight = WindowHeight;
    AppState->DefaultPixelsPerSecond = (real32)WindowWidth / 5.0f;
    AppState->PixelsPerSecond = AppState->DefaultPixelsPerSecond;
    AppState->SecondsPerPixel = 1.0f / AppState->PixelsPerSecond;
    AppState->PixelsPerSecondFactor = 1.1f;
    AppState->SingleFrameTime = (real32)TimeBetweenFramesMillis / 1000.0f;
    AppState->CameraTimePosDeltaPixels = 15.0f;
    AppState->ShouldDrawPoints = false;
    AppState->Mode = AppMode_PRERECORD;
    AppState->LastTouchX = 0;

    AppState->PlatformGetCounter = PlatformGetCounter;
    AppState->PlatformGetSecondsElapsed = PlatformGetSecondsElapsed;
    AppState->Printf = PlatformDebugPrintf;

    SetColor(&AppState->ClearColor, 0.2f, 0.3f, 0.3f, 1.0f);
    SetColor(&AppState->LineColor, 1.0f, 0.5f, 0.2f, 1.0f);
    SetColor(&AppState->PointColor, 1.0f, 1.0f, 1.0f, 1.0f);

    int32 LastMousePosX = 0;
    uint64 TimeElapsedWhenExitedRecordMode = 0;

    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // TODO: shrink the height to accomodate options window at the bottom
    glViewport(0, 0, WindowWidth, WindowHeight);

    create_shader_result CreateVertexShaderResult = CreateShader(GL_VERTEX_SHADER, VertexShaderSource);
    if(!CreateVertexShaderResult.DidCompileSuccessfully)
    {
        PrintOpenGLError(AppState, CreateVertexShaderResult.Shader, 
                         glGetShaderInfoLog, "Error compiling vertex shader: \n");
        return false;
    }

    create_shader_result CreateFragmentShaderResult = CreateShader(GL_FRAGMENT_SHADER, FragmentShaderSource);
    if(!CreateFragmentShaderResult.DidCompileSuccessfully)
    {
        PrintOpenGLError(AppState, CreateFragmentShaderResult.Shader, 
                         glGetShaderInfoLog, "Error compiling fragment shader: \n");
        return false;
    }

    AppState->ShaderProgram = glCreateProgram();
    glAttachShader(AppState->ShaderProgram, CreateVertexShaderResult.Shader);
    glAttachShader(AppState->ShaderProgram, CreateFragmentShaderResult.Shader);
    glLinkProgram(AppState->ShaderProgram);

    bool32 DidProgramLinkSuccessfully;
    glGetProgramiv(AppState->ShaderProgram, GL_LINK_STATUS, &DidProgramLinkSuccessfully);
    if(!DidProgramLinkSuccessfully)
    {
        PrintOpenGLError(AppState, AppState->ShaderProgram, glGetProgramInfoLog, "Error linking shader program: \n");
        return false;
    }

    glDeleteShader(CreateVertexShaderResult.Shader);
    glDeleteShader(CreateFragmentShaderResult.Shader);

    glUseProgram(AppState->ShaderProgram);
    glUniform2f(GetUniformLocation(AppState, AppState->ShaderProgram, "WindowDimensions"), (real32)WindowWidth, (real32)WindowHeight);

    glGenBuffers(1, &AppState->Vbo);
    glBindBuffer(GL_ARRAY_BUFFER, AppState->Vbo);
    glBufferData(GL_ARRAY_BUFFER, AppState->TimePointsCapacity * sizeof(v2), NULL, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &AppState->Vao);
    glBindVertexArray(AppState->Vao);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(0);

    glLineWidth(3.0f);
    glPointSize(3.0f);

    // initialize imgui
    AppState->ImGuiContext = ImGuiContext;
    ImGui::SetCurrentContext(AppState->ImGuiContext);

    ImGui_CreateDeviceObjects(AppState);

    ImGuiIO& ImGuiIO = ImGui::GetIO();
    ImGuiIO.DisplaySize.x = (real32)AppState->WindowWidth;
    ImGuiIO.DisplaySize.y = (real32)AppState->WindowHeight;
    ImGuiIO.RenderDrawListsFn = NULL;

    ImGuiIO.KeyMap[ImGuiKey_Tab] = AppKey_TAB;
    ImGuiIO.KeyMap[ImGuiKey_LeftArrow] = AppKey_LEFT;
    ImGuiIO.KeyMap[ImGuiKey_RightArrow] = AppKey_RIGHT;
    ImGuiIO.KeyMap[ImGuiKey_UpArrow] = AppKey_UP;
    ImGuiIO.KeyMap[ImGuiKey_DownArrow] = AppKey_DOWN;
    ImGuiIO.KeyMap[ImGuiKey_PageUp] = AppKey_PAGEUP;
    ImGuiIO.KeyMap[ImGuiKey_PageDown] = AppKey_PAGEDOWN;
    ImGuiIO.KeyMap[ImGuiKey_Home] = AppKey_HOME;
    ImGuiIO.KeyMap[ImGuiKey_End] = AppKey_END;
    ImGuiIO.KeyMap[ImGuiKey_Delete] = AppKey_DELETE;
    ImGuiIO.KeyMap[ImGuiKey_Backspace] = AppKey_BACKSPACE;
    ImGuiIO.KeyMap[ImGuiKey_Enter] = AppKey_ENTER;
    ImGuiIO.KeyMap[ImGuiKey_Escape] = AppKey_ESCAPE;
    ImGuiIO.KeyMap[ImGuiKey_A] = AppKey_A;
    ImGuiIO.KeyMap[ImGuiKey_C] = AppKey_C;
    ImGuiIO.KeyMap[ImGuiKey_V] = AppKey_V;
    ImGuiIO.KeyMap[ImGuiKey_X] = AppKey_X;
    ImGuiIO.KeyMap[ImGuiKey_Y] = AppKey_Y;
    ImGuiIO.KeyMap[ImGuiKey_Z] = AppKey_Z;

    AppState->StartCounter = AppState->PlatformGetCounter();

    return true;
}

APP_CODE_RELOAD(AppReload)
{
    app_state *AppState = (app_state *)Memory;

    ImGui::SetCurrentContext(AppState->ImGuiContext);
}

APP_CODE_TOUCH_DOWN(AppTouchDown)
{
    app_state *AppState = (app_state *)Memory;

    switch(AppState->Mode)
    {
        case AppMode_PRERECORD:
        {
            if(!CheckInsideRectangle(V2I(TouchX, TouchY), AppState->ImGuiWindowRect))
            {
                AppState->StartCounter = AppState->PlatformGetCounter() - AppState->CounterElapsedWhenExitedRecordMode;
                HandleRecordModeMovement(AppState, TouchY);
                AppState->Mode = AppMode_RECORD;
            }
        } break;

        case AppMode_RECENT_PLAYBACK:
        {
            // save touch here for AppTouchMovement()
            AppState->LastTouchX = TouchX;
        } break;
    }

    ImGui::GetIO().MouseDown[0] = true;
}

APP_CODE_TOUCH_UP(AppTouchUp)
{
    app_state *AppState = (app_state *)Memory;

    switch(AppState->Mode)
    {
        case AppMode_RECORD:
        {
            AppState->CounterElapsedWhenExitedRecordMode = AppState->PlatformGetCounter() - AppState->StartCounter;
            HandleRecordModeMovement(AppState, TouchY);
            AppState->Mode = AppMode_RECENT_PLAYBACK;
        } break;
    }

    ImGui::GetIO().MouseDown[0] = false;
}

APP_CODE_TOUCH_MOVEMENT(AppTouchMovement)
{
    app_state *AppState = (app_state *)Memory;

    switch(AppState->Mode)
    {
        case AppMode_RECORD:
        {
            HandleRecordModeMovement(AppState, TouchY);
        } break;

        case AppMode_RECENT_PLAYBACK:
        {
            int TouchDeltaX = TouchX - AppState->LastTouchX;
            ChangeCameraTimePos(AppState, (real32)(-TouchDeltaX));

            AppState->LastTouchX = TouchX;
        } break;
    }

    ImGui::GetIO().MousePos = ImVec2((real32)TouchX, (real32)TouchY);
}

APP_CODE_NON_TOUCH_MOVEMENT(AppNonTouchMovement)
{
    ImGui::GetIO().MousePos = ImVec2((real32)TouchX, (real32)TouchY);
}

APP_CODE_ZOOM_IN(AppZoomIn)
{
    app_state *AppState = (app_state *)Memory;

    IncreasePixelsPerSecond(AppState);
}

APP_CODE_ZOOM_OUT(AppZoomOut)
{
    app_state *AppState = (app_state *)Memory;

    DecreasePixelsPerSecond(AppState);
}

APP_CODE_KEY_DOWN(AppKeyDown)
{
    app_state *AppState = (app_state *)Memory;

    switch(Key)
    {
        case AppKey_UP:
        {
            IncreasePixelsPerSecond(AppState);
        } break;

        case AppKey_DOWN:
        {
            DecreasePixelsPerSecond(AppState);
        } break;

        case AppKey_RIGHT:
        {
            IncreaseCameraTimePos(AppState);
        } break;

        case AppKey_LEFT:
        {
            DecreaseCameraTimePos(AppState);
        } break;
    }

    ImGui::GetIO().KeysDown[Key] = true;

    char InputChar = -1;
    switch(Key)
    {
        case AppKey_A:
        case AppKey_B:
        case AppKey_C:
        case AppKey_D:
        case AppKey_E:
        case AppKey_F:
        case AppKey_G:
        case AppKey_H:
        case AppKey_I:
        case AppKey_J:
        case AppKey_K:
        case AppKey_L:
        case AppKey_M:
        case AppKey_N:
        case AppKey_O:
        case AppKey_P:
        case AppKey_Q:
        case AppKey_R:
        case AppKey_S:
        case AppKey_T:
        case AppKey_U:
        case AppKey_V:
        case AppKey_W:
        case AppKey_X:
        case AppKey_Y:
        case AppKey_Z:
        {
            InputChar = KeyToChar(Key);

            if(!ImGui::GetIO().KeysDown[AppKey_SHIFT])
            {
                InputChar += 32;
            }
        } break;

        case ' ':
        {
            InputChar = ' ';
        } break;
    }

    if(InputChar != -1)
    {
        ImGui::GetIO().AddInputCharacter(InputChar);
    }
}

APP_CODE_KEY_UP(AppKeyUp)
{
    app_state *AppState = (app_state *)Memory;

    ImGui::GetIO().KeysDown[Key] = false;
}

APP_CODE_RENDER(AppRender)
{
    app_state *AppState = (app_state *)Memory;

    if(AppState->Mode == AppMode_RECORD)
    {
        AppState->CameraTimePos = AppState->PlatformGetSecondsElapsed(AppState->StartCounter);
    }

    // update vertex buffer
    {
        if(AppState->Mode == AppMode_RECORD)
        {
            PushLastTimePoint(AppState, AppState->PlatformGetSecondsElapsed(AppState->StartCounter));
        }

        if(AppState->TimePointsInVbo < AppState->TimePointsUsed)
        {
            uint32 ByteOffsetInVbo = AppState->TimePointsInVbo * sizeof(v2);
            uint32 NumBytesToCopy = (AppState->TimePointsUsed - AppState->TimePointsInVbo) * sizeof(v2);

            glBindBuffer(GL_ARRAY_BUFFER, AppState->Vbo);
            glBufferSubData(GL_ARRAY_BUFFER, ByteOffsetInVbo, NumBytesToCopy, 
                            &AppState->TimePoints[AppState->TimePointsInVbo]);
        }

        if(AppState->Mode == AppMode_RECORD)
        {
            PopTimePoint(AppState);
        }

        AppState->TimePointsInVbo = AppState->TimePointsUsed;
    }

    ImGui::NewFrame();
#if 1
    // draw bottom options window
    {
        v2 GuiWindowSize = {(real32)AppState->WindowWidth, (real32)AppState->WindowHeight / 27.0f};
        v2 GuiWindowPos = {0, AppState->WindowHeight - GuiWindowSize.Y};

        ImGui::SetNextWindowPos(ImVec2(GuiWindowPos.X, GuiWindowPos.Y));
        ImGui::SetNextWindowSize(ImVec2(GuiWindowSize.X, GuiWindowSize.Y)/*, ImGuiSetCond_Once*/);
        if(ImGui::Begin("window", NULL, 
                        ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoResize))
        {
            ImVec2 ImGuiWindowPosNative = ImGui::GetWindowPos();
            ImVec2 ImGuiWindowSizeNative = ImGui::GetWindowSize();
            v2 ImGuiWindowPos = {(real32)ImGuiWindowPosNative.x, (real32)ImGuiWindowPosNative.y};
            v2 ImGuiWindowSize = {(real32)ImGuiWindowSizeNative.x, (real32)ImGuiWindowSizeNative.y};
            RectanglePosSize(&AppState->ImGuiWindowRect, ImGuiWindowPos, ImGuiWindowSize);

            if(AppState->Mode == AppMode_RECENT_PLAYBACK ||
               AppState->Mode == AppMode_RANDOM_PLAYBACK)
            {
                if(ImGui::Button("New"))
                {
                    AppState->TimePointsUsed = 0;
                    AppState->TimePointsInVbo = 0;
                    AppState->TimePointsInVbo = 0;
                    AppState->StartCounter = AppState->PlatformGetCounter();
                    AppState->CounterElapsedWhenExitedRecordMode = 0;
                }
                ImGui::SameLine();
            }

            if(AppState->Mode == AppMode_RECENT_PLAYBACK)
            {
                if(ImGui::Button("Save"))
                {
                    FILE *File = fopen("feedback_output.fbk", "wb");
                    if(File == NULL)
                    {
                        // TODO: logging
                        Assert(0);
                        return;
                    }

                    // TOUCH_UP causes us to push a time point, so no need to worry about dummies
                    fwrite((void *)AppState->TimePoints, sizeof(v2), AppState->TimePointsCapacity, File);
                    fwrite((void *)&AppState->TimePointsUsed, sizeof(uint32), 1, File);
                    fclose(File);
                }
                ImGui::SameLine();
            }

            if(AppState->Mode != AppMode_RECORD)
            {
                if(ImGui::Button("Load"))
                {
                    Load(AppState);
                }
                ImGui::SameLine();
            }
        }
        ImGui::End();
    }

    // draw debug window
    {
        v2 GuiWindowSize = {(real32)AppState->WindowWidth / 2.1f, (real32)AppState->WindowHeight / 8.0f};
        v2 GuiWindowPos = {(real32)AppState->WindowWidth - GuiWindowSize.X, 0};

        ImGui::SetNextWindowPos(ImVec2(GuiWindowPos.X, GuiWindowPos.Y));
        ImGui::SetNextWindowSize(ImVec2(GuiWindowSize.X, GuiWindowSize.Y));
        if(ImGui::Begin("debug window", NULL,
                        ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoResize))
        {
            ImGui::Value("CameraTimePos", AppState->CameraTimePos);
#if 0
            ImGui::Value("PixelsPerSecond", AppState->PixelsPerSecond);

            local real32 RecordModeElapsedTime = 0;
            if(AppState->Mode == AppMode_TRACE)
            {
                RecordModeElapsedTime = AppState->PlatformGetSecondsElapsed(AppState->StartCounter);
            }

            ImGui::Value("RecordModeElapsedTime", RecordModeElapsedTime);
            ImGui::Checkbox("Draw Points", (bool *)&AppState->ShouldDrawPoints);
#endif
        }
        ImGui::End();
    }
#else
    ImGui::ShowTestWindow();
#endif

    bool32 ShouldDrawDummy = AppState->Mode == AppMode_RECORD;
    uint32 TimePointsToRender = ShouldDrawDummy ? AppState->TimePointsInVbo + 1 : AppState->TimePointsInVbo;
    real32 HalfWindowWidth = (real32)AppState->WindowWidth / 2.0f;
    real32 CameraTimePosCentered = AppState->CameraTimePos - (AppState->SecondsPerPixel * HalfWindowWidth);
    {
        // update uniforms
        glUniform1f(GetUniformLocation(AppState, AppState->ShaderProgram, "CameraTimePos"), CameraTimePosCentered);
        glUniform1f(GetUniformLocation(AppState, AppState->ShaderProgram, "PixelsPerSecond"), AppState->PixelsPerSecond);

        glClearColor(AppState->ClearColor.R, AppState->ClearColor.G, AppState->ClearColor.B, AppState->ClearColor.A);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(AppState->ShaderProgram);
        glBindVertexArray(AppState->Vao);

        // draw lines and points
        glUniform4fv(GetUniformLocation(AppState, AppState->ShaderProgram, "ShapeColor"), 1, AppState->LineColor.Values);
        glDrawArrays(GL_LINE_STRIP, 0, TimePointsToRender);
        if(AppState->ShouldDrawPoints)
        {
            glUniform4fv(GetUniformLocation(AppState, AppState->ShaderProgram, "ShapeColor"), 1, AppState->PointColor.Values);
            glDrawArrays(GL_POINTS, 0, TimePointsToRender);
        }

        ImGui::Render();
        ImGui_RenderFunction(AppState, ImGui::GetDrawData());
    }
}
//}
