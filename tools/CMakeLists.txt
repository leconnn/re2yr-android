# Developer tools (desktop only).

add_executable(asset_viewer
	asset_viewer/gl_texture.cpp
	asset_viewer/gl_texture.h
	asset_viewer/main.cpp
)
target_include_directories(asset_viewer PRIVATE asset_viewer .)
target_link_libraries(asset_viewer PRIVATE ra_core ra_vfs ra_ini ra_formats imgui SDL3::SDL3)
ra_set_target_options(asset_viewer)

add_executable(map_viewer
	map_viewer/main.cpp
)
target_include_directories(map_viewer PRIVATE .)
target_link_libraries(map_viewer PRIVATE ra_core ra_vfs ra_ini ra_formats ra_rules ra_world ra_render imgui SDL3::SDL3)
ra_set_target_options(map_viewer)
