#include <GLFW/glfw3.h>

#include "debug_utils.h"
#include "app.h"
#include "renderer.h"
#include "render_settings.h"
#include "scene.h"
#include "camera.h"
#include "mesh.h"
#include "lighting.h"

App::App()
{
    // ------------------
    // --- GLFW setup ---
    // ------------------

    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_WindowWidth = 1280;
    m_WindowHeight = 720;

    m_Window = glfwCreateWindow(m_WindowWidth, m_WindowHeight,
        "Vulkan Deferred PBR Renderer", nullptr, nullptr);

    if (!m_Window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create a GLFW window");
    }

    glfwGetWindowSize(m_Window, &m_WindowWidth, &m_WindowHeight);



    // --------------------
    // --- Vulkan setup ---
    // --------------------

    try
    {
        // Configurable rendering parameters
        RenderSettings renderSettings { };
        renderSettings.shadowMapResolution = 4096;
        renderSettings.shadowBias = 0.0015f;
        renderSettings.shadowDepthBiasConstant = 1.25f;
        renderSettings.shadowDepthBiasSlope = 1.75f;
        renderSettings.ambientColor = glm::vec3(10.f, 10.f, 10.f);
        renderSettings.iblIntensity = 100.0f;

        m_Renderer = std::make_unique<Renderer>(m_Window, renderSettings);



        // --- Sponza scene ---
        m_Camera = std::make_unique<Camera>(m_Window);
        m_Camera->SetAspect(static_cast<float>(m_WindowWidth) /
            static_cast<float>(m_WindowHeight));

        m_Camera->SetAperture(8.0f);
        m_Camera->SetShutterSpeed(1.0f / 1000.0f);
        m_Camera->SetISO(400.0f);
        m_Camera->GetTransform().SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));

        m_Scene = std::make_unique<Scene>();
        m_Scene->SetEnvironmentMap("resources/ibl/desert_env.hdr");
        m_Scene->LoadFromGltf("resources/models/sponza/Sponza.gltf");

        auto& lighting = m_Scene->GetLighting();

        DirectionalLight& sun = lighting.GetDirectional();
        sun.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        sun.color = glm::vec3(1.0f, 0.96f, 0.9f);
        sun.illuminanceLux = 80000.0f;

        auto& points = lighting.GetPointLights();
        points.push_back(PointLight {
            glm::vec3(4.0f, 3.0f, -1.5f), glm::vec3(1.0f, 0.6f, 0.3f), 100000.0f, 20.0f });
        points.push_back(PointLight {
            glm::vec3(-4.0f, 3.0f, 0.5f), glm::vec3(0.4f, 0.6f, 1.0f), 100000.0f, 20.0f });

        // --- Chess scene ---
        //m_Camera = std::make_unique<Camera>(m_Window);
        //m_Camera->SetAspect(static_cast<float>(m_WindowWidth) /
        //    static_cast<float>(m_WindowHeight));
        //m_Camera->SetAperture(4.0f);
        //m_Camera->SetShutterSpeed(1.0f / 125.0f);
        //m_Camera->SetISO(1600.0f);
        //m_Camera->GetTransform().SetPosition(glm::vec3(0.0f, 0.2f, 1.0f));
        //
        //m_Scene = std::make_unique<Scene>();
        //m_Scene->SetEnvironmentMap("resources/ibl/circus_arena_1k.hdr");
        //m_Scene->LoadFromGltf("resources/models/ABeautifulGame/ABeautifulGame.gltf");
        //
        //auto& lighting = m_Scene->GetLighting();
        //
        //DirectionalLight& sun = lighting.GetDirectional();
        //sun.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        //sun.color = glm::vec3(1.0f, 0.96f, 0.9f);
        //sun.illuminanceLux = 200.0f;



        m_Renderer->LoadScene(*m_Scene);
    }
    catch (std::exception& e)
    {
        logCaughtException(e.what());
        m_ShouldTerminate = true;
    }
}

void App::Run()
{
    auto shouldExit = [this] () -> bool
        {
            return glfwWindowShouldClose(m_Window) || m_ShouldTerminate;
        };

    while (!shouldExit())
    {
        glfwPollEvents();

        using clock = std::chrono::steady_clock;
        using seconds = std::chrono::duration<float>;

        const auto frameStartTime { clock::now() };
        const float deltaTime { seconds(frameStartTime - m_LastUpdateTime).count() };
        m_LastUpdateTime = frameStartTime;

        try
        {
            m_Camera->Update(deltaTime);
            m_Renderer->DrawFrame(*m_Camera);
        }
        catch (std::exception& e)
        {
            logCaughtException(e.what());
            break;
        }
    }

    if (m_Renderer)
        m_Renderer->DeviceWaitIdle();
}

App::~App()
{
    m_Renderer.reset(nullptr);

    if (m_Window)
        glfwDestroyWindow(m_Window);

    glfwTerminate();
}