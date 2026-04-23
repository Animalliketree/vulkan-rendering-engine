/* Copyright 2026 Alix Boivin */

#include <SDL3/SDL_events.h>
#include <chrono>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>

#include <exception>

#include "src/app.hpp"

int main() {
    quill::Logger* logger = quill::simple_logger();

    try {
        app::App app;

        bool done = false;
        float delta_time = 0.0f;
        while (!done) {
            SDL_Event event;
            const auto kStartTime = std::chrono::high_resolution_clock::now();
            quill::info(logger, "FPS: {}", 1 / delta_time);

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
            auto end_time = std::chrono::high_resolution_clock::now();
            delta_time = std::chrono::duration<float, 
                std::chrono::seconds::period>(end_time - kStartTime).count();
        }
    } catch (const std::exception& e) {
        quill::error(logger, e.what());
        return 1;
    }
    return 0;
}
