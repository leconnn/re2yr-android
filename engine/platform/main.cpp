// Application entry point (desktop and Android), using SDL's callback model.
//
// Phase 0: opens a window with an OpenGL ES 3.0 context (desktop OpenGL 3.3
// as a fallback) and shows a debug overlay.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "core/log.h"
#include "core/random.h"
#include "version.h"

namespace {

struct App {
	SDL_Window* window = nullptr;
	SDL_GLContext gl = nullptr;
	bool gles = false;
	const char* gl_version = "";
	const char* gl_renderer = "";
	uint64_t last_counter = 0;
	float frame_ms = 0.0f;
	ra::Random2 rng { 0 };
};

using GlClearColorFn = void (*)(float, float, float, float);
using GlClearFn = void (*)(unsigned int);
using GlViewportFn = void (*)(int, int, int, int);
using GlGetStringFn = const unsigned char* (*)(unsigned int);

GlClearColorFn gl_clear_color = nullptr;
GlClearFn gl_clear = nullptr;
GlViewportFn gl_viewport = nullptr;

constexpr unsigned int kGlColorBufferBit = 0x00004000;
constexpr unsigned int kGlVersion = 0x1F02;
constexpr unsigned int kGlRenderer = 0x1F01;

void sdl_log_sink(ra::LogLevel level, const char* message)
{
	switch (level) {
	case ra::LogLevel::Debug: SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
	case ra::LogLevel::Info: SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
	case ra::LogLevel::Warning: SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
	case ra::LogLevel::Error: SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message); break;
	}
}

// Tries OpenGL ES 3.0 first (the Android target), then desktop OpenGL 3.3 core.
bool create_gl_context(App& app)
{
	struct Attempt {
		int profile, major, minor;
		bool gles;
	};
	static constexpr Attempt attempts[] = {
		{ SDL_GL_CONTEXT_PROFILE_ES, 3, 0, true },
		{ SDL_GL_CONTEXT_PROFILE_CORE, 3, 3, false },
	};
	for (const Attempt& a : attempts) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, a.profile);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.major);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.minor);
		app.gl = SDL_GL_CreateContext(app.window);
		if (app.gl) {
			app.gles = a.gles;
			return true;
		}
		RA_LOG_WARN("Could not create %s %d.%d context: %s", a.gles ? "OpenGL ES" : "OpenGL", a.major, a.minor, SDL_GetError());
	}
	return false;
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
	ra::set_log_sink(&sdl_log_sink);
	SDL_SetAppMetadata("Red Alert 2 / Yuri's Revenge", RA_VERSION_STRING, "io.github.leconnn.re2yr");

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		RA_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	auto* app = new App();
	*appstate = app;

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	app->window = SDL_CreateWindow("Red Alert 2 / Yuri's Revenge", 1280, 720, flags);
	if (!app->window) {
		RA_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	if (!create_gl_context(*app)) {
		RA_LOG_ERROR("No usable OpenGL context");
		return SDL_APP_FAILURE;
	}
	SDL_GL_MakeCurrent(app->window, app->gl);
	SDL_GL_SetSwapInterval(1);

	gl_clear_color = reinterpret_cast<GlClearColorFn>(SDL_GL_GetProcAddress("glClearColor"));
	gl_clear = reinterpret_cast<GlClearFn>(SDL_GL_GetProcAddress("glClear"));
	gl_viewport = reinterpret_cast<GlViewportFn>(SDL_GL_GetProcAddress("glViewport"));
	auto gl_get_string = reinterpret_cast<GlGetStringFn>(SDL_GL_GetProcAddress("glGetString"));
	if (!gl_clear_color || !gl_clear || !gl_viewport || !gl_get_string) {
		RA_LOG_ERROR("Missing core OpenGL functions");
		return SDL_APP_FAILURE;
	}
	app->gl_version = reinterpret_cast<const char*>(gl_get_string(kGlVersion));
	app->gl_renderer = reinterpret_cast<const char*>(gl_get_string(kGlRenderer));
	RA_LOG_INFO("re2yr %s, %s on %s", RA_VERSION_STRING, app->gl_version, app->gl_renderer);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	if (scale > 0.0f) {
		ImGui::GetStyle().ScaleAllSizes(scale);
		ImGui::GetStyle().FontScaleDpi = scale;
	}
	ImGui_ImplSDL3_InitForOpenGL(app->window, app->gl);
	ImGui_ImplOpenGL3_Init(app->gles ? "#version 300 es" : "#version 330 core");

	app->last_counter = SDL_GetPerformanceCounter();
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	auto* app = static_cast<App*>(appstate);
	ImGui_ImplSDL3_ProcessEvent(event);
	if (event->type == SDL_EVENT_QUIT)
		return SDL_APP_SUCCESS;
	if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event->window.windowID == SDL_GetWindowID(app->window))
		return SDL_APP_SUCCESS;
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	auto* app = static_cast<App*>(appstate);

	const uint64_t now = SDL_GetPerformanceCounter();
	const float ms = float(double(now - app->last_counter) * 1000.0 / double(SDL_GetPerformanceFrequency()));
	app->last_counter = now;
	app->frame_ms = app->frame_ms * 0.95f + ms * 0.05f;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
	ImGui::Begin("re2yr", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("Red Alert 2 / Yuri's Revenge");
	ImGui::Text("Version %s (Phase 0)", RA_VERSION_STRING);
	ImGui::Separator();
	ImGui::Text("Platform: %s", SDL_GetPlatform());
	ImGui::Text("Graphics: %s", app->gl_version);
	ImGui::Text("Renderer: %s", app->gl_renderer);
	ImGui::Text("Frame: %.2f ms (%.0f fps)", app->frame_ms, app->frame_ms > 0 ? 1000.0f / app->frame_ms : 0.0f);
	ImGui::Separator();
	if (ImGui::Button("Draw from the game RNG"))
		app->rng.next();
	ImGui::SameLine();
	ImGui::Text("index %d / %d", app->rng.index1(), app->rng.index2());
	ImGui::End();

	ImGui::Render();
	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(app->window, &w, &h);
	gl_viewport(0, 0, w, h);
	gl_clear_color(0.08f, 0.09f, 0.11f, 1.0f);
	gl_clear(kGlColorBufferBit);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(app->window);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
	auto* app = static_cast<App*>(appstate);
	if (ImGui::GetCurrentContext()) {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}
	if (app) {
		if (app->gl)
			SDL_GL_DestroyContext(app->gl);
		if (app->window)
			SDL_DestroyWindow(app->window);
		delete app;
	}
}
