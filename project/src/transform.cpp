#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#include "transform.h"

Transform::Transform(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) :
    m_Position(position), m_Rotation(rotation), m_Scale(scale)
{ }

bool Transform::IsMatrixDirty() const
{
    return m_ModelMatrixDirty;
}

glm::mat4 Transform::GetModelMatrix() const
{
    if (m_ModelMatrixDirty)
        RecalculateModelMatrix();

    return m_ModelMatrix;
}

glm::vec3 Transform::GetPosition() const
{
    return m_Position;
}

void Transform::SetPosition(glm::vec3 position)
{
    m_Position = position; m_ModelMatrixDirty = true;
}

glm::vec3 Transform::GetRotation() const
{
    return m_Rotation;
}

void Transform::SetRotation(glm::vec3 rotation)
{
    m_Rotation = rotation;
    m_ModelMatrixDirty = true;
    m_DirectionDirty = true;
}

glm::vec3 Transform::GetScale() const
{
    return m_Scale;
}

void Transform::SetScale(glm::vec3 scale)
{
    m_Scale = scale; m_ModelMatrixDirty = true;
}

glm::vec3 Transform::GetForward() const
{
    if (m_DirectionDirty)
        RecalculateDirections();

    return m_Forward;
}

glm::vec3 Transform::GetRight() const
{
    if (m_DirectionDirty)
        RecalculateDirections();

    return m_Right;
}

glm::vec3 Transform::GetUp() const
{
    if (m_DirectionDirty)
        RecalculateDirections();

    return m_Up;
}

void Transform::RecalculateModelMatrix() const
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);

    glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f),
        m_Rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));

    glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f),
        m_Rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f),
        m_Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));

    glm::mat4 rotation = rotationZ * rotationY * rotationX;

    glm::mat4 scale = glm::scale(glm::mat4(1.0f), m_Scale);

    m_ModelMatrix = translation * rotation * scale;
    m_ModelMatrixDirty = false;
}

void Transform::RecalculateDirections() const
{
    glm::mat4 rot =
        glm::yawPitchRoll(
            m_Rotation.y,
            m_Rotation.x,
            m_Rotation.z);

    m_Forward = glm::normalize(glm::vec3(rot * glm::vec4(0, 0, 1, 0)));
    m_Right = glm::normalize(glm::vec3(rot * glm::vec4(1, 0, 0, 0)));
    m_Up = glm::normalize(glm::vec3(rot * glm::vec4(0, 1, 0, 0)));

    m_DirectionDirty = false;
}