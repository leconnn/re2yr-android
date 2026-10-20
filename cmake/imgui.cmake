# Dear ImGui (debug UI) with the SDL3 and OpenGL 3 / ES 3 backends.

set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/external/imgui)

add_library(imgui STATIC
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_demo.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
	${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
if(ANDROID)
	target_link_libraries(imgui PUBLIC GLESv3)
endif()
