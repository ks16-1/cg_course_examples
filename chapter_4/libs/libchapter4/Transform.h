#pragma once
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/quaternion.hpp>

// Преобразует координаты из локальных в мировые в следующем порядке:
//  - сначала вершины масштабируются
//    например, единичный цилиндр превращается в диск или в трубку
//  - затем поворачиваются
//    т.е. тела ориентируются в пространстве
//  - затем переносятся
//    т.е. задаётся положение тела
// изменив порядок, мы изменили бы значение трансформаций.
class CTransform3D
{
public:
    // Конструирует трансформацию с
    //  - единичным масштабированием;
    //  - нулевым вращением вокруг оси Oy;
    //  - нулевой позицией.
    CTransform3D();

    // Преобразует исходную трансформацию в матрицу 4x4.
    glm::mat4 ToMat4()const;

    glm::vec3 m_sizeScale;
    glm::quat m_orientation;
    glm::vec3 m_position;
};
