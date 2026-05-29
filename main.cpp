#include "Engine.hpp"


class DebugPanel : public IGuiLayer {
    float speed = 1.0f;
public:
    void onGui() override {
        ImGui::Begin("Debug");
        ImGui::SliderFloat("Speed", &speed, 0, 10);
        ImGui::End();
    }
};


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

    DebugPanel debugPanel;
    engine.pushLayer(&debugPanel);

    mainLoop(engine);

    engine.cleanup();

    getchar();
    return 0;
}
