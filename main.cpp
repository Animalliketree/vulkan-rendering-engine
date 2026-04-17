
#include <SDL3/SDL_events.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>

#include <exception>

#include "src/app.hpp"

int main() {
    quill::Logger* logger = quill::simple_logger();

    try {
        App app;

        bool done = false;
        while (!done) {
            SDL_Event event;

            while (app.pollEvent(event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        done = true;
                        break;
                    default:
                        break;
                }
            }

            app.drawFrame();
        }
    } catch (const std::exception& e) {
        quill::error(logger, e.what());
        return 1;
    }
    return 0;
}
