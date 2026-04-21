#include "Engine.hpp"

void mainLoop(Engine &engine)
{
    while (!glfwWindowShouldClose(engine.window))
    {
        glfwPollEvents();
        engine.drawFrame();
    }
}

int main(int argc, char **argv)
{
    Engine engine = Engine();
    engine.initWindow();
    engine.initVulkan();
    mainLoop(engine);

    engine.cleanup();

    getchar();
    return 0;
}
