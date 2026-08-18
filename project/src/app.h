#ifndef APP_H
#define APP_H

#include <memory>
#include <chrono>

struct GLFWwindow;
class Renderer;
class Scene;
class Camera;

class App final
{
    public:

    App();

    void Run();

    ~App();

    private:

    GLFWwindow* m_Window { };
    int m_WindowWidth { };
    int m_WindowHeight { };

    std::unique_ptr<Renderer> m_Renderer { nullptr };
    bool m_ShouldTerminate { false };

    std::unique_ptr<Scene> m_Scene { nullptr };
    std::unique_ptr<Camera> m_Camera { nullptr };

    std::chrono::steady_clock::time_point m_LastUpdateTime {
        std::chrono::high_resolution_clock::now() };
};

#endif // !APP_H