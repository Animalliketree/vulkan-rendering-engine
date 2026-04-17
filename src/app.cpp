#include "app.hpp"

#include <SDL3/SDL.h>

namespace {
constexpr char kAppTitle[] = "Game";
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
}  // namespace

SDLApp::SDLApp() {
    SDL_SetAppMetadata(kAppTitle, "0.0.1", "");
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow(kAppTitle, kWindowWidth, kWindowHeight,
                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (window_ == nullptr) throw std::runtime_error(
        "Failed to create SDL window: " + std::to_string(*SDL_GetError()));
}

App::App() :
    renderer_(window_) {}

App::~App() {}

bool App::pollEvent(SDL_Event& event) {
    bool result = SDL_PollEvent(&event);
    switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
            renderer_.flagResized();
            return result;
        default:
            return result;
    }
}
