/* Copyright 2026 Alix Boivin */

#ifndef SRC_APP_HPP_
#define SRC_APP_HPP_

#include <SDL3/SDL.h>

#include "graphics/vulkan_renderer.hpp"

namespace app {
class SDLWindow {
 protected:
    SDLWindow() noexcept;
    SDLWindow(const SDLWindow&) = delete;
    SDLWindow& operator=(const SDLWindow&) = delete;

    ~SDLWindow() noexcept {
        SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    SDL_Window* window_;
};

class App : private SDLWindow {
 public:
    explicit App() noexcept : renderer_(window_) {}
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool pollEvent(SDL_Event& event) noexcept;
    inline void drawFrame() { renderer_.drawFrame(); }

 private:
    graphics::vk_renderer::VulkanRenderer renderer_;
};
}  // namespace app

#endif  // SRC_APP_HPP_
