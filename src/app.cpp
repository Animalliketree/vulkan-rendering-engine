#include "app.hpp"

#include <SDL3/SDL.h>
#include <cassert>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>

namespace {
constexpr char kAppTitle[] = "Game";
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
}  // namespace

namespace app {
SDLWindow::SDLWindow() noexcept {
    SDL_SetAppMetadata(kAppTitle, "0.0.1", "");
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow(kAppTitle, kWindowWidth, kWindowHeight,
                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        quill::Logger* log = quill::simple_logger();
        quill::error(log, "Failed to create SDL window: {}", SDL_GetError());
    }

    assert(window_ != nullptr);
}

bool App::pollEvent(SDL_Event& event) {
    bool result = SDL_PollEvent(&event);
    switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
            renderer_.flagResized();
            [[fallthrough]];
        default:
            return result;
    }
}
}  // namespace app
