// Map viewer: pan and zoom over any map of the game.
//
//   map_viewer [--ra2] [--names names.txt] [--map name] [--center x,y] [--zoom n] <game folder>
//
// Pick a map in the list (or give --map to open one at start-up). Drag with the left mouse
// button or use the arrow keys / WASD to pan, the mouse wheel to zoom. The game folder
// defaults to the RA_GAME_DIR environment variable.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "core/log.h"
#include "formats/map.h"
#include "render/gl_api.h"
#include "render/map_renderer.h"
#include "render/sprite_batch.h"
#include "rules/rules_db.h"
#include "sim/world/world.h"
#include "common/display.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace ra;

namespace {

std::string lower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string extension_of(const std::string& path)
{
	const auto dot = path.rfind('.');
	return dot == std::string::npos ? "" : lower(path.substr(dot + 1));
}

struct MapEntry {
	std::string name;                 // shown in the list
	std::span<const uint8_t> bytes;   // valid while the vfs / sources live
};

struct App {
	SDL_Window* window = nullptr;
	SDL_GLContext gl = nullptr;
	bool gles = false;

	vfs::GameKind kind = vfs::GameKind::YurisRevenge;
	vfs::Vfs vfs;
	vfs::NameDb names;
	size_t base_mounts = 0;
	rules::RulesDb rules;

	std::vector<std::shared_ptr<const vfs::ByteSource>> loose_sources;
	std::vector<MapEntry> maps;
	std::vector<int> filtered;
	char filter[128] = "";
	std::string last_filter;
	int selected = -1;
	std::string status;

	std::unique_ptr<world::World> world;
	// Destroyed before the GL context (its destructors call OpenGL).
	std::unique_ptr<render::MapRenderer> renderer = std::make_unique<render::MapRenderer>();
	render::View view;
	bool dragging = false;
	float last_frame_ms = 16.0f;
	uint64_t last_counter = 0;
	bool show_ui = true;
	world::ShroudMap shroud;
	bool show_shroud = false;
	std::string screenshot_path; // when set: draw a few frames, save the picture and exit
	int frames = 0;
};

void apply_filter(App& app)
{
	app.last_filter = app.filter;
	app.filtered.clear();
	const std::string f = lower(app.filter);
	for (size_t i = 0; i < app.maps.size(); ++i) {
		if (f.empty() || lower(app.maps[i].name).find(f) != std::string::npos)
			app.filtered.push_back(static_cast<int>(i));
	}
}

void center_view(App& app)
{
	if (!app.world)
		return;
	const auto b = app.renderer->playable_bounds();
	app.view.scroll_x = (b.left + b.right) * 0.5f - float(app.view.width) / (2.0f * app.view.zoom);
	app.view.scroll_y = (b.top + b.bottom) * 0.5f - float(app.view.height) / (2.0f * app.view.zoom);
}

// A demonstration of the shroud: everything hidden except around the objects of the first player
// house the map places, each revealing a circle of its sight radius.
void update_shroud(App& app)
{
	app.renderer->set_shroud(app.show_shroud ? &app.shroud : nullptr);
	if (!app.show_shroud || !app.world)
		return;
	app.shroud.hide_all();
	std::string owner;
	for (const auto& o : app.world->placed_objects) {
		const std::string l = lower(o.owner);
		if (l != "neutral" && l != "special") {
			owner = o.owner;
			break;
		}
	}
	for (const auto& o : app.world->placed_objects) {
		if (!owner.empty() && lower(o.owner) != lower(owner))
			continue; // (a map with only neutral objects reveals around all of them)
		const int sight = app.world->rules.object(o.kind, o.type).get_int("Sight");
		app.shroud.reveal_area(o.x, o.y, sight > 0 ? sight : 5);
	}
}

bool load_map_entry(App& app, int index)
{
	const MapEntry& entry = app.maps[static_cast<size_t>(index)];
	formats::MapData data;
	std::string error;
	if (!formats::load_map(entry.bytes, data, &error)) {
		app.status = "cannot read the map: " + error;
		return false;
	}
	auto next = std::make_unique<world::World>();
	if (!world::World::load(app.vfs, app.base_mounts, app.rules, std::move(data), *next, &error)) {
		app.status = "cannot load the map: " + error;
		return false;
	}
	if (!app.renderer->set_world(next.get(), &app.vfs))
		RA_LOG_WARN("some palettes could not be loaded");
	app.world = std::move(next);
	app.selected = index;
	update_shroud(app);
	app.status.clear();
	center_view(app);
	return true;
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	std::string game_dir = std::getenv("RA_GAME_DIR") ? std::getenv("RA_GAME_DIR") : "";
	std::string names_path = std::getenv("RA_NAMES_FILE") ? std::getenv("RA_NAMES_FILE") : "";
	std::string open_map;
	float zoom = 1.0f;
	bool have_center = false;
	bool start_shroud = false;
	float center_x = 0, center_y = 0;
	auto* app = new App();
	*appstate = app;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--ra2")
			app->kind = vfs::GameKind::RedAlert2;
		else if (a == "--names" && i + 1 < argc)
			names_path = argv[++i];
		else if (a == "--map" && i + 1 < argc)
			open_map = lower(argv[++i]);
		else if (a == "--screenshot" && i + 1 < argc) {
			app->screenshot_path = argv[++i];
			app->show_ui = false;
		} else if (a == "--show-ui")
			app->show_ui = true;
		else if (a == "--zoom" && i + 1 < argc)
			zoom = static_cast<float>(std::atof(argv[++i]));
		else if (a == "--shroud")
			start_shroud = true;
		else if (a == "--center" && i + 1 < argc) {
			have_center = std::sscanf(argv[++i], "%f,%f", &center_x, &center_y) == 2;
		} else
			game_dir = a;
	}
	if (game_dir.empty()) {
		std::fprintf(stderr, "usage: map_viewer [--ra2] [--names names.txt] [--map name] [--center x,y] [--zoom n] <game folder>\n");
		return SDL_APP_FAILURE;
	}
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
		return SDL_APP_FAILURE;

	// Files and rules
	app->vfs.set_loose_directory(game_dir);
	app->vfs.mount_standard(app->kind);
	app->base_mounts = app->vfs.mounts().size();
	if (!names_path.empty()) {
		std::ifstream in(names_path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		app->names.add_lines(ss.str());
	}
	std::string error;
	if (!app->rules.load(app->vfs, app->kind, &error)) {
		std::fprintf(stderr, "cannot load the rules: %s\n", error.c_str());
		return SDL_APP_FAILURE;
	}
	app->vfs.visit_all(&app->names, [&](const vfs::Vfs::VisitedEntry& e) {
		if (e.named && extension_of(e.path) == "map")
			app->maps.push_back({ e.path, e.bytes });
	});
	for (const auto& real_name : app->vfs.loose_files_matching("", "")) {
		const std::string ext = extension_of(real_name);
		if (ext != "map" && ext != "yrm" && ext != "mpr" && ext != "mmx" && ext != "yro")
			continue;
		auto source = vfs::map_file(game_dir + "/" + real_name);
		if (!source)
			continue;
		app->loose_sources.push_back(source);
		app->maps.push_back({ real_name, source->all() });
	}
	std::sort(app->maps.begin(), app->maps.end(), [](const MapEntry& a, const MapEntry& b) { return lower(a.name) < lower(b.name); });
	apply_filter(*app);
	std::printf("%zu maps\n", app->maps.size());

	// Window and GL
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	app->window = SDL_CreateWindow("Map viewer", 1500, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!app->window)
		return SDL_APP_FAILURE;
	tools::place_window_on_display(app->window);
	struct Attempt { int profile, major, minor; bool gles; };
	for (const Attempt& a : { Attempt { SDL_GL_CONTEXT_PROFILE_ES, 3, 0, true }, Attempt { SDL_GL_CONTEXT_PROFILE_CORE, 3, 3, false } }) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, a.profile);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.major);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.minor);
		app->gl = SDL_GL_CreateContext(app->window);
		if (app->gl) {
			app->gles = a.gles;
			break;
		}
	}
	if (!app->gl)
		return SDL_APP_FAILURE;
	SDL_GL_MakeCurrent(app->window, app->gl);
	SDL_GL_SetSwapInterval(1);
	render::set_gl_is_es(app->gles);
	if (!render::load_gl() || !app->renderer->init())
		return SDL_APP_FAILURE;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::StyleColorsDark();
	const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	if (scale > 0.0f) {
		ImGui::GetStyle().ScaleAllSizes(scale);
		ImGui::GetStyle().FontScaleDpi = scale;
	}
	ImGui_ImplSDL3_InitForOpenGL(app->window, app->gl);
	ImGui_ImplOpenGL3_Init(app->gles ? "#version 300 es" : "#version 330 core");

	app->view.zoom = zoom;
	app->show_shroud = start_shroud;
	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(app->window, &w, &h);
	app->view.width = w;
	app->view.height = h;
	if (!open_map.empty()) {
		for (size_t i = 0; i < app->maps.size(); ++i) {
			const std::string n = lower(app->maps[i].name);
			if (n.size() >= open_map.size() && n.compare(n.size() - open_map.size(), open_map.size(), open_map) == 0) {
				load_map_entry(*app, static_cast<int>(i));
				break;
			}
		}
		if (app->world && have_center) {
			// A cell given as x,y: put it in the middle of the screen.
			app->view.scroll_x = (center_x - center_y) * 30.0f - float(app->view.width) / (2.0f * app->view.zoom);
			app->view.scroll_y = (center_x + center_y) * 15.0f - float(app->view.height) / (2.0f * app->view.zoom);
		}
	}
	app->last_counter = SDL_GetPerformanceCounter();
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	auto* app = static_cast<App*>(appstate);
	ImGui_ImplSDL3_ProcessEvent(event);
	const bool ui_wants_mouse = ImGui::GetIO().WantCaptureMouse;
	switch (event->type) {
	case SDL_EVENT_QUIT:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		return SDL_APP_SUCCESS;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button == SDL_BUTTON_LEFT && !ui_wants_mouse)
			app->dragging = true;
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event->button.button == SDL_BUTTON_LEFT)
			app->dragging = false;
		break;
	case SDL_EVENT_MOUSE_MOTION:
		if (app->dragging) {
			const float scale = SDL_GetWindowPixelDensity(app->window);
			app->view.scroll_x -= event->motion.xrel * scale / app->view.zoom;
			app->view.scroll_y -= event->motion.yrel * scale / app->view.zoom;
		}
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		if (!ui_wants_mouse && event->wheel.y != 0) {
			// Zoom around the mouse position.
			const float scale = SDL_GetWindowPixelDensity(app->window);
			float mx, my;
			SDL_GetMouseState(&mx, &my);
			mx *= scale;
			my *= scale;
			const float wx = app->view.scroll_x + mx / app->view.zoom, wy = app->view.scroll_y + my / app->view.zoom;
			app->view.zoom = std::clamp(app->view.zoom * (event->wheel.y > 0 ? 1.25f : 0.8f), 0.2f, 8.0f);
			app->view.scroll_x = wx - mx / app->view.zoom;
			app->view.scroll_y = wy - my / app->view.zoom;
		}
		break;
	case SDL_EVENT_KEY_DOWN:
		if (event->key.key == SDLK_H)
			app->show_ui = !app->show_ui;
		if (event->key.key == SDLK_C)
			center_view(*app);
		break;
	default:
		break;
	}
	return SDL_APP_CONTINUE;
}

// Saves the current framebuffer as a 24-bit BMP.
bool save_bmp(const std::string& path, int w, int h)
{
	std::vector<uint8_t> rgba(size_t(w) * h * 4);
	const render::GlApi& g = render::gl();
	g.glPixelStorei(render::GL_PACK_ALIGNMENT, 1);
	g.glReadPixels(0, 0, w, h, render::GL_RGBA, render::GL_UNSIGNED_BYTE, rgba.data());
	const uint32_t row = (uint32_t(w) * 3 + 3) & ~3u;
	const uint32_t size = 54 + row * uint32_t(h);
	std::vector<uint8_t> out(size, 0);
	auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) out[at + i] = uint8_t(v >> (8 * i)); };
	out[0] = 'B'; out[1] = 'M';
	put32(2, size); put32(10, 54); put32(14, 40); put32(18, uint32_t(w)); put32(22, uint32_t(h));
	out[26] = 1; out[28] = 24; put32(34, row * uint32_t(h));
	for (int y = 0; y < h; ++y) {      // GL rows run bottom to top, like BMP
		uint8_t* dst = &out[54 + size_t(row) * y];
		const uint8_t* src = &rgba[size_t(y) * w * 4];
		for (int x = 0; x < w; ++x) {
			dst[x * 3 + 0] = src[x * 4 + 2];
			dst[x * 3 + 1] = src[x * 4 + 1];
			dst[x * 3 + 2] = src[x * 4 + 0];
		}
	}
	std::ofstream f(path, std::ios::binary);
	f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
	return bool(f);
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	auto* app = static_cast<App*>(appstate);
	const uint64_t now = SDL_GetPerformanceCounter();
	app->last_frame_ms = float(double(now - app->last_counter) * 1000.0 / double(SDL_GetPerformanceFrequency()));
	app->last_counter = now;

	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(app->window, &w, &h);
	app->view.width = w;
	app->view.height = h;

	// Keyboard panning
	{
		const bool* keys = SDL_GetKeyboardState(nullptr);
		const float speed = 900.0f * (app->last_frame_ms / 1000.0f) / app->view.zoom;
		if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])
			app->view.scroll_x -= speed;
		if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])
			app->view.scroll_x += speed;
		if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])
			app->view.scroll_y -= speed;
		if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])
			app->view.scroll_y += speed;
	}

	const render::GlApi& g = render::gl();
	g.glViewport(0, 0, w, h);
	g.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	g.glClear(render::GL_COLOR_BUFFER_BIT);
	app->renderer->draw(app->view);
	if (!app->screenshot_path.empty() && ++app->frames >= 3) {
		const bool ok = save_bmp(app->screenshot_path, w, h);
		std::printf("screenshot %s: %s (%dx%d)\n", app->screenshot_path.c_str(), ok ? "saved" : "FAILED", w, h);
		std::fflush(stdout);
		return SDL_APP_SUCCESS;
	}

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	if (app->show_ui) {
		ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
		ImGui::Begin("Maps");
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##filter", "filter", app->filter, sizeof app->filter);
		if (app->last_filter != app->filter)
			apply_filter(*app);
		ImGui::Text("%zu of %zu maps. H hides this window, C centres the view.", app->filtered.size(), app->maps.size());
		if (!app->status.empty())
			ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", app->status.c_str());
		ImGui::BeginChild("list", ImVec2(0, -150));
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(app->filtered.size()));
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const int idx = app->filtered[static_cast<size_t>(row)];
				if (ImGui::Selectable((app->maps[static_cast<size_t>(idx)].name + "##" + std::to_string(idx)).c_str(), idx == app->selected))
					load_map_entry(*app, idx);
			}
		}
		ImGui::EndChild();
		ImGui::Separator();
		ImGui::Text("%s mode, zoom %.2f, %.1f ms", rules::game_name(app->kind), app->view.zoom, app->last_frame_ms);
		if (app->world) {
			const auto& st = app->renderer->stats();
			ImGui::Text("%s  %s  %dx%d", app->world->map.ini.get_string("Basic", "Name").c_str(), app->world->map.theater.c_str(), app->world->map.map_width, app->world->map.map_height);
			ImGui::Text("tiles %zu, objects %zu, quads %zu, atlas %zu images on %d pages", st.tiles_drawn, st.objects_drawn, st.quads, st.atlas_images, st.atlas_pages);
		}
		ImGui::Checkbox("Terrain", &app->renderer->draw_tiles);
		ImGui::SameLine();
		ImGui::Checkbox("Overlays", &app->renderer->draw_overlays);
		ImGui::SameLine();
		ImGui::Checkbox("Trees", &app->renderer->draw_terrain_objects);
		ImGui::Checkbox("Objects", &app->renderer->draw_objects);
		ImGui::SameLine();
		ImGui::Checkbox("Lighting", &app->renderer->use_lighting);
		if (ImGui::Checkbox("Shroud", &app->show_shroud))
			update_shroud(*app);
		ImGui::End();
	}
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(app->window);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult)
{
	auto* app = static_cast<App*>(appstate);
	if (!app)
		return;
	if (ImGui::GetCurrentContext()) {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}
	app->renderer.reset();
	app->world.reset();
	if (app->gl)
		SDL_GL_DestroyContext(app->gl);
	if (app->window)
		SDL_DestroyWindow(app->window);
	delete app;
}
