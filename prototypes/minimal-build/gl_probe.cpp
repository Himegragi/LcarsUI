// THROWAWAY native build/GPU probe, not the LcarsUI renderer.
#include <windows.h>
#include "glcorearb.h"
#include "core.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

static bool quitting = false;
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static LRESULT CALLBACK wnd_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CLOSE) { quitting = true; return 0; }
    if (msg == WM_KEYDOWN && w == VK_ESCAPE) { quitting = true; return 0; }
    return DefWindowProcW(window, msg, w, l);
}

static PROC symbol(const char* name) {
    PROC p = wglGetProcAddress(name);
    if (!p || p == reinterpret_cast<PROC>(1) || p == reinterpret_cast<PROC>(2)
           || p == reinterpret_cast<PROC>(3) || p == reinterpret_cast<PROC>(-1)) {
        p = GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name);
    }
    if (!p) throw std::runtime_error(std::string("Missing GL symbol: ") + name);
    return p;
}

// One explicit, checked symbol table, shared by all probe rendering calls.
#define GL_FUNCTIONS(X) \
 X(PFNGLGETSTRINGPROC, GetString) \
 X(PFNGLGETINTEGERVPROC, GetIntegerv) \
 X(PFNGLGETERRORPROC, GetError) \
 X(PFNGLVIEWPORTPROC, Viewport) \
 X(PFNGLCLEARCOLORPROC, ClearColor) \
 X(PFNGLCLEARPROC, Clear) \
 X(PFNGLENABLEPROC, Enable) \
 X(PFNGLDISABLEPROC, Disable) \
 X(PFNGLSCISSORPROC, Scissor) \
 X(PFNGLBLENDFUNCPROC, BlendFunc) \
 X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
 X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
 X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
 X(PFNGLCREATESHADERPROC, CreateShader) \
 X(PFNGLSHADERSOURCEPROC, ShaderSource) \
 X(PFNGLCOMPILESHADERPROC, CompileShader) \
 X(PFNGLGETSHADERIVPROC, GetShaderiv) \
 X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
 X(PFNGLDELETESHADERPROC, DeleteShader) \
 X(PFNGLCREATEPROGRAMPROC, CreateProgram) \
 X(PFNGLATTACHSHADERPROC, AttachShader) \
 X(PFNGLLINKPROGRAMPROC, LinkProgram) \
 X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
 X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
 X(PFNGLUSEPROGRAMPROC, UseProgram) \
 X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
 X(PFNGLGENTEXTURESPROC, GenTextures) \
 X(PFNGLBINDTEXTUREPROC, BindTexture) \
 X(PFNGLTEXIMAGE2DPROC, TexImage2D) \
 X(PFNGLTEXPARAMETERIPROC, TexParameteri) \
 X(PFNGLDELETETEXTURESPROC, DeleteTextures) \
 X(PFNGLDRAWARRAYSPROC, DrawArrays) \
 X(PFNGLREADPIXELSPROC, ReadPixels) \
 X(PFNGLFINISHPROC, Finish)
struct GL {
#define FIELD(type, name) type name{};
    GL_FUNCTIONS(FIELD)
#undef FIELD
    void load() {
#define LOAD(type, name) name = reinterpret_cast<type>(symbol("gl" #name));
        GL_FUNCTIONS(LOAD)
#undef LOAD
    }
};

struct Context {
    HWND window{};
    HDC dc{};
    HGLRC context{};
    HGLRC bootstrap{};
    GL gl;
    ~Context() { release(); }
    bool release() {
        bool ok = true;
        if (context || bootstrap) ok = wglMakeCurrent(nullptr, nullptr) != FALSE;
        if (context) { ok = (wglDeleteContext(context) != FALSE) && ok; context = nullptr; }
        if (bootstrap) { ok = (wglDeleteContext(bootstrap) != FALSE) && ok; bootstrap = nullptr; }
        if (dc) { ok = (ReleaseDC(window, dc) != 0) && ok; dc = nullptr; }
        if (window) { ok = (DestroyWindow(window) != FALSE) && ok; window = nullptr; }
        return ok;
    }
    void open() {
        window = CreateWindowExW(0, L"LcarsBuildProbe", L"LcarsUI PROTOTYPE | GL 3.3 core | clipped alpha texture | Esc closes",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 640,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "CreateWindow failed");
        dc = GetDC(window);
        require(dc != nullptr, "GetDC failed");
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cAlphaBits = 8;
        int format = ChoosePixelFormat(dc, &pfd);
        require(format != 0 && SetPixelFormat(dc, format, &pfd), "Pixel format failed");
        bootstrap = wglCreateContext(dc);
        require(bootstrap && wglMakeCurrent(dc, bootstrap), "Bootstrap WGL context failed");
        using CreateAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
        auto create = reinterpret_cast<CreateAttribs>(symbol("wglCreateContextAttribsARB"));
        // WGL_CONTEXT_MAJOR_VERSION_ARB, MINOR_VERSION_ARB, PROFILE_MASK_ARB, CORE_PROFILE_BIT_ARB.
        const int attribs[] = {0x2091, 3, 0x2092, 3, 0x9126, 1, 0};
        context = create(dc, nullptr, attribs);
        require(context != nullptr, "OpenGL 3.3 core creation failed");
        require(wglMakeCurrent(nullptr, nullptr), "Detach bootstrap failed");
        require(wglDeleteContext(bootstrap), "Delete bootstrap failed");
        bootstrap = nullptr;
        require(wglMakeCurrent(dc, context), "Make core context current failed");
        gl.load();
        int major{}, minor{}, profile{};
        gl.GetIntegerv(GL_MAJOR_VERSION, &major); gl.GetIntegerv(GL_MINOR_VERSION, &minor);
        gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
        require((major > 3 || (major == 3 && minor >= 3)) && (profile & GL_CONTEXT_CORE_PROFILE_BIT), "Wrong core profile/version");
        std::cout << "GL_VENDOR=" << gl.GetString(GL_VENDOR) << '\n'
                  << "GL_RENDERER=" << gl.GetString(GL_RENDERER) << '\n'
                  << "GL_VERSION=" << gl.GetString(GL_VERSION) << '\n'
                  << "GLSL=" << gl.GetString(GL_SHADING_LANGUAGE_VERSION) << '\n'
                  << "profile=" << profile << " dpi=" << GetDpiForWindow(window) << '\n';
        ShowWindow(window, SW_SHOWNORMAL);
    }
};

static GLuint shader(GL& g, GLenum kind, const char* source) {
    GLuint s = g.CreateShader(kind); g.ShaderSource(s, 1, &source, nullptr); g.CompileShader(s);
    GLint ok{}; g.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]{}; g.GetShaderInfoLog(s, sizeof(log), nullptr, log); g.DeleteShader(s); throw std::runtime_error(log); }
    return s;
}
struct Scene {
    GL& g;
    GLuint program{}, vao{}, texture{};
    explicit Scene(GL& gl) : g(gl) {}
    ~Scene() {
        if (texture) g.DeleteTextures(1, &texture);
        if (vao) g.DeleteVertexArrays(1, &vao);
        if (program) g.DeleteProgram(program);
    }
    void init() {
        const char* vertex = R"(#version 330 core
out vec2 uv;
void main(){
    vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(p[gl_VertexID],0,1); uv=(p[gl_VertexID]+1)*0.5;
})";
        const char* fragment = R"(#version 330 core
in vec2 uv; out vec4 color; uniform sampler2D image;
void main(){color=texture(image,uv);})";
        auto vs=shader(g, GL_VERTEX_SHADER, vertex), fs=shader(g, GL_FRAGMENT_SHADER, fragment);
        program=g.CreateProgram(); g.AttachShader(program,vs); g.AttachShader(program,fs); g.LinkProgram(program);
        g.DeleteShader(vs); g.DeleteShader(fs);
        GLint ok{}; g.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { char log[4096]{}; g.GetProgramInfoLog(program,sizeof(log),nullptr,log); throw std::runtime_error(log); }
        g.GenVertexArrays(1,&vao); g.BindVertexArray(vao);
        g.GenTextures(1,&texture); g.BindTexture(GL_TEXTURE_2D,texture);
        const unsigned char pixels[] = {255,128,32,128, 255,128,32,128, 255,128,32,128, 255,128,32,128};
        g.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    void draw(int width,int height) {
        g.Viewport(0,0,width,height); g.Disable(GL_SCISSOR_TEST);
        g.Disable(GL_FRAMEBUFFER_SRGB); g.Disable(GL_DITHER);
        g.ClearColor(0.1f,0.2f,0.3f,1); g.Clear(GL_COLOR_BUFFER_BIT);
        g.Enable(GL_BLEND); g.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        g.Enable(GL_SCISSOR_TEST); g.Scissor(width/4,height/4,width/2,height/2);
        g.UseProgram(program); g.BindVertexArray(vao); g.BindTexture(GL_TEXTURE_2D,texture);
        g.DrawArrays(GL_TRIANGLES,0,3); g.Disable(GL_SCISSOR_TEST);
    }
};

static void capture(GL& g, int width, int height, const char* path) {
    std::vector<unsigned char> bytes(static_cast<size_t>(width)*height*4);
    g.ReadPixels(0,0,width,height,GL_BGRA,GL_UNSIGNED_BYTE,bytes.data());
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info); file.bfSize=file.bfOffBits+static_cast<DWORD>(bytes.size());
    info.biSize=sizeof(info); info.biWidth=width; info.biHeight=height; info.biPlanes=1; info.biBitCount=32; info.biSizeImage=static_cast<DWORD>(bytes.size());
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file),sizeof(file)); out.write(reinterpret_cast<const char*>(&info),sizeof(info));
    out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); require(bool(out),"Capture failed");
}
static void verify(GL& g, int width,int height) {
    auto check = [&](int x,int y,std::array<int,3> expected) {
        std::array<unsigned char,4> p{}; g.ReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());
        std::cout << "pixel(" << x << ',' << y << ")=" << int(p[0]) << ',' << int(p[1]) << ',' << int(p[2]) << '\n';
        for(int i=0;i<3;++i) require(std::abs(int(p[i])-expected[i])<=3,"Scissor/alpha pixel mismatch");
    };
    check(width/8,height/2,{26,51,77});
    check(width/2,height/2,{141,90,54});
    check(width*7/8,height/2,{26,51,77});
    require(g.GetError()==GL_NO_ERROR,"OpenGL error");
}
int main(int argc,char** argv) {
    try {
        std::cout << "THROWAWAY PROTOTYPE cxx26=" << verified_cxx26_value() << '\n';
        std::cout << "__cplusplus=" << __cplusplus << " pack_indexing=" << __cpp_pack_indexing << '\n';
#ifdef __clang__
        std::cout << "compiler=clang " << __clang_version__ << '\n';
#else
        std::cout << "compiler=MSVC " << _MSC_FULL_VER << '\n';
#endif
        bool interactive=argc>1 && std::string(argv[1])=="--interactive";
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW wc{}; wc.style=CS_OWNDC; wc.lpfnWndProc=wnd_proc; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"LcarsBuildProbe"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        require(RegisterClassW(&wc)!=0,"RegisterClass failed");
        for(int cycle=0;cycle<(interactive?1:2);++cycle) {
            Context c; c.open();
            {
                Scene scene(c.gl); scene.init();
                for(int phase=0;phase<2;++phase) {
                    if(phase) require(SetWindowPos(c.window,nullptr,0,0,640,480,SWP_NOMOVE|SWP_NOZORDER),"Resize failed");
                    MSG msg{}; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                    RECT rect{}; GetClientRect(c.window,&rect);
                    std::cout << "cycle=" << cycle << " client=" << rect.right << 'x' << rect.bottom << '\n';
                    scene.draw(rect.right,rect.bottom); c.gl.Finish(); verify(c.gl,rect.right,rect.bottom);
                    capture(c.gl,rect.right,rect.bottom,phase?"probe-resized.bmp":"probe-initial.bmp");
                    require(SwapBuffers(c.dc),"SwapBuffers failed");
                }
                while(interactive && !quitting) {
                    MSG msg{}; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                    RECT rect{}; GetClientRect(c.window,&rect);
                    if(rect.right>0 && rect.bottom>0) { scene.draw(rect.right,rect.bottom); SwapBuffers(c.dc); }
                    Sleep(16);
                }
            }
            require(c.gl.GetError()==GL_NO_ERROR,"GL cleanup error");
            require(c.release(),"WGL/window cleanup failed");
            std::cout << "cycle " << cycle << " cleanup=PASS\n";
        }
        require(UnregisterClassW(wc.lpszClassName,wc.hInstance),"UnregisterClass failed");
        std::cout << "PASS: C++26, core context, shaders, texture, alpha blend, scissor, resize, cleanup/recreation\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
