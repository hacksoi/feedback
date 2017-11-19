/*
    TODO:
        -clean up the fucking code:
            -make Log() global
        -alternate the colors of the lines
            -possible implementation: have two VBOs each of which contain every other line
        -in playback mode, add a circle at the middle of screen that follows the graph
        -compress time points to allow for at least an hour
*/

#include "nps_math.h"

#include <stdio.h>
#include <string.h>

#include "glew.h"

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
struct two_points
{
    v2 P1, P2;
};

struct app_context
{
    uint32_t WindowWidth, WindowHeight;

    app_mode Mode;

    uint32_t ShaderProgram;
    uint32_t Vbo;
    uint32_t Vao;

    uint32_t TimePointsCapacity;
    v2 *TimePoints;
    uint32_t TimePointsUsed;
    uint32_t TimePointsInVbo;

    uint32_t ScratchSize;
    uint8_t *Scratch;

    float DefaultPixelsPerSecond;
    float _PixelsPerSecond, _SecondsPerPixel;
    float PixelsPerSecondFactor;

    float RecordingTime;
    float LastFrameTime;

    float CameraTimePos;
    float CameraTimePosDeltaPixels;

    float SingleFrameTime;
    bool32 IsCurrentRecordingSaved;

    v4 ClearColor, LineColor, PointColor;

    // platform
    platform_printf *Printf;
    platform_log *Log;

    // imgui/debug
    ImGuiContext *ImGuiContext;
    rect2 ImGuiWindowRect;
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
#if 0
global char *VertexShaderSource = R"STR(
#version 330 core
layout (location = 0) in vec2 TimePos;

uniform vec2 WindowDimensions;
uniform float PixelsPerSecond;
uniform float CameraTimePos;

out vec3 fsPos;

void
main()
{
    vec2 ScreenPos = vec2((TimePos.x - CameraTimePos) * PixelsPerSecond, TimePos.y);
    vec2 ClipPos = ((2.0f * ScreenPos) / WindowDimensions) - 1.0f;
    gl_Position = vec4(ClipPos, 0.0, 1.0);
}
)STR";

global char *FragmentShaderSource = R"STR(
#version 330 core
uniform vec4 ShapeColor;

out vec4 OutputColor;

void
main()
{
    OutputColor = ShapeColor;
}
)STR";
#else
global char *VertexShaderSource = R"STR(
#version 330 core

layout(location = 0) in vec2 vsBottomPos;
layout(location = 1) in vec2 vsTopPos;

uniform vec2 WindowDimensions;

out vec2 gsWindowDimensions;
out vec2 gsBottomPos;
out vec2 gsTopPos;

void
main()
{
    // dummy
    gl_Position = vec4(0.0f, 0.0f, 0.0f, 1.0f);

    gsWindowDimensions = WindowDimensions;

    gsBottomPos = vsBottomPos;
    gsTopPos = vsTopPos;
}
)STR";

global char *GeometryShaderSource = R"STR(
#version 330 core

layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

in vec2 gsWindowDimensions[];
in vec2 gsBottomPos[];
in vec2 gsTopPos[];

out vec2 fsPos;
out vec2 fsLineP1;
out vec2 fsLineP2;

vec4
ToClip(vec2 WorldPos)
{
    return vec4(((2.0f * WorldPos) / gsWindowDimensions[0]) - 1.0f, 0.0f, 1.0f);
}

void
main()
{
    vec2 LineP1 = gsBottomPos[0] + 0.5f*(gsTopPos[0] - gsBottomPos[0]);
    vec2 LineP2 = gsBottomPos[1] + 0.5f*(gsTopPos[1] - gsBottomPos[1]);

    fsLineP1 = LineP1;
    fsLineP2 = LineP2;

    // bottom left
    fsPos = gsBottomPos[0];
    gl_Position = ToClip(gsBottomPos[0]);
    EmitVertex();

    // bottom right
    fsPos = gsBottomPos[1];
    gl_Position = ToClip(gsBottomPos[1]);
    EmitVertex();

    // top left
    fsPos = gsTopPos[0];
    gl_Position = ToClip(gsTopPos[0]);
    EmitVertex();

    // top right
    fsPos = gsTopPos[1];
    gl_Position = ToClip(gsTopPos[1]);
    EmitVertex();

    EndPrimitive();
}
)STR";

global char *FragmentShaderSource = R"STR(
#version 330 core

uniform float LineWidth;

in vec2 fsPos;
in vec2 fsLineP1;
in vec2 fsLineP2;

out vec4 OutputColor;

float
DistToLine()
{
    vec2 LineDir = fsLineP2 - fsLineP1;
    vec2 RelPos = fsPos - fsLineP1;
    float RelPosDotLineDir = dot(RelPos, LineDir);
    float LineDirLenSq = dot(LineDir, LineDir);
    float t = clamp(RelPosDotLineDir / LineDirLenSq, 0.0f, 1.0f);
    vec2 ClosestPointOnLine = fsLineP1 + t*LineDir;
    vec2 Diff = fsPos - ClosestPointOnLine;
    return length(Diff);
}

void
main()
{
    float Distance = DistToLine() / (LineWidth / 2.0f);
    float CutOff = 0.5f;
    float Alpha;
    if(Distance <= CutOff)
    {
        Alpha = 1.0f;
    }
    else
    {
        Alpha = 1.0f - ((1.0f / CutOff) * (Distance - CutOff));
    }
    //vec4 LineColor = vec4(0.0f, 0.0f, 0.0f, Alpha);
    vec4 LineColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    OutputColor = LineColor;
}
)STR";
#endif
//}

/* global variables */
//{
global char GeneralBuffer[512];
global float TmpBuffer[1024 * 4];
//}

internal two_points
CreatePoints(v2 P1, v2 P2, v2 P3, float LineWidth)
{
    line2 Line1 = {P1, P2};
    line2 Line2 = {P2, P3};

    quad2 Line1QuadSimple = CreateLineQuad(Line1, LineWidth);
    quad2 Line2QuadSimple = CreateLineQuad(Line2, LineWidth);

    v2 Line1TopDirection = Line1QuadSimple.TopRight - Line1QuadSimple.TopLeft;
    v2 Line2TopDirection = Line2QuadSimple.TopLeft - Line2QuadSimple.TopRight;
    v2 Line1BottomDirection = Line1QuadSimple.BottomRight - Line1QuadSimple.BottomLeft;
    v2 Line2BottomDirection = Line2QuadSimple.BottomLeft - Line2QuadSimple.BottomRight;

    ray2 Line1TopRay = {Line1QuadSimple.TopLeft, Line1TopDirection};
    ray2 Line2TopRay = {Line2QuadSimple.TopRight, Line2TopDirection};
    ray2 Line1BottomRay = {Line1QuadSimple.BottomLeft, Line1BottomDirection};
    ray2 Line2BottomRay = {Line2QuadSimple.BottomRight, Line2BottomDirection};

    v2 TopIntersection, BottomIntersection;
    Assert(FindIntersection(&TopIntersection, Line1TopRay, Line2TopRay) == true);
    Assert(FindIntersection(&BottomIntersection, Line1BottomRay, Line2BottomRay) == true);

    two_points Result;
    Result.P1 = TopIntersection;
    Result.P2 = BottomIntersection;
    return Result;
}

/* IMGUI */
//{ 
internal void
ImGui_CreateDeviceObjects(app_context *AppContext)
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

    AppContext->g_ShaderHandle = glCreateProgram();
    AppContext->g_VertHandle = glCreateShader(GL_VERTEX_SHADER);
    AppContext->g_FragHandle = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(AppContext->g_VertHandle, 1, &vertex_shader, 0);
    glShaderSource(AppContext->g_FragHandle, 1, &fragment_shader, 0);
    glCompileShader(AppContext->g_VertHandle);
    glCompileShader(AppContext->g_FragHandle);
    glAttachShader(AppContext->g_ShaderHandle, AppContext->g_VertHandle);
    glAttachShader(AppContext->g_ShaderHandle, AppContext->g_FragHandle);
    glLinkProgram(AppContext->g_ShaderHandle);

    AppContext->g_AttribLocationTex = glGetUniformLocation(AppContext->g_ShaderHandle, "Texture");
    AppContext->g_AttribLocationProjMtx = glGetUniformLocation(AppContext->g_ShaderHandle, "ProjMtx");
    AppContext->g_AttribLocationPosition = glGetAttribLocation(AppContext->g_ShaderHandle, "Position");
    AppContext->g_AttribLocationUV = glGetAttribLocation(AppContext->g_ShaderHandle, "UV");
    AppContext->g_AttribLocationColor = glGetAttribLocation(AppContext->g_ShaderHandle, "Color");

    glGenBuffers(1, &AppContext->g_VboHandle);
    glGenBuffers(1, &AppContext->g_ElementsHandle);

    glGenVertexArrays(1, &AppContext->g_VaoHandle);
    glBindVertexArray(AppContext->g_VaoHandle);
    glBindBuffer(GL_ARRAY_BUFFER, AppContext->g_VboHandle);
    glEnableVertexAttribArray(AppContext->g_AttribLocationPosition);
    glEnableVertexAttribArray(AppContext->g_AttribLocationUV);
    glEnableVertexAttribArray(AppContext->g_AttribLocationColor);

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    glVertexAttribPointer(AppContext->g_AttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
    glVertexAttribPointer(AppContext->g_AttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
    glVertexAttribPointer(AppContext->g_AttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

    // Load font
    uint8_t *Pixels;
    int Width, Height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

    glGenTextures(1, &AppContext->g_FontTexture);
    glBindTexture(GL_TEXTURE_2D, AppContext->g_FontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);

    ImGui::GetIO().Fonts->TexID = (void *)(uint64_t)AppContext->g_FontTexture;

    // Restore modified GL state
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindVertexArray(last_vertex_array);
}

internal void
ImGui_RenderFunction(app_context *AppContext, ImDrawData* draw_data)
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
    glUseProgram(AppContext->g_ShaderHandle);
    glUniform1i(AppContext->g_AttribLocationTex, 0);
    glUniformMatrix4fv(AppContext->g_AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
    glBindVertexArray(AppContext->g_VaoHandle);

    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx* idx_buffer_offset = 0;

        glBindBuffer(GL_ARRAY_BUFFER, AppContext->g_VboHandle);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, AppContext->g_ElementsHandle);
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
typedef void opengl_get_error_info(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
internal void
PrintOpenGLError(app_context *AppContext, uint32_t OpenGLObject, opengl_get_error_info OpenGLGetErrorInfo, char *ExitMessage)
{
    size_t ExitMessageLength = strlen(ExitMessage);

    strcpy(GeneralBuffer, ExitMessage);
    OpenGLGetErrorInfo(OpenGLObject, (GLsizei)(sizeof(GeneralBuffer) - ExitMessageLength - 1), 
                       NULL, GeneralBuffer + ExitMessageLength);

    AppContext->Log("%s\n", GeneralBuffer);
}

internal uint32_t
CreateShader(app_context *AppContext, GLenum ShaderType, char *ShaderSource)
{
    uint32_t VertexShader = glCreateShader(ShaderType);
    glShaderSource(VertexShader, 1, &ShaderSource, NULL);
    glCompileShader(VertexShader);

    bool32 DidCompileSuccessfully;
    glGetShaderiv(VertexShader, GL_COMPILE_STATUS, &DidCompileSuccessfully);
    if(!DidCompileSuccessfully)
    {
        char *ErrorMessage;
        switch(ShaderType)
        {
            case GL_VERTEX_SHADER:
            {
                ErrorMessage = "error compiling vertex shader: \n";
            } break;

            case GL_FRAGMENT_SHADER:
            {
                ErrorMessage = "error compiling fragment shader: \n";
            } break;

            default:
            {
                ErrorMessage = "error compiling unknown shader type: \n";
            } break;
        }

        PrintOpenGLError(AppContext, VertexShader, glGetShaderInfoLog, ErrorMessage);
        VertexShader = 0;
        Assert(0);
    }

    return VertexShader;
}

inline internal int32_t
GetUniformLocation(app_context *AppContext, uint32_t ShaderProgram, char *UniformName)
{
    int32_t UniformLocation = glGetUniformLocation(ShaderProgram, UniformName);
    if(UniformLocation == -1)
    {
        AppContext->Log("error: could not find uniform '%s'.\n", UniformName);
    }

    return UniformLocation;
}
//}

/* application */
//{
inline internal void
_PushTimePoint(app_context *AppContext, float Time, float Y)
{
    if(AppContext->TimePointsUsed < AppContext->TimePointsCapacity)
    {
        v2 *NewTimePoint = &AppContext->TimePoints[AppContext->TimePointsUsed++];

        NewTimePoint->X = Time;
        NewTimePoint->Y = Y;
    }
}

inline internal void
_PushLastTimePoint(app_context *AppContext, float Time)
{
    _PushTimePoint(AppContext, Time, AppContext->TimePoints[AppContext->TimePointsUsed - 1].Y);
}

inline internal void
_PopTimePoint(app_context *AppContext)
{
    AppContext->TimePointsUsed--;
}

// NOTE: CurrentTime is the seconds since the start of this recording.
internal void
AddTimePoint(app_context *AppContext, float RecordingTime, int32_t Y)
{
    if(AppContext->TimePointsUsed > 0)
    {
        float TimeSinceLastTimePointAdd = RecordingTime - AppContext->TimePoints[AppContext->TimePointsUsed - 1].X;
        if(TimeSinceLastTimePointAdd >= AppContext->SingleFrameTime)
        {
            _PushLastTimePoint(AppContext, RecordingTime - AppContext->SingleFrameTime);
        }
    }

    _PushTimePoint(AppContext, RecordingTime, (float)Y);
}

inline internal float
GetSecondsPerPixel(app_context *AppContext)
{
    float Result = AppContext->_SecondsPerPixel;
    return Result;
}

inline internal float
GetPixelsPerSecond(app_context *AppContext)
{
    float Result = AppContext->_PixelsPerSecond;
    return Result;
}

inline internal void
SetPixelsPerSecond(app_context *AppContext, float NewPixelsPerSecond)
{
    AppContext->_PixelsPerSecond = NewPixelsPerSecond;
    if(AppContext->_PixelsPerSecond < 1)
    {
        AppContext->_PixelsPerSecond = 1;
    }
    AppContext->_SecondsPerPixel = 1.0f / AppContext->_PixelsPerSecond;
}

inline internal void
IncreasePixelsPerSecond(app_context *AppContext)
{
    float NewPixelsPerSecond = GetPixelsPerSecond(AppContext) * AppContext->PixelsPerSecondFactor;
    SetPixelsPerSecond(AppContext, NewPixelsPerSecond);
}

inline internal void
DecreasePixelsPerSecond(app_context *AppContext)
{
    float NewPixelsPerSecond = GetPixelsPerSecond(AppContext) / AppContext->PixelsPerSecondFactor;
    SetPixelsPerSecond(AppContext, NewPixelsPerSecond);
}

inline internal float
GetRecordingEndTime(app_context *AppContext)
{
    float Result = 0;
    if(AppContext->TimePointsUsed > 0)
    {
        Result = AppContext->TimePoints[AppContext->TimePointsUsed - 1].X;
    }
    return Result;
}
//}

/* api to platform */
//{
APP_INITIALIZE(AppInitialize)
{
    app_context *AppContext = (app_context *)Memory;
    uint64_t MemoryUsed = sizeof(app_context);
    if(MemoryUsed > MemorySize)
    {
        PlatformLog("Not enough memory for app_context\n");
        return false;
    }

    AppContext->TimePoints = (v2 *)((uint8_t *)Memory + MemoryUsed);
    AppContext->TimePointsCapacity = Megabytes(1) / sizeof(v2);
    MemoryUsed += AppContext->TimePointsCapacity * sizeof(v2);
    if(MemoryUsed > MemorySize)
    {
        PlatformLog("Not enough memory for time points\n");
        return false;
    }

    AppContext->ScratchSize = Megabytes(1);
    AppContext->Scratch = (uint8_t *)Memory + MemoryUsed;
    MemoryUsed += AppContext->ScratchSize;
    if(MemoryUsed > MemorySize)
    {
        PlatformLog("Not enough memory for scratch size\n");
        return false;
    }

    AppContext->WindowWidth = WindowWidth;
    AppContext->WindowHeight = WindowHeight;
    AppContext->DefaultPixelsPerSecond = (float)AppContext->WindowWidth / 5.0f;
    SetPixelsPerSecond(AppContext, AppContext->DefaultPixelsPerSecond);
    AppContext->PixelsPerSecondFactor = 1.1f;
    AppContext->SingleFrameTime = (float)TimeBetweenFrames;
    AppContext->CameraTimePosDeltaPixels = 15.0f;
    AppContext->Mode = AppMode_PreRecording;
    AppContext->Printf = PlatformPrintf;
    AppContext->Log = PlatformLog;

#if 0
    SetColor(&AppContext->ClearColor, 0.2f, 0.3f, 0.3f, 1.0f);
    SetColor(&AppContext->LineColor, 1.0f, 0.5f, 0.2f, 1.0f);
#else
    AppContext->ClearColor = V4(1.0f, 1.0f, 1.0f, 1.0f);
    AppContext->LineColor = V4(0.0f, 0.0f, 0.0f, 1.0f);
#endif
    AppContext->PointColor = V4(1.0f, 1.0f, 1.0f, 1.0f);

#if 0
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, AppContext->WindowWidth, AppContext->WindowHeight);

    glPointSize(1.5f);

    uint32_t VertexShader = CreateShader(AppContext, GL_VERTEX_SHADER, VertexShaderSource);
    if(VertexShader == 0)
    {
        return false;
    }

    uint32_t GeometryShader = CreateShader(AppContext, GL_GEOMETRY_SHADER, GeometryShaderSource);
    if(GeometryShader == 0)
    {
        return false;
    }

    uint32_t FragmentShader = CreateShader(AppContext, GL_FRAGMENT_SHADER, FragmentShaderSource);
    if(FragmentShader == 0)
    {
        return false;
    }

    AppContext->ShaderProgram = glCreateProgram();
    glAttachShader(AppContext->ShaderProgram, VertexShader);
    glAttachShader(AppContext->ShaderProgram, GeometryShader);
    glAttachShader(AppContext->ShaderProgram, FragmentShader);
    glLinkProgram(AppContext->ShaderProgram);

    bool32 DidProgramLinkSuccessfully;
    glGetProgramiv(AppContext->ShaderProgram, GL_LINK_STATUS, &DidProgramLinkSuccessfully);
    if(!DidProgramLinkSuccessfully)
    {
        PrintOpenGLError(AppContext, AppContext->ShaderProgram, 
                         glGetProgramInfoLog, "Error linking shader program: \n");
        Assert(0);
        return false;
    }

    glDeleteShader(VertexShader);
    glDeleteShader(GeometryShader);
    glDeleteShader(FragmentShader);

    glUseProgram(AppContext->ShaderProgram);
    glUniform2f(GetUniformLocation(AppContext, AppContext->ShaderProgram, "WindowDimensions"), 
                (float)AppContext->WindowWidth, (float)AppContext->WindowHeight);

    glGenBuffers(1, &AppContext->Vbo);
    glBindBuffer(GL_ARRAY_BUFFER, AppContext->Vbo);
    uint32_t SizeOfVboElement = 4 * sizeof(float);
    glBufferData(GL_ARRAY_BUFFER, AppContext->TimePointsCapacity * SizeOfVboElement, NULL, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &AppContext->Vao);
    glBindVertexArray(AppContext->Vao);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // initialize imgui
    {
        AppContext->ImGuiContext = ImGuiContext;
        ImGui::SetCurrentContext(AppContext->ImGuiContext);

        ImGui_CreateDeviceObjects(AppContext);

        ImGuiIO& ImGuiIO = ImGui::GetIO();
        ImGuiIO.DisplaySize.x = (float)AppContext->WindowWidth;
        ImGuiIO.DisplaySize.y = (float)AppContext->WindowHeight;
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

    return true;
}

APP_UPDATE_AND_RENDER(AppUpdateAndRender)
{
    app_context *AppContext = (app_context *)Memory;

    if(CodeReload)
    {
        ImGui::SetCurrentContext(AppContext->ImGuiContext);
    }

    float TimeSinceLastFrame = CurrentTime - AppContext->LastFrameTime;
    switch(AppContext->Mode)
    {
        case AppMode_Recording:
        {
            AppContext->RecordingTime += TimeSinceLastFrame;
        } break;

        case AppMode_PlaybackPlaying:
        {
            AppContext->CameraTimePos += TimeSinceLastFrame;
        } break;
    }
    AppContext->LastFrameTime = CurrentTime;

    // input
    {
        if(Input->MousePressed)
        {
            switch(AppContext->Mode)
            {
                case AppMode_PreRecording:
                {
                    if(!CheckInsideRectangle(V2((float)Input->MouseX, (float)Input->MouseY), AppContext->ImGuiWindowRect))
                    {
                        AppContext->Mode = AppMode_Recording;
                    }
                } break;
            }

            ImGui::GetIO().MouseDown[0] = true;
        }

        if(Input->MouseDown)
        {
            switch(AppContext->Mode)
            {
                case AppMode_PreRecording:
                {
                } break;

                case AppMode_Recording:
                {
                    AddTimePoint(AppContext, AppContext->RecordingTime, Input->MouseY);
                } break;

                case AppMode_PlaybackPaused:
                {
                    float CameraTimePosDeltaPixels = (float)(-Input->MouseDeltaX);
                    AppContext->CameraTimePos += GetSecondsPerPixel(AppContext) * CameraTimePosDeltaPixels;
                    if(AppContext->CameraTimePos < 0.0f)
                    {
                        AppContext->CameraTimePos = 0.0f;
                    }
                    else if(AppContext->CameraTimePos > GetRecordingEndTime(AppContext))
                    {
                        AppContext->CameraTimePos = GetRecordingEndTime(AppContext);
                    }
                } break;

                case AppMode_PlaybackPlaying:
                {
                } break;
            }
        }

        if(Input->MouseReleased)
        {
            switch(AppContext->Mode)
            {
                case AppMode_Recording:
                {
                    AddTimePoint(AppContext, AppContext->RecordingTime, Input->MouseY);
                    AppContext->IsCurrentRecordingSaved = false;
                    AppContext->Mode = AppMode_PlaybackPaused;
                } break;

                case AppMode_PlaybackPlaying:
                {
                    AppContext->Mode = AppMode_PlaybackPaused;
                } break;
            }

            ImGui::GetIO().MouseDown[0] = false;
        }

        if(Input->ZoomInPressed)
        {
            IncreasePixelsPerSecond(AppContext);
        }

        if(Input->ZoomOutPressed)
        {
            DecreasePixelsPerSecond(AppContext);
        }

        ImGui::GetIO().MousePos = ImVec2((float)Input->MouseX, (float)Input->FlippedMouseY);
    }

    // render
    {
        // set camera position
        if(AppContext->Mode == AppMode_Recording)
        {
            AppContext->CameraTimePos = AppContext->RecordingTime;
        }
        else if(AppContext->Mode == AppMode_PlaybackPlaying)
        {
            if(AppContext->CameraTimePos > GetRecordingEndTime(AppContext))
            {
                AppContext->CameraTimePos = GetRecordingEndTime(AppContext);
                AppContext->Mode = AppMode_PlaybackPaused;
            }
        }

        // fill vertex buffer
        {
#if 0
#if 0
            if(AppContext->Mode == AppMode_Recording)
            {
                _PushLastTimePoint(AppContext, AppContext->RecordingTime);
            }
#endif

            if(AppContext->TimePointsInVbo < AppContext->TimePointsUsed)
            {
                uint32_t ByteOffsetInVbo = AppContext->TimePointsInVbo * sizeof(v2);
                uint32_t NumBytesToCopy = (AppContext->TimePointsUsed - AppContext->TimePointsInVbo) * sizeof(v2);

                glBindBuffer(GL_ARRAY_BUFFER, AppContext->Vbo);
                glBufferSubData(GL_ARRAY_BUFFER, ByteOffsetInVbo, NumBytesToCopy, 
                                &AppContext->TimePoints[AppContext->TimePointsInVbo]);
            }

#if 0
            if(AppContext->Mode == AppMode_Recording)
            {
                _PopTimePoint(AppContext);
            }

            AppContext->TimePointsInVbo = AppContext->TimePointsUsed;
#endif
#else
            if(AppContext->TimePointsUsed > 2)
            {
                float HalfWindowWidth = (float)AppContext->WindowWidth / 2.0f;
                float CameraTimePosCentered = AppContext->CameraTimePos - (GetSecondsPerPixel(AppContext) * HalfWindowWidth);

                for(uint32_t TimePointIdx = 1; TimePointIdx < AppContext->TimePointsUsed - 1; TimePointIdx++)
                {
                    v2 TimePos1 = AppContext->TimePoints[TimePointIdx - 1];
                    v2 TimePos2 = AppContext->TimePoints[TimePointIdx];
                    v2 TimePos3 = AppContext->TimePoints[TimePointIdx + 1];

                    // convert to screen space
                    v2 ScreenPos1 = V2((TimePos1.X - CameraTimePosCentered) * GetPixelsPerSecond(AppContext), TimePos1.Y);
                    v2 ScreenPos2 = V2((TimePos2.X - CameraTimePosCentered) * GetPixelsPerSecond(AppContext), TimePos2.Y);
                    v2 ScreenPos3 = V2((TimePos3.X - CameraTimePosCentered) * GetPixelsPerSecond(AppContext), TimePos3.Y);

                    two_points Points = CreatePoints(ScreenPos1, ScreenPos2, ScreenPos3, 5.0f);

                    TmpBuffer[4*(TimePointIdx - 1) + 0] = Points.P1.X;
                    TmpBuffer[4*(TimePointIdx - 1) + 1] = Points.P1.Y;
                    TmpBuffer[4*(TimePointIdx - 1) + 2] = Points.P2.X;
                    TmpBuffer[4*(TimePointIdx - 1) + 3] = Points.P2.Y;

                    glBindBuffer(GL_ARRAY_BUFFER, AppContext->Vbo);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(TmpBuffer), TmpBuffer, GL_STREAM_DRAW);
                }

                AppContext->TimePointsInVbo = AppContext->TimePointsUsed - 2;
            }
#endif
        }

        ImGui::NewFrame();

        // draw options window
        {
            v2 GuiWindowSize = {(float)AppContext->WindowWidth / 3.0f, (float)AppContext->WindowHeight / 27.0f};
            v2 GuiWindowPos = {(float)AppContext->WindowWidth - GuiWindowSize.X, 0};

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
                v2 ImGuiWindowPos = {(float)ImGuiWindowPosNative.x, (float)ImGuiWindowPosNative.y};
                v2 ImGuiWindowSize = {(float)ImGuiWindowSizeNative.x, (float)ImGuiWindowSizeNative.y};
                AppContext->ImGuiWindowRect = RectFromPosSize(ImGuiWindowPos, ImGuiWindowSize);

                ImGui::PushStyleColor(ImGuiCol_Button, ImColor(0.3f, 0.3f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(0.3f, 0.3f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(0.3f, 0.3f, 0.3f));
                if(ImGui::Button("New"))
                {
                    if(AppContext->Mode == AppMode_PlaybackPaused)
                    {
                        AppContext->TimePointsUsed = 0;
                        AppContext->TimePointsInVbo = 0;
                        AppContext->RecordingTime = 0;
                        AppContext->Mode = AppMode_PreRecording;
                    }
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine();
                if(ImGui::Button("Save"))
                {
                    if(AppContext->Mode == AppMode_PlaybackPaused &&
                       !AppContext->IsCurrentRecordingSaved)
                    {
                        char *Filename = "feedback_output.fbk";
                        FILE *File = fopen(Filename, "wb");
                        if(File == NULL)
                        {
                            AppContext->Log("Failed to open file: %s\n", Filename);
                            Assert(0);
                        }

                        // TOUCH_UP causes us to push a time point, so no need to worry about dummies
                        fwrite((void *)AppContext->TimePoints, sizeof(v2), AppContext->TimePointsCapacity, File);
                        fwrite((void *)&AppContext->TimePointsUsed, sizeof(uint32_t), 1, File);
                        fclose(File);
                    }
                }

                ImGui::SameLine();
                if(ImGui::Button("Load"))
                {
                    if(AppContext->Mode == AppMode_PreRecording ||
                       AppContext->Mode == AppMode_PlaybackPaused)
                    {
                        char *Filename = "feedback_output.fbk";
                        FILE *File = fopen(Filename, "rb");
                        if(File == NULL)
                        {
                            AppContext->Log("Failed to open file: %s\n", Filename);
                            Assert(0);
                        }

                        fread((void *)AppContext->TimePoints, sizeof(v2), AppContext->TimePointsCapacity, File);
                        fread((void *)&AppContext->TimePointsUsed, sizeof(uint32_t), 1, File);
                        fclose(File);

                        AppContext->CameraTimePos = 0.0f;
                        SetPixelsPerSecond(AppContext, AppContext->DefaultPixelsPerSecond);

                        // ensure the newly loaded time points will be uploaded to the vertex buffer
                        AppContext->TimePointsInVbo = 0;

                        AppContext->IsCurrentRecordingSaved = true;
                        AppContext->Mode = AppMode_PlaybackPaused;
                    }
                }

                ImGui::SameLine();
                if(ImGui::Button("Play"))
                {
                    if(AppContext->Mode == AppMode_PlaybackPaused)
                    {
                        AppContext->Mode = AppMode_PlaybackPlaying;
                    }
                }
            }
            ImGui::End();
        }

        // draw debug window
        {
            v2 GuiWindowSize = {(float)AppContext->WindowWidth / 3.0f, (float)AppContext->WindowHeight / 8.0f};
            v2 GuiWindowPos = {0, 0};

            ImGui::SetNextWindowPos(ImVec2(GuiWindowPos.X, GuiWindowPos.Y));
            ImGui::SetNextWindowSize(ImVec2(GuiWindowSize.X, GuiWindowSize.Y));
            if(ImGui::Begin("debug window", NULL,
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoResize))
            {
                ImGui::Value("CameraTimePos", AppContext->CameraTimePos);
                ImGui::Checkbox("Draw Points", (bool *)&AppContext->ShouldDrawPoints);
            }
            ImGui::End();
        }

#if 0
        ImGui::ShowTestWindow();
#endif

        // draw app
        {
#if 0
            float HalfWindowWidth = (float)AppContext->WindowWidth / 2.0f;
            float CameraTimePosCentered = AppContext->CameraTimePos - (GetSecondsPerPixel(AppContext) * HalfWindowWidth);
            glUniform1f(GetUniformLocation(AppContext, AppContext->ShaderProgram, "CameraTimePos"), CameraTimePosCentered);
            glUniform1f(GetUniformLocation(AppContext, AppContext->ShaderProgram, "PixelsPerSecond"), GetPixelsPerSecond(AppContext));
#endif

            glClearColor(EXPANDV4(AppContext->ClearColor));
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glUseProgram(AppContext->ShaderProgram);
            glBindVertexArray(AppContext->Vao);

            uint32_t TimePointsToRender = AppContext->TimePointsInVbo;

            //glUniform4fv(GetUniformLocation(AppContext, AppContext->ShaderProgram, "ShapeColor"), 1, AppContext->LineColor.Components);
            glDrawArrays(GL_LINE_STRIP, 0, TimePointsToRender);

            if(AppContext->ShouldDrawPoints)
            {
                glUniform4fv(GetUniformLocation(AppContext, AppContext->ShaderProgram, "ShapeColor"), 1, AppContext->PointColor.Components);
                glDrawArrays(GL_POINTS, 0, TimePointsToRender);
            }

            ImGui::Render();
            ImGui_RenderFunction(AppContext, ImGui::GetDrawData());
        }
    }
}
//}
