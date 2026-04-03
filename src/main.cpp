
#include <SDL3/SDL_events.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>

#include <exception>

#include "vulkan/render.hpp"

int main() {
    quill::Logger* logger = quill::simple_logger();

    try {
        App app(logger);

        bool done = false;
        while (!done) {
            SDL_Event event;

            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        done = true;
                        break;
                    case SDL_EVENT_WINDOW_RESIZED:
                        app.flagResized();
                        break;
                }
            }

            app.drawFrame();
        }

        app.waitIdle();
    } catch (const std::exception& e) {
        quill::error(logger, e.what());
        return 1;
    }
    return 0;
}
