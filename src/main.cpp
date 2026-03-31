#include <exception>

#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>

#include "vulkan/render.hpp"

int main() {
    quill::Logger* logger = quill::simple_logger();

    try {
        App app(logger);

        bool done = false;
        while (!done) {
            SDL_Event event;

            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    done = true;
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