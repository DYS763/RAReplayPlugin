#include <Windows.h>
#include <functional> 
#include <gl/GL.h>
#include "kiero/kiero.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_opengl3.h"  // 改为OpenGL3的头文件

#define _CAST(t,v)	reinterpret_cast<t>(v)
#define _VOID_1(v)	std::function<void(v)>
#define _VOID_2(v)	_VOID_1(_VOID_1(v))

typedef BOOL(__stdcall* wglSwapBuffers_t) (
    HDC hDc
    );

typedef LRESULT(CALLBACK* WNDPROC) (
    IN  HWND   hwnd,
    IN  UINT   uMsg,
    IN  WPARAM wParam,
    IN  LPARAM lParam
    );

extern LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

extern void RenderMain();

namespace ImGuiHook
{
    // Original functions variable
    static WNDPROC			o_WndProc;
    static wglSwapBuffers_t o_wglSwapBuffers;

    // Global variable
    static HGLRC      g_WglContext;
    static bool	      initImGui = false;
    static _VOID_1()  RenderMain;

    // WndProc callback ImGui handler
    LRESULT CALLBACK h_WndProc(
        const HWND	hWnd,
        UINT		uMsg,
        WPARAM		wParam,
        LPARAM		lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) return true;

        return CallWindowProc(o_WndProc, hWnd, uMsg, wParam, lParam);
    }

    // Helper function
    void ExitStatus(bool* status, bool value)
    {
        if (status) *status = value;
    }

    HWND hWnd = nullptr;

    // Initialisation for ImGui
    void InitOpenGL3(  // 改为OpenGL3
        IN  HDC	  hDc,
        OUT bool* init,
        OUT bool* status)
    {
        if (WindowFromDC(hDc) == hWnd && *init) return;
        auto tStatus = true;

        hWnd = WindowFromDC(hDc);
        auto wLPTR = SetWindowLongPtr(hWnd, GWLP_WNDPROC, _CAST(LONG_PTR, h_WndProc));

        if (*init)
        {
            ImGui_ImplWin32_Init(hWnd);
            ImGui_ImplOpenGL3_Init();  // 改为OpenGL3初始化
            return;
        }

        if (!wLPTR) return ExitStatus(status, false);

        o_WndProc = _CAST(WNDPROC, wLPTR);
        g_WglContext = wglCreateContext(hDc);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // 设置OpenGL版本
        ImGui_ImplOpenGL3_Init("#version 130");  // 添加着色器版本

        tStatus &= ImGui_ImplWin32_Init(hWnd);
        tStatus &= ImGui_ImplOpenGL3_Init("#version 130");  // 再次初始化确保设置

        *init = true;
        return ExitStatus(status, tStatus);
    }

    // Generic ImGui renderer for Win32 backend
    void RenderWin32(
        IN  std::function<void()> render)
    {
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        render();

        ImGui::EndFrame();
        ImGui::Render();
    }

    // Generic ImGui renderer for OpenGL3 backend
    void RenderOpenGL3(  // 改为OpenGL3
        IN  HGLRC 	  WglContext,
        IN  HDC		  hDc,
        IN  _VOID_2() render,
        IN  _VOID_1() render_inner,
        OUT bool* status)
    {
        auto tStatus = true;

        auto o_WglContext = wglGetCurrentContext();
        tStatus &= wglMakeCurrent(hDc, WglContext);

        ImGui_ImplOpenGL3_NewFrame();  // 改为OpenGL3
        render(render_inner);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());  // 改为OpenGL3

        tStatus &= wglMakeCurrent(hDc, o_WglContext);

        return ExitStatus(status, tStatus);
    }

    // Hooked wglSwapBuffers function
    BOOL __stdcall h_wglSwapBuffers(
        IN  HDC hDc)
    {
        InitOpenGL3(hDc, &initImGui, nullptr);  // 改为OpenGL3
        RenderOpenGL3(g_WglContext, hDc, RenderWin32, RenderMain, nullptr);  // 改为OpenGL3

        return o_wglSwapBuffers(hDc);
    }

    // Function to get the pointer of wglSwapBuffers
    wglSwapBuffers_t* get_wglSwapBuffers()
    {
        auto hMod = GetModuleHandleA("OPENGL32.dll");
        if (!hMod) return nullptr;

        return (wglSwapBuffers_t*)GetProcAddress(hMod, "wglSwapBuffers");
    }

    // Initialise hook
    bool InitHook()
    {
        if (kiero::init(kiero::RenderType::OpenGL) == kiero::Status::Success)
            return kiero::bind(get_wglSwapBuffers(), (void**)&o_wglSwapBuffers, h_wglSwapBuffers) == kiero::Status::Success;
        return false;
    }

    // Main load function
    bool Load(
        IN  _VOID_1() render)
    {
        RenderMain = render;
        return InitHook();
    }

    // Main unload function
    void Unload()
    {
        kiero::shutdown();
    }
}
