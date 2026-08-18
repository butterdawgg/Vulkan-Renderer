#ifndef RENDERER_H
#define RENDERER_H

#include <memory>

#include "render_settings.h"

struct GLFWwindow;
class Scene;
class Camera;

class Renderer final
{
    public:

    Renderer(GLFWwindow* window, const RenderSettings& settings = {});

    void LoadScene(Scene& scene);

    void DrawFrame(Camera& camera);

    void DeviceWaitIdle();

    RenderSettings& GetSettings();

    ~Renderer();

    private:

    class Impl;
    std::unique_ptr<Impl> m_Impl;
};

#endif // !RENDERER_H