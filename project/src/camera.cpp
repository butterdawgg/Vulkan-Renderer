#include "camera.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

Camera::Camera(GLFWwindow* window) : m_Window(window)
{ };

Camera::Camera(GLFWwindow* window, float fov, float aspect, float near, float far) :
    m_Window(window), m_Fov(fov), m_Aspect(aspect), m_NearClip(near), m_FarClip(far)
{ }

Camera::~Camera() = default;

void Camera::Update(float deltaTime)
{
    const bool wKey =
        glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS ||
        glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS;

    const bool aKey =
        glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS ||
        glfwGetKey(m_Window, GLFW_KEY_LEFT) == GLFW_PRESS;

    const bool sKey =
        glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS ||
        glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS;

    const bool dKey =
        glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS ||
        glfwGetKey(m_Window, GLFW_KEY_RIGHT) == GLFW_PRESS;

    const bool eKey = glfwGetKey(m_Window, GLFW_KEY_E) == GLFW_PRESS;
    const bool qKey = glfwGetKey(m_Window, GLFW_KEY_Q) == GLFW_PRESS;
    const bool shiftKey = glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

    glm::vec3 translationInput(0.0f);

    if (dKey && !aKey) translationInput.x = 1.0f;
    else if (aKey && !dKey) translationInput.x = -1.0f;

    if (eKey && !qKey) translationInput.y = 1.0f;
    else if (qKey && !eKey) translationInput.y = -1.0f;

    if (wKey && !sKey) translationInput.z = -1.0f;
    else if (sKey && !wKey) translationInput.z = 1.0f;

    if (glm::length(translationInput) > 0.0f)
        translationInput = glm::normalize(translationInput);

    double mouseX, mouseY;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);

    static double prevMouseX = mouseX;
    static double prevMouseY = mouseY;

    double deltaMouseX = mouseX - prevMouseX;
    double deltaMouseY = mouseY - prevMouseY;

    prevMouseX = mouseX;
    prevMouseY = mouseY;

    const bool lmb = glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool rmb = glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    const float shiftSpeedMult = 5.0f;
    const float keyboardSpeed = 5.0f * (shiftKey ? shiftSpeedMult : 1.0f) * deltaTime;
    const float mouseSpeed = 0.01f * (shiftKey ? shiftSpeedMult : 1.0f);
    const float radiansDelta = 0.005f;

    glm::vec2 localRotation(0.0f);
    glm::vec3 translation(0.0f);

    if (rmb && !lmb)
    {
        localRotation.x = radiansDelta * static_cast<float>(-deltaMouseY);
        localRotation.y = radiansDelta * static_cast<float>(-deltaMouseX);
        translation = translationInput * keyboardSpeed;
    }
    else if (!rmb && lmb)
    {
        translation.z = static_cast<float>(-deltaMouseY) * mouseSpeed;
        localRotation.y = radiansDelta * static_cast<float>(deltaMouseX);
    }
    else if (rmb && lmb)
    {
        translation.x = static_cast<float>(-deltaMouseX) * mouseSpeed;
        translation.y = static_cast<float>(deltaMouseY) * mouseSpeed;
    }
    else
    {
        translation = translationInput * keyboardSpeed;
    }

    m_TotalPitch += localRotation.x;
    m_TotalYaw += localRotation.y;

    m_TotalPitch = std::clamp(m_TotalPitch,
        -glm::half_pi<float>(), glm::half_pi<float>());

    GetTransform().SetRotation(glm::vec3(m_TotalPitch, m_TotalYaw, 0.0f));

    glm::vec3 forward = m_Transform.GetForward();
    glm::vec3 right = m_Transform.GetRight();
    glm::vec3 up = m_Transform.GetUp();

    glm::vec3 position = GetTransform().GetPosition();
    position += forward * translation.z;
    position += right * translation.x;
    position += up * translation.y;

    GetTransform().SetPosition(position);
}

float Camera::GetFOV() const { return m_Fov; }
void Camera::SetFOV(float fov) { m_Fov = fov; m_ProjMatrixDirty = true; }

float Camera::GetAspect() const { return m_Aspect; }
void Camera::SetAspect(float aspect) { m_Aspect = aspect; m_ProjMatrixDirty = true; }

float Camera::GetNearClip() const { return m_NearClip; }
void Camera::SetNearClip(float near) { m_NearClip = near; m_ProjMatrixDirty = true; }

float Camera::GetFarClip() const { return m_FarClip; }
void Camera::SetFarClip(float far) { m_FarClip = far; m_ProjMatrixDirty = true; }

float Camera::GetAperture() const { return m_Aperture; }
void Camera::SetAperture(float fNumber) { m_Aperture = fNumber; }

float Camera::GetShutterSpeed() const { return m_ShutterSpeed; }
void Camera::SetShutterSpeed(float seconds) { m_ShutterSpeed = seconds; }

float Camera::GetISO() const { return m_ISO; }
void Camera::SetISO(float iso) { m_ISO = iso; }

float Camera::GetEV100() const
{
    // EV100 = log2((N^2 / t) * (100 / ISO))
    return std::log2((m_Aperture * m_Aperture) / m_ShutterSpeed * 100.0f / m_ISO);
}

float Camera::GetExposure() const
{
    const float ev100 = GetEV100();
    const float maxLuminance = 1.2f * std::pow(2.0f, ev100);
    return 1.0f / maxLuminance;
}

Transform& Camera::GetTransform() { return m_Transform; }

glm::mat4 Camera::GetViewMatrix() const
{
    if (m_Transform.IsMatrixDirty())
        RecalculateViewMatrix();
    return m_ViewMatrix;
}

glm::mat4 Camera::GetProjMatrix() const
{
    if (m_ProjMatrixDirty)
        RecalculateProjMatrix();
    return m_ProjMatrix;
}

void Camera::RecalculateViewMatrix() const
{
    m_ViewMatrix = glm::inverse(m_Transform.GetModelMatrix());
}

void Camera::RecalculateProjMatrix() const
{
    m_ProjMatrix = glm::perspective(
        glm::radians(m_Fov), m_Aspect, m_NearClip, m_FarClip);

    m_ProjMatrix[1][1] *= -1.0f; // Vulkan clip-space correction

    m_ProjMatrixDirty = false;
}