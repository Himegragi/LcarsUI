#include "renderer_opengl.hpp"
#include "glcorearb.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
namespace lcars::detail {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static PROC symbol(const char* name) {
    PROC p = wglGetProcAddress(name);
    if (!p || p == reinterpret_cast<PROC>(1) || p == reinterpret_cast<PROC>(2)
           || p == reinterpret_cast<PROC>(3) || p == reinterpret_cast<PROC>(-1)) {
        p = GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name);
    }
    if (!p) throw std::runtime_error(std::string("Missing GL symbol: ") + name);
    return p;
}

// Context-local checked symbol table; no process-global GL function state.
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
        GLuint vs=0,fs=0;
        try {
            vs=shader(g,GL_VERTEX_SHADER,vertex);fs=shader(g,GL_FRAGMENT_SHADER,fragment);
            program=g.CreateProgram();g.AttachShader(program,vs);g.AttachShader(program,fs);g.LinkProgram(program);
        } catch (...) {if(vs)g.DeleteShader(vs);if(fs)g.DeleteShader(fs);throw;}
        g.DeleteShader(vs);g.DeleteShader(fs);
        GLint ok{}; g.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { char log[4096]{}; g.GetProgramInfoLog(program,sizeof(log),nullptr,log); throw std::runtime_error(log); }
        g.GenVertexArrays(1,&vao); g.BindVertexArray(vao);
        g.GenTextures(1,&texture); g.BindTexture(GL_TEXTURE_2D,texture);
        g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }

};

}

struct Renderer::Impl {
    HWND window;
    HDC dc=nullptr;
    HGLRC context=nullptr,bootstrap=nullptr;
    GL gl;
    std::unique_ptr<Scene> scene;
    explicit Impl(HWND handle):window(handle) {
        try {
            dc=GetDC(window);require(dc!=nullptr,"GetDC failed");
            PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;
            pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cAlphaBits=8;
            int format=ChoosePixelFormat(dc,&pfd);require(format&&SetPixelFormat(dc,format,&pfd),"Pixel format failed");
            bootstrap=wglCreateContext(dc);require(bootstrap&&wglMakeCurrent(dc,bootstrap),"WGL bootstrap failed");
            using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);
            auto create=reinterpret_cast<Create>(symbol("wglCreateContextAttribsARB"));
            const int attributes[]={0x2091,3,0x2092,3,0x9126,1,0};context=create(dc,nullptr,attributes);
            require(context!=nullptr,"OpenGL 3.3 core is required");
            require(wglMakeCurrent(nullptr,nullptr)!=FALSE,"WGL detach failed");
            require(wglDeleteContext(bootstrap)!=FALSE,"WGL bootstrap cleanup failed");bootstrap=nullptr;
            require(wglMakeCurrent(dc,context)!=FALSE,"WGL activation failed");gl.load();
            GLint major=0,minor=0,profile=0;gl.GetIntegerv(GL_MAJOR_VERSION,&major);gl.GetIntegerv(GL_MINOR_VERSION,&minor);gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
            require((major>3||(major==3&&minor>=3))&&(profile&GL_CONTEXT_CORE_PROFILE_BIT),"Wrong OpenGL context");
            std::cout<<"OpenGL "<<gl.GetString(GL_VERSION)<<" | "<<gl.GetString(GL_RENDERER)<<'\n';
            scene=std::make_unique<Scene>(gl);scene->init();
        } catch(...) {release();throw;}
    }
    void release(){scene.reset();if(context||bootstrap)wglMakeCurrent(nullptr,nullptr);if(context)wglDeleteContext(context);if(bootstrap)wglDeleteContext(bootstrap);if(dc)ReleaseDC(window,dc);context=bootstrap=nullptr;dc=nullptr;}
    ~Impl(){release();}
    void upload(const Canvas& canvas){
        auto& g=gl;g.Viewport(0,0,canvas.width,canvas.height);g.Disable(GL_SCISSOR_TEST);g.Disable(GL_BLEND);g.Disable(GL_FRAMEBUFFER_SRGB);g.Disable(GL_DITHER);
        g.BindTexture(GL_TEXTURE_2D,scene->texture);g.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,canvas.width,canvas.height,0,GL_RGBA,GL_UNSIGNED_BYTE,canvas.pixels.data());
        g.UseProgram(scene->program);g.BindVertexArray(scene->vao);g.DrawArrays(GL_TRIANGLES,0,3);
        require(g.GetError()==GL_NO_ERROR,"OpenGL frame failed");
    }
};
Renderer::Renderer(HWND window):impl_(std::make_unique<Impl>(window)){}
Renderer::~Renderer()=default;
void Renderer::draw(const Canvas& canvas){impl_->upload(canvas);require(SwapBuffers(impl_->dc)!=FALSE,"SwapBuffers failed");}
void Renderer::capture(const Canvas& canvas,const std::filesystem::path& path){
    impl_->upload(canvas);auto& gl=impl_->gl;gl.Finish();
    std::vector<unsigned char> actual(canvas.pixels.size());
    gl.ReadPixels(0,0,canvas.width,canvas.height,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    for(std::size_t i=0;i<actual.size();++i)if(i%4!=3)require(std::abs(int(actual[i])-int(canvas.pixels[i]))<=1,"GPU readback does not match rasterized frame");
    for(std::size_t i=0;i<actual.size();i+=4)std::swap(actual[i],actual[i+2]);
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+static_cast<DWORD>(actual.size());
    info.biSize=sizeof(info);info.biWidth=canvas.width;info.biHeight=canvas.height;info.biPlanes=1;info.biBitCount=32;info.biSizeImage=static_cast<DWORD>(actual.size());
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));out.write(reinterpret_cast<const char*>(actual.data()),actual.size());require(bool(out),"Screenshot write failed");
    std::cout<<"PASS GPU readback "<<canvas.width<<'x'<<canvas.height<<'\n';
}
}
