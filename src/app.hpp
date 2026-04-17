#ifndef SRC_APP_HPP_
#define SRC_APP_HPP_

#include <SDL3/SDL.h>

#include "vulkan/render.hpp"

class SDLApp {
protected:
    SDLApp();
    ~SDLApp() {
        SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    SDL_Window* window_;
};

class App : private SDLApp {
  public:
    explicit App();
    ~App();

    bool pollEvent(SDL_Event& event);
    inline void drawFrame() { renderer_.drawFrame(); }
    void close();

  private:
    SDL_Window* createWindow();

    VulkanRenderer renderer_;
};

#endif  // SRC_APP_HPP_