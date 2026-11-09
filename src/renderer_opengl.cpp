#include "utf.hpp"
#include "renderer_opengl.hpp"
#include "glcorearb.h"
#include <stdexcept>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <unordered_map>

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
 X(PFNGLGENQUERIESPROC, GenQueries) \
 X(PFNGLDELETEQUERIESPROC, DeleteQueries) \
 X(PFNGLBEGINQUERYPROC, BeginQuery) \
 X(PFNGLENDQUERYPROC, EndQuery) \
 X(PFNGLGETQUERYOBJECTUI64VPROC, GetQueryObjectui64v) \
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
 X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate) \
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
 X(PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D) \
 X(PFNGLTEXPARAMETERIPROC, TexParameteri) \
 X(PFNGLDELETETEXTURESPROC, DeleteTextures) \
 X(PFNGLDRAWARRAYSPROC, DrawArrays) \
 X(PFNGLREADPIXELSPROC, ReadPixels) \
 X(PFNGLFINISHPROC, Finish) \
 X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
 X(PFNGLUNIFORM1IPROC, Uniform1i) \
 X(PFNGLUNIFORM1FPROC, Uniform1f) \
 X(PFNGLUNIFORM2FPROC, Uniform2f) \
 X(PFNGLUNIFORM4FPROC, Uniform4f) \
 X(PFNGLPIXELSTOREIPROC, PixelStorei) \
 X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
 X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
 X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
 X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
 X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)
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
    GLuint program{}, vao{}, textures[4]{}, fbo{}, target{};
    int atlas_width=0, atlas_height=0;
    explicit Scene(GL& gl) : g(gl) {}
    ~Scene() {
        if (fbo) g.DeleteFramebuffers(1,&fbo);
        if (target) g.DeleteTextures(1,&target);
        g.DeleteTextures(4,textures);
        if (vao) g.DeleteVertexArrays(1,&vao);
        if (program) g.DeleteProgram(program);
    }
    void init(const std::string& fragment_override) {
        const char* vertex = R"(#version 330 core
uniform vec2 screen; uniform vec4 rect;
out vec2 pixel;
void main(){
 vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
 pixel=rect.xy+corners[gl_VertexID]*rect.zw;
 gl_Position=vec4(pixel.x/screen.x*2.0-1.0,1.0-pixel.y/screen.y*2.0,0,1);
})";
        const char* fragment = R"(#version 330 core
in vec2 pixel; out vec4 outputColor;
uniform vec4 rect; uniform vec4 tint; uniform float radius; uniform int kind;
uniform vec2 atlasOrigin; uniform sampler2D atlas;
void main(){
 float coverage=1.0;
 if(kind==1){
   float r=min(radius,min(floor(rect.z/2.0),floor(rect.w/2.0)));
   vec2 d=max(vec2(0),vec2(r)-min(pixel-rect.xy,rect.xy+rect.zw-pixel));
   if(d.x>0.0 && d.y>0.0) coverage=floor(clamp(r+0.5-length(d),0.0,1.0)*255.0)/255.0;
 } else if(kind==2) {
   coverage=texelFetch(atlas,ivec2(atlasOrigin+floor(pixel-rect.xy)),0).r;
 }
 outputColor=vec4(tint.rgb,tint.a*coverage);
})";
        GLuint vs=0,fs=0;
        try { vs=shader(g,GL_VERTEX_SHADER,vertex); fs=shader(g,GL_FRAGMENT_SHADER,fragment_override.empty() ? fragment : fragment_override.c_str());
            program=g.CreateProgram();g.AttachShader(program,vs);g.AttachShader(program,fs);g.LinkProgram(program);
        } catch (...) {if(vs)g.DeleteShader(vs);if(fs)g.DeleteShader(fs);throw;}
        g.DeleteShader(vs);g.DeleteShader(fs);
        GLint ok{};g.GetProgramiv(program,GL_LINK_STATUS,&ok);
        if(!ok){char log[4096]{};g.GetProgramInfoLog(program,sizeof(log),nullptr,log);throw std::runtime_error(log);}
        g.GenVertexArrays(1,&vao);g.GenTextures(4,textures);
        for(auto texture : textures){g.BindTexture(GL_TEXTURE_2D,texture);
            g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
        g.GenTextures(1,&target);g.GenFramebuffers(1,&fbo);
    }
};

}

struct Renderer::Impl {
    HWND window;
    HDC dc=nullptr;
    HGLRC context=nullptr,bootstrap=nullptr;
    GL gl;
    std::unique_ptr<Scene> scene;
    RendererTestHooks* hooks;
    int frames = 0;
    PFNGLGETGRAPHICSRESETSTATUSPROC reset_status = nullptr;
    RendererCapabilities caps;
    std::function<void(RenderDiagnostic)> sink;
    PFNGLDEBUGMESSAGECALLBACKPROC debug_callback = nullptr;
    PFNGLPUSHDEBUGGROUPPROC push_group = nullptr;
    PFNGLPOPDEBUGGROUPPROC pop_group = nullptr;
    std::string current_group;
    static void APIENTRY receive(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user) {
        auto& self=*static_cast<const Impl*>(user);
        if (!self.sink) return;
        try {
            RenderDiagnostic event; event.backend=u"OpenGL"; event.native_code=id;
            event.native_message.assign(message, length > 0 ? static_cast<std::size_t>(length) : 0);
            event.severity=severity==GL_DEBUG_SEVERITY_HIGH ? DiagnosticSeverity::error : severity==GL_DEBUG_SEVERITY_MEDIUM ? DiagnosticSeverity::warning : DiagnosticSeverity::info;
            event.category=type==GL_DEBUG_TYPE_ERROR ? DiagnosticCategory::api_misuse : type==GL_DEBUG_TYPE_PERFORMANCE ? DiagnosticCategory::performance : DiagnosticCategory::other;
            event.component_id=self.current_group;
            event.backend_detail="OpenGL:"+std::to_string(source)+":"+std::to_string(type)+":"+std::to_string(id);
            self.sink(std::move(event));
        } catch (...) {}
    }
    explicit Impl(HWND handle, RendererTestHooks* test, DebugContext debug, std::function<void(RenderDiagnostic)> callback):window(handle), hooks(test), sink(std::move(callback)) {
        try {
            dc=GetDC(window);require(dc!=nullptr,"GetDC failed");
            PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;
            pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cAlphaBits=8;
            int format=GetPixelFormat(dc); if (!format) { format=ChoosePixelFormat(dc,&pfd); require(format&&SetPixelFormat(dc,format,&pfd),"Pixel format failed"); }
            bootstrap=wglCreateContext(dc);require(bootstrap&&wglMakeCurrent(dc,bootstrap),"WGL bootstrap failed");
            using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);
            auto get_string = reinterpret_cast<PFNGLGETSTRINGPROC>(symbol("glGetString"));
            auto version = reinterpret_cast<const char*>(get_string(GL_VERSION));
            auto vendor = reinterpret_cast<const char*>(get_string(GL_RENDERER));
            std::string actual = std::string(version ? version : "unknown") + " / " + (vendor ? vendor : "unknown");
            auto to16=[](const std::string& v) { return utf16_from_utf8(v); };
            if (hooks && hooks->fail_context_33) throw StartupError{{StartupFailureKind::context_version,u"OpenGL 3.3 core",u"2.1",u"OpenGL 3.3 core is required"}};
            Create create = nullptr;
            try { create=reinterpret_cast<Create>(symbol("wglCreateContextAttribsARB")); } catch (...) { throw StartupError{{StartupFailureKind::context_version,u"OpenGL 3.3 core",to16(actual),u"OpenGL 3.3 core is required"}}; }
            using Extensions=const char*(WINAPI*)(HDC);
            auto extproc=wglGetProcAddress("wglGetExtensionsStringARB");
            auto ext=extproc ? reinterpret_cast<Extensions>(extproc)(dc) : nullptr;
            std::string extensions=ext ? ext : "";
            bool robust=!(hooks && hooks->hide_robustness) && extensions.find("WGL_ARB_create_context_robustness")!=std::string::npos;
#ifdef NDEBUG
            bool debug_enabled=debug==DebugContext::on;
#else
            bool debug_enabled=debug!=DebugContext::off;
#endif
            int flags=(debug_enabled ? 0x0001 : 0) | (robust ? 0x0004 : 0);
            std::vector<int> attributes={0x2091,3,0x2092,3,0x9126,1,0x2094,flags};
            if (robust) { attributes.push_back(0x8256); attributes.push_back(0x8252); }
            attributes.push_back(0);
            context=create(dc,nullptr,attributes.data());
            if (!context) {
                int major=0, minor=0; if (version) std::sscanf(version,"%d.%d",&major,&minor);
                auto kind=(major<3 || (major==3 && minor<3)) ? StartupFailureKind::context_version : StartupFailureKind::context_creation;
                throw StartupError{{kind,u"OpenGL 3.3 core",to16(actual),u"Unable to create OpenGL 3.3 core context"}};
            }
            require(wglMakeCurrent(nullptr,nullptr)!=FALSE,"WGL detach failed");
            require(wglDeleteContext(bootstrap)!=FALSE,"WGL bootstrap cleanup failed");bootstrap=nullptr;
            require(wglMakeCurrent(dc,context)!=FALSE,"WGL activation failed");gl.load();
            GLint major=0,minor=0,profile=0;gl.GetIntegerv(GL_MAJOR_VERSION,&major);gl.GetIntegerv(GL_MINOR_VERSION,&minor);gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
            require((major>3||(major==3&&minor>=3))&&(profile&GL_CONTEXT_CORE_PROFILE_BIT),"Wrong OpenGL context");
            using SwapInterval = BOOL (WINAPI*)(int);
            auto interval = wglGetProcAddress("wglSwapIntervalEXT");
            if (interval && interval != reinterpret_cast<PROC>(1) && interval != reinterpret_cast<PROC>(2) &&
                interval != reinterpret_cast<PROC>(3) && interval != reinterpret_cast<PROC>(-1))
                reinterpret_cast<SwapInterval>(interval)(1);
            auto string16=[&](GLenum key) { auto p=reinterpret_cast<const char*>(gl.GetString(key)); return to16(p ? p : ""); };
            caps.version=string16(GL_VERSION); caps.vendor=string16(GL_VENDOR); caps.renderer=string16(GL_RENDERER);
            caps.debug_context=debug_enabled;
            if (debug_enabled && !(hooks && hooks->hide_khr_debug)) {
                using GetStringi=PFNGLGETSTRINGIPROC;
                auto get_string_i=reinterpret_cast<GetStringi>(symbol("glGetStringi"));
                GLint count=0; gl.GetIntegerv(GL_NUM_EXTENSIONS,&count);
                bool supported=major>4 || (major==4 && minor>=3);
                for (GLint i=0;i<count;++i) { auto extname=reinterpret_cast<const char*>(get_string_i(GL_EXTENSIONS,static_cast<GLuint>(i))); if (extname && std::string(extname)=="GL_KHR_debug") supported=true; }
                if (supported) {
                    try {
                        debug_callback=reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(symbol("glDebugMessageCallback"));
                        push_group=reinterpret_cast<PFNGLPUSHDEBUGGROUPPROC>(symbol("glPushDebugGroup"));
                        pop_group=reinterpret_cast<PFNGLPOPDEBUGGROUPPROC>(symbol("glPopDebugGroup"));
                        gl.Enable(GL_DEBUG_OUTPUT); gl.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                        debug_callback(receive,this); caps.debug_messages=true;
                    } catch (...) { debug_callback=nullptr; push_group=nullptr; pop_group=nullptr; }
                }
            }
            if (robust) {
                auto proc=wglGetProcAddress("glGetGraphicsResetStatusARB");
                if (proc && proc != reinterpret_cast<PROC>(1) && proc != reinterpret_cast<PROC>(2) && proc != reinterpret_cast<PROC>(3) && proc != reinterpret_cast<PROC>(-1)) reset_status=reinterpret_cast<PFNGLGETGRAPHICSRESETSTATUSPROC>(proc);
            }
            caps.reset_detection=reset_status!=nullptr;
            scene=std::make_unique<Scene>(gl);
            try { scene->init(hooks ? hooks->fragment_shader_override : std::string{}); } catch (const std::exception& e) { throw StartupError{{StartupFailureKind::shader,{}, {},to16(e.what())}}; }
            if (gl.GetError()!=GL_NO_ERROR) throw StartupError{{StartupFailureKind::resource,{}, {},u"OpenGL resource creation failed"}};
        } catch (const StartupError&) { release(); throw; }
          catch (const std::exception& e) {
            std::string message=e.what(); release();
            throw StartupError{{StartupFailureKind::context_creation,{}, {},utf16_from_utf8(message)}};
        } catch (...) { release(); throw; }
    }
    void release(){if(context && dc)wglMakeCurrent(dc,context);if(debug_callback)debug_callback(nullptr,nullptr);scene.reset();if(context||bootstrap)wglMakeCurrent(nullptr,nullptr);if(context)wglDeleteContext(context);if(bootstrap)wglDeleteContext(bootstrap);if(dc)ReleaseDC(window,dc);context=bootstrap=nullptr;dc=nullptr;}
    ~Impl(){release();}
    void execute(const DrawList& list,int width,int height,bool offscreen) {
        require(wglMakeCurrent(dc,context)!=FALSE,"WGL activation failed");
        auto& g=gl; auto& sc=*scene;
        if(offscreen){
            g.BindTexture(GL_TEXTURE_2D,sc.target);
            g.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            g.BindFramebuffer(GL_FRAMEBUFFER,sc.fbo);
            g.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,sc.target,0);
            require(g.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Framebuffer incomplete");
        } else g.BindFramebuffer(GL_FRAMEBUFFER,0);
        g.Viewport(0,0,width,height);g.Disable(GL_FRAMEBUFFER_SRGB);g.Disable(GL_DITHER);
        g.Enable(GL_BLEND);g.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        g.UseProgram(sc.program);g.BindVertexArray(sc.vao);g.PixelStorei(GL_UNPACK_ALIGNMENT,1);
        auto uniform=[&](const char* name){return g.GetUniformLocation(sc.program,name);};
        g.Uniform2f(uniform("screen"),float(width),float(height));
        g.Uniform1i(uniform("atlas"),0);
        // Resolve the complete frame before uploading. An eviction invalidates borrowed
        // pages, so retry once from the new generation; a frame larger than the atlas
        // is consumed one glyph at a time instead.
        std::vector<ResolvedGlyph> resolved(list.commands.size());
        bool segmented=false;
        for(int attempt=0;attempt<2;++attempt){
            bool reset=false;
            for(std::size_t i=0;i<list.commands.size();++i){
                const auto& command=list.commands[i];
                resolved[i]=command.resolve?command.resolve():ResolvedGlyph{command.rect,command.coverage};
                if(resolved[i].reset){reset=true;break;}
            }
            if(!reset)break;
            if(attempt==1)segmented=true;
        }
        std::unordered_map<const AtlasPage*,unsigned> pages;
        if(!segmented){
            for(const auto& glyph:resolved)if(glyph.region.page && !pages.contains(glyph.region.page)){
                if(pages.size()==4){segmented=true;break;}
                unsigned slot=static_cast<unsigned>(pages.size());pages.emplace(glyph.region.page,slot);
                const auto& page=*glyph.region.page;g.BindTexture(GL_TEXTURE_2D,sc.textures[slot]);
                g.TexImage2D(GL_TEXTURE_2D,0,GL_R8,page.width,page.height,0,GL_RED,GL_UNSIGNED_BYTE,page.pixels.data());
            }
        }
        if(!segmented){sc.atlas_width=0;sc.atlas_height=0;}
        for(std::size_t index=0;index<list.commands.size();++index){
            const auto& command=list.commands[index];
            if(command.type==DrawCommand::Type::begin_group) {
                current_group=command.owner;
#ifndef NDEBUG
                if(push_group) push_group(GL_DEBUG_SOURCE_APPLICATION,0,static_cast<GLsizei>(current_group.size()),current_group.c_str());
#endif
                continue;
            }
            if(command.type==DrawCommand::Type::end_group) {
#ifndef NDEBUG
                if(pop_group) pop_group();
#endif
                current_group.clear(); continue;
            }
            if(command.type==DrawCommand::Type::clear){
                g.Disable(GL_SCISSOR_TEST);g.ClearColor(command.color.r/255.f,command.color.g/255.f,command.color.b/255.f,command.color.a/255.f);g.Clear(GL_COLOR_BUFFER_BIT);continue;
            }
            if(command.type==DrawCommand::Type::clip){
                auto clip=intersect(command.rect,{0,0,width,height});g.Enable(GL_SCISSOR_TEST);
                g.Scissor(clip.x,height-clip.y-clip.h,clip.w,clip.h);continue;
            }
            if(command.type!=DrawCommand::Type::fill && command.type!=DrawCommand::Type::rounded && command.type!=DrawCommand::Type::glyph)continue;
            auto glyph=segmented?(command.resolve?command.resolve():ResolvedGlyph{command.rect,command.coverage}):resolved[index];
            if(command.type==DrawCommand::Type::glyph && !glyph.region.page)continue;
            auto rect=glyph.rect;if(rect.w<=0 || rect.h<=0)continue;
            int kind=command.type==DrawCommand::Type::rounded?1:command.type==DrawCommand::Type::glyph?2:0;
            if(kind==2){const auto& page=*glyph.region.page;
                if(!segmented)g.BindTexture(GL_TEXTURE_2D,sc.textures[pages.at(&page)]);
                else {g.BindTexture(GL_TEXTURE_2D,sc.textures[0]);
                // Each borrowed region is uploaded and drawn before the next
                // resolution can evict its CPU page.
                if(sc.atlas_width!=page.width || sc.atlas_height!=page.height){
                    g.TexImage2D(GL_TEXTURE_2D,0,GL_R8,page.width,page.height,0,GL_RED,GL_UNSIGNED_BYTE,nullptr);
                    sc.atlas_width=page.width;sc.atlas_height=page.height;
                }
                auto r=glyph.region.rect;g.PixelStorei(GL_UNPACK_ROW_LENGTH,page.width);
                g.TexSubImage2D(GL_TEXTURE_2D,0,r.x,r.y,r.w,r.h,GL_RED,GL_UNSIGNED_BYTE,page.pixels.data()+static_cast<std::size_t>(r.y)*page.width+r.x);
                g.PixelStorei(GL_UNPACK_ROW_LENGTH,0);}
                g.Uniform2f(uniform("atlasOrigin"),float(glyph.region.rect.x),float(glyph.region.rect.y));}
            g.Uniform4f(uniform("rect"),float(rect.x),float(rect.y),float(rect.w),float(rect.h));
            g.Uniform4f(uniform("tint"),command.color.r/255.f,command.color.g/255.f,command.color.b/255.f,command.color.a/255.f);
            g.Uniform1f(uniform("radius"),float(command.radius));g.Uniform1i(uniform("kind"),kind);
            g.DrawArrays(GL_TRIANGLES,0,6);
        }
        if (hooks && hooks->misuse_once) { hooks->misuse_once=false; g.Enable(0xffffffffu); (void)g.GetError(); }
        GLenum error=GL_NO_ERROR, first=GL_NO_ERROR;
        std::vector<unsigned> errors;
        while ((error=g.GetError())!=GL_NO_ERROR) { if (first==GL_NO_ERROR) first=error; errors.push_back(error); }
        ++frames;
        if (hooks && hooks->reset_at_frame==frames) { hooks->reset_at_frame=0; throw FrameError{{RendererFailureKind::device_reset,0x8253,u"Graphics reset"}}; }
        if (reset_status) { auto reset=reset_status(); if (reset!=GL_NO_ERROR) throw FrameError{{RendererFailureKind::device_reset,reset,u"Graphics reset"}}; }
        if (hooks && hooks->gl_error_at_frame==frames) { first=hooks->injected_error; errors.push_back(first); hooks->gl_error_at_frame=0; }
        if (first!=GL_NO_ERROR) throw FrameError{{RendererFailureKind::frame_error,first,u"OpenGL frame failed"},std::move(errors)};
    }
};
Renderer::Renderer(HWND window, RendererTestHooks* hooks, DebugContext debug, std::function<void(RenderDiagnostic)> sink):impl_(std::make_unique<Impl>(window,hooks,debug,std::move(sink))){}
Renderer::~Renderer()=default;
RenderDiagnostic frame_diagnostic(unsigned code) {
    RenderDiagnostic diagnostic; diagnostic.backend=u"OpenGL"; diagnostic.severity=DiagnosticSeverity::error;
    diagnostic.category=DiagnosticCategory::other; diagnostic.native_code=code; diagnostic.native_message="OpenGL frame error";
    return diagnostic;
}
void Renderer::restore_atlas(const std::vector<const AtlasPage*>& pages) {
    require(wglMakeCurrent(impl_->dc,impl_->context)!=FALSE,"WGL activation failed");
    auto& gl=impl_->gl;
    for (std::size_t i=0; i<pages.size() && i<4; ++i) {
        const auto& page=*pages[i]; gl.BindTexture(GL_TEXTURE_2D,impl_->scene->textures[i]);
        gl.TexImage2D(GL_TEXTURE_2D,0,GL_R8,page.width,page.height,0,GL_RED,GL_UNSIGNED_BYTE,page.pixels.data());
    }
    require(gl.GetError()==GL_NO_ERROR,"Atlas upload failed");
}
double Renderer::draw(const DrawList& list,int width,int height,bool measure,bool offscreen,double* submission_ms,std::chrono::steady_clock::time_point* presented){
    auto start=std::chrono::steady_clock::now();
    auto& gl=impl_->gl; GLuint query=0; GLuint64 ns=0;
    if(measure){gl.GenQueries(1,&query); gl.BeginQuery(GL_TIME_ELAPSED,query);}
    impl_->execute(list,width,height,offscreen);
    if(measure) gl.EndQuery(GL_TIME_ELAPSED);
    if(submission_ms) *submission_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(!offscreen && !SwapBuffers(impl_->dc)) throw FrameError{{RendererFailureKind::present_failed,GetLastError(),u"SwapBuffers failed"}};
    if(presented) *presented=std::chrono::steady_clock::now();
    if(measure){gl.GetQueryObjectui64v(query,GL_QUERY_RESULT,&ns); gl.DeleteQueries(1,&query);}
    return double(ns)/1000000.0;
}
RendererCapabilities Renderer::capabilities() const { return impl_->caps; }
std::string Renderer::name() const {
    return std::string(reinterpret_cast<const char*>(impl_->gl.GetString(GL_RENDERER))) + " / " +
        reinterpret_cast<const char*>(impl_->gl.GetString(GL_VERSION));
}
Image Renderer::read_back(const DrawList& list,int width,int height){
    impl_->execute(list,width,height,true);auto& gl=impl_->gl;gl.Finish();
    Image result{width,height,std::vector<unsigned char>(static_cast<std::size_t>(width)*height*4)};
    std::vector<unsigned char> raw(result.pixels.size());gl.ReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
    const auto stride=static_cast<std::size_t>(width)*4;
    for(int y=0;y<height;++y)std::copy_n(raw.data()+static_cast<std::size_t>(height-1-y)*stride,stride,result.pixels.data()+static_cast<std::size_t>(y)*stride);
    return result;
}
}
