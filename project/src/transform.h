#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <glm/glm.hpp>

class Transform
{
    public:

    Transform() { }

    Transform(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale);

    glm::vec3 GetPosition() const;
    void SetPosition(glm::vec3 position);

    glm::vec3 GetRotation() const;
    void SetRotation(glm::vec3 rotation);

    glm::vec3 GetScale() const;
    void SetScale(glm::vec3 scale);

    bool IsMatrixDirty() const;
    glm::mat4 GetModelMatrix() const;

    glm::vec3 GetForward() const;
    glm::vec3 GetRight() const;
    glm::vec3 GetUp() const;

    private:

    glm::vec3 m_Position { };
    glm::vec3 m_Rotation { };
    glm::vec3 m_Scale { 1.0f, 1.0f, 1.0f };

    mutable bool m_ModelMatrixDirty { true };
    mutable glm::mat4 m_ModelMatrix { };

    mutable bool m_DirectionDirty { true };
    mutable glm::vec3 m_Forward { 0, 0, 1 };
    mutable glm::vec3 m_Right { 1, 0, 0 };
    mutable glm::vec3 m_Up { 0, 1, 0 };

    void RecalculateModelMatrix() const;
    void RecalculateDirections() const;
};

#endif // !TRANSFORM_H