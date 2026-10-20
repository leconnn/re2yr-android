# Core: platform-independent foundations shared by the simulation and tools.
add_library(ra_core STATIC
	core/assert.cpp
	core/assert.h
	core/base64.cpp
	core/base64.h
	core/containers.h
	core/coord.h
	core/dir.h
	core/fastmath.cpp
	core/fastmath.h
	core/fastmath_tables.inc
	core/fpu.cpp
	core/fpu.h
	core/log.cpp
	core/log.h
	core/random.cpp
	core/random.h
)
target_include_directories(ra_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
ra_set_target_options(ra_core)

# Virtual file system: MIX archives, encryption, and the lookup across them.
add_library(ra_vfs STATIC
	vfs/blowfish.cpp
	vfs/blowfish.h
	vfs/blowfish_tables.inc
	vfs/byte_source.cpp
	vfs/byte_source.h
	vfs/mix_archive.cpp
	vfs/mix_archive.h
	vfs/mix_id.cpp
	vfs/mix_id.h
	vfs/mix_key.cpp
	vfs/mix_key.h
	vfs/vfs.cpp
	vfs/vfs.h
)
target_link_libraries(ra_vfs PUBLIC ra_core)
ra_set_target_options(ra_vfs)

# INI parser.
add_library(ra_ini STATIC
	ini/ini.cpp
	ini/ini.h
)
target_link_libraries(ra_ini PUBLIC ra_core)
ra_set_target_options(ra_ini)

# Asset formats: sprites, terrain, voxels, palettes, strings, audio, maps.
add_library(ra_formats STATIC
	formats/audio.cpp
	formats/audio.h
	formats/compression.cpp
	formats/compression.h
	formats/csf.cpp
	formats/csf.h
	formats/fnt.cpp
	formats/fnt.h
	formats/map.cpp
	formats/map.h
	formats/palette.cpp
	formats/palette.h
	formats/pcx.cpp
	formats/pcx.h
	formats/shp.cpp
	formats/shp.h
	formats/tmp.cpp
	formats/tmp.h
	formats/voxel.cpp
	formats/voxel.h
)
target_link_libraries(ra_formats PUBLIC ra_core ra_ini ra_vfs)
ra_set_target_options(ra_formats)

# Software voxel renderer: VXL/HVA models drawn into palette-indexed sprites.
add_library(ra_voxel STATIC
	voxel/voxel_normals.inc
	voxel/voxel_renderer.cpp
	voxel/voxel_renderer.h
)
target_link_libraries(ra_voxel PUBLIC ra_core ra_formats)
ra_set_target_options(ra_voxel)

# Rules: the typed INI key table, the rules database and the game definitions.
add_library(ra_rules STATIC
	rules/game_def.cpp
	rules/game_def.h
	rules/key_table.cpp
	rules/key_table.h
	rules/key_table_data.inc
	rules/rule_object.cpp
	rules/rule_object.h
	rules/rules_db.cpp
	rules/rules_db.h
)
target_link_libraries(ra_rules PUBLIC ra_core ra_ini ra_vfs ra_formats)
ra_set_target_options(ra_rules)

# World: theater tiles, the cell grid, land types and heights, and the loaded map.
add_library(ra_world STATIC
	sim/world/land.cpp
	sim/world/land.h
	sim/world/map_grid.cpp
	sim/world/map_grid.h
	sim/world/shroud.cpp
	sim/world/shroud.h
	sim/world/slope_planes.inc
	sim/world/theater_tiles.cpp
	sim/world/theater_tiles.h
	sim/world/world.cpp
	sim/world/world.h
)
target_link_libraries(ra_world PUBLIC ra_core ra_ini ra_vfs ra_formats ra_rules)
ra_set_target_options(ra_world)

# Simulation loop: events, the DoList, the frame CRC, recordings and LogicClass::Update order.
add_library(ra_sim STATIC
	sim/loop/event.cpp
	sim/loop/event.h
	sim/loop/event_lengths.inc
	sim/loop/event_queue.cpp
	sim/loop/event_queue.h
	sim/loop/frame_crc.h
	sim/loop/game_loop.cpp
	sim/loop/game_loop.h
	sim/loop/recording.cpp
	sim/loop/recording.h
	sim/path/astar.cpp
	sim/path/astar.h
)
target_link_libraries(ra_sim PUBLIC ra_core)
ra_set_target_options(ra_sim)

if(RA_BUILD_APP)
	# Render: the GPU side of drawing maps and sprites (needs SDL, so only with the app).
	add_library(ra_render STATIC
		render/gl_api.cpp
		render/gl_api.h
		render/gl_util.cpp
		render/gl_util.h
		render/indexed_atlas.cpp
		render/indexed_atlas.h
		render/map_renderer.cpp
		render/map_renderer.h
		render/sprite_batch.cpp
		render/sprite_batch.h
	)
	target_link_libraries(ra_render PUBLIC ra_core ra_formats ra_world ra_rules ra_vfs ra_voxel SDL3::SDL3)
	ra_set_target_options(ra_render)
	

	configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

	if(ANDROID)
		# SDLActivity loads this library and calls SDL_main.
		add_library(re2yr SHARED platform/main.cpp)
		target_link_libraries(re2yr PRIVATE android log)
	else()
		add_executable(re2yr WIN32 platform/main.cpp)
	endif()
	target_include_directories(re2yr PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
	target_link_libraries(re2yr PRIVATE ra_core imgui SDL3::SDL3)
	ra_set_target_options(re2yr)
endif()
