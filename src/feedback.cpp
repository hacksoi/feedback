/*
    TODO:
        -allow new points to be added at cursor instead of middle of screen
        -alternate the colors of the lines
            -possible implementation: have two VBOs each of which contain every other line
        -in playback mode, add a circle at the middle of screen that follows the graph
        -compress time points to allow for at least an hour
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
    AppMode_PreRecording,
    AppMode_Recording,
    AppMode_PlaybackPaused,
    AppMode_PlaybackPlaying
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
    real32 _PixelsPerSecond, _SecondsPerPixel;
    real32 PixelsPerSecondFactor;

    real32 RecordingTime;
    real32 LastFrameTime;

    real32 CameraTimePos;
    real32 CameraTimePosDeltaPixels;

    int32 LastTouchX;
    real32 SingleFrameTime;
    bool32 IsCurrentRecordingSaved;

    v4 ClearColor, LineColor, PointColor;

    // platform
    platform_printf *Printf;

    // imgui/debug
    ImGuiContext *ImGuiContext;
    rectangle ImGuiWindowRect;
    bool32 ShouldDrawPoints;

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
layout (location = 1) in vec3 Color;

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
out vec4 OutputColor;

uniform vec4 ShapeColor;

void
main()
{
    OutputColor = ShapeColor;
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
    }

    return UniformLocation;
}
//}

/* application */
//{
inline internal void
_PushTimePoint(app_state *AppState, real32 Time, real32 Y)
{
    if(AppState->TimePointsUsed < AppState->TimePointsCapacity)
    {
        v2 *NewTimePoint = &AppState->TimePoints[AppState->TimePointsUsed++];

        NewTimePoint->Time = Time;
        NewTimePoint->Y = Y;
    }
}

inline internal void
_PushLastTimePoint(app_state *AppState, real32 Time)
{
    _PushTimePoint(AppState, Time, AppState->TimePoints[AppState->TimePointsUsed - 1].Y);
}

inline internal void
_PopTimePoint(app_state *AppState)
{
    AppState->TimePointsUsed--;
}

// NOTE: CurrentTime is the seconds since the start of this recording.
internal void
AddTimePoint(app_state *AppState, real32 RecordingTime, int FlippedY)
{
    if(AppState->TimePointsUsed > 0)
    {
        real32 TimeSinceLastTimePointAdd = RecordingTime - AppState->TimePoints[AppState->TimePointsUsed - 1].Time;
        if(TimeSinceLastTimePointAdd >= AppState->SingleFrameTime)
        {
            _PushLastTimePoint(AppState, RecordingTime - AppState->SingleFrameTime);
        }
    }

    _PushTimePoint(AppState, RecordingTime, (real32)(AppState->WindowHeight - FlippedY));
}

inline internal real32
GetSecondsPerPixel(app_state *AppState)
{
    real32 Result = AppState->_SecondsPerPixel;
    return Result;
}

inline internal real32
GetPixelsPerSecond(app_state *AppState)
{
    real32 Result = AppState->_PixelsPerSecond;
    return Result;
}

inline internal void
SetPixelsPerSecond(app_state *AppState, real32 NewPixelsPerSecond)
{
    AppState->_PixelsPerSecond = NewPixelsPerSecond;
    if(AppState->_PixelsPerSecond < 1)
    {
        AppState->_PixelsPerSecond = 1;
    }
    AppState->_SecondsPerPixel = 1.0f / AppState->_PixelsPerSecond;
}

inline internal void
IncreasePixelsPerSecond(app_state *AppState)
{
    real32 NewPixelsPerSecond = GetPixelsPerSecond(AppState) * AppState->PixelsPerSecondFactor;
    SetPixelsPerSecond(AppState, NewPixelsPerSecond);
}

inline internal void
DecreasePixelsPerSecond(app_state *AppState)
{
    real32 NewPixelsPerSecond = GetPixelsPerSecond(AppState) / AppState->PixelsPerSecondFactor;
    SetPixelsPerSecond(AppState, NewPixelsPerSecond);
}

inline internal real32
GetRecordingEndTime(app_state *AppState)
{
    real32 Result = 0;
    if(AppState->TimePointsUsed > 0)
    {
        Result = AppState->TimePoints[AppState->TimePointsUsed - 1].Time;
    }
    return Result;
}
//}

/* api to platform */
//{
APP_CODE_INITIALIZE(AppInitialize)
{
    app_state *AppState = (app_state *)Memory;
    uint64 MemoryUsed = sizeof(app_state);
    if(MemoryUsed > MemorySize)
    {
        // TODO: logging
        AppState->Printf("Not enough memory for app_state\n");
    }

    AppState->TimePoints = (v2 *)((uint8 *)Memory + MemoryUsed);
    AppState->TimePointsCapacity = Megabytes(1) / sizeof(v2);
    MemoryUsed += AppState->TimePointsCapacity * sizeof(v2);
    if(MemoryUsed > MemorySize)
    {
        // TODO: logging
        AppState->Printf("Not enough memory for time points\n");
    }

    AppState->WindowWidth = WindowWidth;
    AppState->WindowHeight = WindowHeight;
    AppState->DefaultPixelsPerSecond = (real32)AppState->WindowWidth / 5.0f;
    SetPixelsPerSecond(AppState, AppState->DefaultPixelsPerSecond);
    AppState->PixelsPerSecondFactor = 1.1f;
    AppState->SingleFrameTime = (real32)TimeBetweenFramesMillis / 1000.0f;
    AppState->CameraTimePosDeltaPixels = 15.0f;
    AppState->Mode = AppMode_PreRecording;
    AppState->Printf = PlatformPrintf;

#if 1
    SetColor(&AppState->ClearColor, 0.2f, 0.3f, 0.3f, 1.0f);
    SetColor(&AppState->LineColor, 1.0f, 0.5f, 0.2f, 1.0f);
#else
    SetColor(&AppState->ClearColor, 1.0f, 1.0f, 1.0f, 1.0f);
    SetColor(&AppState->LineColor, 0.0f, 0.0f, 0.0f, 1.0f);
#endif
    SetColor(&AppState->PointColor, 1.0f, 1.0f, 1.0f, 1.0f);

    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, AppState->WindowWidth, AppState->WindowHeight);

    glLineWidth(3.0f);
    glPointSize(1.5f);

    create_shader_result CreateVertexShaderResult = CreateShader(GL_VERTEX_SHADER, VertexShaderSource);
    if(!CreateVertexShaderResult.DidCompileSuccessfully)
    {
        // TODO: logging
        PrintOpenGLError(AppState, CreateVertexShaderResult.Shader, 
                         glGetShaderInfoLog, "Error compiling vertex shader: \n");
    }

    create_shader_result CreateFragmentShaderResult = CreateShader(GL_FRAGMENT_SHADER, FragmentShaderSource);
    if(!CreateFragmentShaderResult.DidCompileSuccessfully)
    {
        // TODO: logging
        PrintOpenGLError(AppState, CreateFragmentShaderResult.Shader, 
                         glGetShaderInfoLog, "Error compiling fragment shader: \n");
    }

    AppState->ShaderProgram = glCreateProgram();
    glAttachShader(AppState->ShaderProgram, CreateVertexShaderResult.Shader);
    glAttachShader(AppState->ShaderProgram, CreateFragmentShaderResult.Shader);
    glLinkProgram(AppState->ShaderProgram);

    bool32 DidProgramLinkSuccessfully;
    glGetProgramiv(AppState->ShaderProgram, GL_LINK_STATUS, &DidProgramLinkSuccessfully);
    if(!DidProgramLinkSuccessfully)
    {
        // TODO: logging
        PrintOpenGLError(AppState, AppState->ShaderProgram, 
                         glGetProgramInfoLog, "Error linking shader program: \n");
    }

    glDeleteShader(CreateVertexShaderResult.Shader);
    glDeleteShader(CreateFragmentShaderResult.Shader);

    glUseProgram(AppState->ShaderProgram);
    glUniform2f(GetUniformLocation(AppState, AppState->ShaderProgram, "WindowDimensions"), 
                (real32)AppState->WindowWidth, (real32)AppState->WindowHeight);

    glGenBuffers(1, &AppState->Vbo);
    glBindBuffer(GL_ARRAY_BUFFER, AppState->Vbo);
    uint32 SizeOfVboElement = (2 * sizeof(float)) + (3 * sizeof(float)); // position and color
    glBufferData(GL_ARRAY_BUFFER, 2 * AppState->TimePointsCapacity * SizeOfVboElement, NULL, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &AppState->Vao);
    glBindVertexArray(AppState->Vao);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(0);

    // initialize imgui
    {
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
    }
}

APP_CODE_HANDLE_EVENT(AppHandleEvent)
{
    app_state *AppState = (app_state *)Memory;

    switch(AppState->Mode)
    {
        case AppMode_Recording:
        {
            real32 TimeSinceLastFrame = CurrentTime - AppState->LastFrameTime;
            AppState->RecordingTime += TimeSinceLastFrame;
        } break;

        case AppMode_PlaybackPlaying:
        {
            real32 TimeSinceLastFrame = CurrentTime - AppState->LastFrameTime;
            AppState->CameraTimePos += TimeSinceLastFrame;
        } break;
    }

    switch(Event->Type)
    {
        case AppEventType_CodeReload:
        {
            ImGui::SetCurrentContext(AppState->ImGuiContext);
        } break;

        case AppEventType_TouchDown:
        {
            switch(AppState->Mode)
            {
                case AppMode_PreRecording:
                {
                    if(!CheckInsideRectangle(V2I(Event->TouchX, Event->TouchY), AppState->ImGuiWindowRect))
                    {
                        AddTimePoint(AppState, AppState->RecordingTime, Event->TouchY);
                        AppState->Mode = AppMode_Recording;
                    }
                } break;
            }

            AppState->LastTouchX = Event->TouchX;

            ImGui::GetIO().MouseDown[0] = true;
        } break;

        case AppEventType_TouchUp:
        {
            switch(AppState->Mode)
            {
                case AppMode_Recording:
                {
                    AddTimePoint(AppState, AppState->RecordingTime, Event->TouchY);
                    AppState->IsCurrentRecordingSaved = false;
                    AppState->Mode = AppMode_PlaybackPaused;
                } break;

                case AppMode_PlaybackPlaying:
                {
                    AppState->Mode = AppMode_PlaybackPaused;
                } break;
            }

            AppState->LastTouchX = Event->TouchX;

            ImGui::GetIO().MouseDown[0] = false;
        } break;

        case AppEventType_TouchMovement:
        {
            switch(AppState->Mode)
            {
                case AppMode_PreRecording:
                {
                } break;

                case AppMode_Recording:
                {
                    AddTimePoint(AppState, AppState->RecordingTime, Event->TouchY);
                } break;

                case AppMode_PlaybackPaused:
                {
                    int TouchDeltaX = Event->TouchX - AppState->LastTouchX;

                    real32 CameraTimePosDeltaPixels = (real32)(-TouchDeltaX);
                    AppState->CameraTimePos += GetSecondsPerPixel(AppState) * CameraTimePosDeltaPixels;
                    if(AppState->CameraTimePos < 0.0f)
                    {
                        AppState->CameraTimePos = 0.0f;
                    }
                    else if(AppState->CameraTimePos > GetRecordingEndTime(AppState))
                    {
                        AppState->CameraTimePos = GetRecordingEndTime(AppState);
                    }
                } break;

                case AppMode_PlaybackPlaying:
                {
                } break;
            }

            AppState->LastTouchX = Event->TouchX;

            ImGui::GetIO().MousePos = ImVec2((real32)Event->TouchX, (real32)Event->TouchY);
        } break;

        case AppEventType_ZoomInPressed:
        {
            IncreasePixelsPerSecond(AppState);
        } break;

        case AppEventType_ZoomOutPressed:
        {
            DecreasePixelsPerSecond(AppState);
        } break;

        case AppEventType_NonTouchMovement:
        {
            ImGui::GetIO().MousePos = ImVec2((real32)Event->TouchX, (real32)Event->TouchY);
        } break;

        case AppEventType_UpdateAndRender:
        {
            if(AppState->Mode == AppMode_Recording)
            {
                AppState->CameraTimePos = AppState->RecordingTime;
            }
            else if(AppState->Mode == AppMode_PlaybackPlaying)
            {
                if(AppState->CameraTimePos > GetRecordingEndTime(AppState))
                {
                    AppState->CameraTimePos = GetRecordingEndTime(AppState);
                    AppState->Mode = AppMode_PlaybackPaused;
                }
            }

            // update vertex buffer
            {
                if(AppState->Mode == AppMode_Recording)
                {
                    _PushLastTimePoint(AppState, AppState->RecordingTime);
                }

                if(AppState->TimePointsInVbo < AppState->TimePointsUsed)
                {
                    uint32 ByteOffsetInVbo = AppState->TimePointsInVbo * sizeof(v2);
                    uint32 NumBytesToCopy = (AppState->TimePointsUsed - AppState->TimePointsInVbo) * sizeof(v2);

                    float NewElements[2 * 16 * AppState->NumValuesPerVboElement];
                    for(int TimePointsIndex = AppState->TimePointsInVbo; 
                        TimePointsIndex < AppState->TimePointsUsed; 
                        TimePointsIndex++)
                    {
                        // fill this easy shit in nhk
                    }

                    glBindBuffer(GL_ARRAY_BUFFER, AppState->Vbo);
                    glBufferSubData(GL_ARRAY_BUFFER, ByteOffsetInVbo, NumBytesToCopy, 
                                    &AppState->TimePoints[AppState->TimePointsInVbo]);
                }

                if(AppState->Mode == AppMode_Recording)
                {
                    _PopTimePoint(AppState);
                }

                AppState->TimePointsInVbo = AppState->TimePointsUsed;
            }

            ImGui::NewFrame();

            // draw options window
            {
                v2 GuiWindowSize = {(real32)AppState->WindowWidth / 3.0f, (real32)AppState->WindowHeight / 27.0f};
                v2 GuiWindowPos = {(real32)AppState->WindowWidth - GuiWindowSize.X, 0};

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

                    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(0.3f, 0.3f, 0.3f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(0.3f, 0.3f, 0.3f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(0.3f, 0.3f, 0.3f));
                    if(ImGui::Button("New"))
                    {
                        if(AppState->Mode == AppMode_PlaybackPaused)
                        {
                            AppState->TimePointsUsed = 0;
                            AppState->TimePointsInVbo = 0;
                            AppState->TimePointsInVbo = 0;
                            AppState->RecordingTime = 0;
                            AppState->Mode = AppMode_PreRecording;
                        }
                    }
                    ImGui::PopStyleColor(3);

                    ImGui::SameLine();
                    if(ImGui::Button("Save"))
                    {
                        if(AppState->Mode == AppMode_PlaybackPaused &&
                           !AppState->IsCurrentRecordingSaved)
                        {
                            char *Filename = "feedback_output.fbk";
                            FILE *File = fopen(Filename, "wb");
                            if(File == NULL)
                            {
                                // TODO: logging
                                AppState->Printf("Failed to open file: %s\n", Filename);
                            }

                            // TOUCH_UP causes us to push a time point, so no need to worry about dummies
                            fwrite((void *)AppState->TimePoints, sizeof(v2), AppState->TimePointsCapacity, File);
                            fwrite((void *)&AppState->TimePointsUsed, sizeof(uint32), 1, File);
                            fclose(File);
                        }
                    }

                    ImGui::SameLine();
                    if(ImGui::Button("Load"))
                    {
                        if(AppState->Mode == AppMode_PreRecording ||
                           AppState->Mode == AppMode_PlaybackPaused)
                        {
                            char *Filename = "feedback_output.fbk";
                            FILE *File = fopen(Filename, "rb");
                            if(File == NULL)
                            {
                                // TODO: logging
                                AppState->Printf("Failed to open file: %s\n", Filename);
                            }

                            fread((void *)AppState->TimePoints, sizeof(v2), AppState->TimePointsCapacity, File);
                            fread((void *)&AppState->TimePointsUsed, sizeof(uint32), 1, File);
                            fclose(File);

                            AppState->CameraTimePos = 0.0f;
                            SetPixelsPerSecond(AppState, AppState->DefaultPixelsPerSecond);

                            // ensure the newly loaded time points will be uploaded to the vertex buffer
                            AppState->TimePointsInVbo = 0;

                            AppState->IsCurrentRecordingSaved = true;
                            AppState->Mode = AppMode_PlaybackPaused;
                        }
                    }

                    ImGui::SameLine();
                    if(ImGui::Button("Play"))
                    {
                        if(AppState->Mode == AppMode_PlaybackPaused)
                        {
                            AppState->Mode = AppMode_PlaybackPlaying;
                        }
                    }
                }
                ImGui::End();
            }

            // draw debug window
            {
                v2 GuiWindowSize = {(real32)AppState->WindowWidth / 3.0f, (real32)AppState->WindowHeight / 8.0f};
                v2 GuiWindowPos = {0, 0};

                ImGui::SetNextWindowPos(ImVec2(GuiWindowPos.X, GuiWindowPos.Y));
                ImGui::SetNextWindowSize(ImVec2(GuiWindowSize.X, GuiWindowSize.Y));
                if(ImGui::Begin("debug window", NULL,
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize))
                {
                    ImGui::Value("CameraTimePos", AppState->CameraTimePos);
                    ImGui::Checkbox("Draw Points", (bool *)&AppState->ShouldDrawPoints);
                }
                ImGui::End();
            }

#if 0
            ImGui::ShowTestWindow();
#endif

            // draw app
            {
                real32 HalfWindowWidth = (real32)AppState->WindowWidth / 2.0f;
                real32 CameraTimePosCentered = AppState->CameraTimePos - (GetSecondsPerPixel(AppState) * HalfWindowWidth);
                glUniform1f(GetUniformLocation(AppState, AppState->ShaderProgram, "CameraTimePos"), CameraTimePosCentered);

                glUniform1f(GetUniformLocation(AppState, AppState->ShaderProgram, "PixelsPerSecond"), GetPixelsPerSecond(AppState));

                glClearColor(AppState->ClearColor.R, AppState->ClearColor.G, AppState->ClearColor.B, AppState->ClearColor.A);
                glClear(GL_COLOR_BUFFER_BIT);

                glUseProgram(AppState->ShaderProgram);
                glBindVertexArray(AppState->Vao);

                bool32 ShouldDrawDummy = AppState->Mode == AppMode_Recording;
                uint32 TimePointsToRender = ShouldDrawDummy ? AppState->TimePointsInVbo + 1 : AppState->TimePointsInVbo;

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
        } break;
    }

    AppState->LastFrameTime = CurrentTime;
}
//}
