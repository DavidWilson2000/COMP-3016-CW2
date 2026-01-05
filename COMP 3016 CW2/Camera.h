#pragma once
#include <glm/glm/glm.hpp>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

class Camera
{
public:
    glm::vec3 pos{ 0.0f, 6.0f, 14.0f };
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    float yaw = -90.0f;
    float pitch = -20.0f;
    float boostLerp = 1.0f;

    void ProcessKeyboard(GLFWwindow* window, float dt, float speedMul)
    {

        float speed = 10.0f * dt * speedMul * boostLerp;
        float targetBoost = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 2.2f : 1.0f;
        boostLerp += (targetBoost - boostLerp) * glm::clamp(dt * 6.0f, 0.0f, 1.0f);


        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) pos += speed * front;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) pos -= speed * front;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) pos.y += speed;


        glm::vec3 right = glm::normalize(glm::cross(front, up));
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) pos -= speed * right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) pos += speed * right;
    }

    void ProcessMouse(float xpos, float ypos)
    {
        if (firstMouse)
        {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos;

        lastX = xpos;
        lastY = ypos;

        float sensitivity = 0.1f;
        yaw += xoffset * sensitivity;
        pitch += yoffset * sensitivity;

        pitch = glm::clamp(pitch, -89.0f, 89.0f);

        glm::vec3 f;
        f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y = sin(glm::radians(pitch));
        f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(f);
    }

    glm::mat4 ViewMatrix() const
    {
        return glm::lookAt(pos, pos + front, up);
    }

private:
    float lastX = 640.0f, lastY = 360.0f;
    bool firstMouse = true;
};