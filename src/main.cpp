#include <exception>

#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>

#include "vulkan/render.hpp"

int main() {
    quill::Logger* logger = quill::simple_logger();

    App app(logger);

    try {
        app.mainLoop();
    } catch (const std::exception& e) {
        quill::error(logger, e.what());
        return 1;
    }
    return 0;
}