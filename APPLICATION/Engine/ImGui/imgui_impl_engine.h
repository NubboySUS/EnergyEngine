#pragma once

struct ImGuiContext;

namespace Engine { class Window; }

bool ImGui_ImplEngine_Init(Engine::Window* window);
void ImGui_ImplEngine_Shutdown();
void ImGui_ImplEngine_NewFrame();
