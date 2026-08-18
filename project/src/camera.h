#ifndef CAMERA_H
#define CAMERA_H

#include "transform.h"

struct GLFWwindow;

class Camera
{
    public:

    Camera(GLFWwindow* window);
    Camera(GLFWwindow* window, float fov, float aspect, float near, float far);
    ~Camera();

    void Update(float deltaTime);

    Transform& GetTransform();
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjMatrix() const;

    float GetFOV() const;
    void SetFOV(float fov);

    float GetAspect() const;
    void SetAspect(float aspect);

    float GetNearClip() const;
    void SetNearClip(float near);

    float GetFarClip() const;
    void SetFarClip(float far);

    // Physical camera settings / exposure
    float GetAperture() const;
    void SetAperture(float fNumber);

    float GetShutterSpeed() const;
    void SetShutterSpeed(float seconds);

    float GetISO() const;
    void SetISO(float iso);

    // EV100 = log2( (N^2 / t) * (100 / ISO) )
    float GetEV100() const;

    // Linear exposure multiplier applied to HDR colour before tonemapping
    float GetExposure() const;

    private:

    GLFWwindow* m_Window;

    float m_TotalPitch { 0.0f };
    float m_TotalYaw { 0.0f };

    Transform m_Transform { };

    float m_Fov { 45.0f };
    float m_Aspect { 16.0f / 9.0f };
    float m_NearClip = { 0.1f };
    float m_FarClip = { 1000.0f };

    // Physical camera settings (defaults: bright outdoor exposure)
    float m_Aperture { 8.0f }; // f/8.0
    float m_ShutterSpeed { 1.0f / 1000.0f };
    float m_ISO { 200.0f };

    // Cached params
    mutable glm::mat4 m_ViewMatrix { };

    mutable bool m_ProjMatrixDirty { true };
    mutable glm::mat4 m_ProjMatrix { };

    void RecalculateViewMatrix() const;
    void RecalculateProjMatrix() const;
};

#endif // !CAMERA_H
