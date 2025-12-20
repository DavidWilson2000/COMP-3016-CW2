#include <iostream>
#include <vector>
#include <cmath>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Shader.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

//  Camera state 
glm::vec3 gCamPos(0.0f, 6.0f, 14.0f);
glm::vec3 gCamFront(0.0f, 0.0f, -1.0f);
glm::vec3 gCamUp(0.0f, 1.0f, 0.0f);

float gYaw = -90.0f;
float gPitch = -20.0f;

float gLastX = 640.0f;
float gLastY = 360.0f;
bool gFirstMouse = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (gFirstMouse)
    {
        gLastX = (float)xpos;
        gLastY = (float)ypos;
        gFirstMouse = false;
    }

    float xoffset = (float)xpos - gLastX;
    float yoffset = gLastY - (float)ypos;
    gLastX = (float)xpos;
    gLastY = (float)ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    gYaw += xoffset;
    gPitch += yoffset;

    if (gPitch > 89.0f) gPitch = 89.0f;
    if (gPitch < -89.0f) gPitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(gYaw)) * cos(glm::radians(gPitch));
    front.y = sin(glm::radians(gPitch));
    front.z = sin(glm::radians(gYaw)) * cos(glm::radians(gPitch));
    gCamFront = glm::normalize(front);
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

//  Vertex struct 
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};

//  Noise helpers (no extra libs) 
static float hash2D(int x, int z, int seed)
{
    int h = x * 374761393 + z * 668265263 + seed * 1442695041;
    h = (h ^ (h >> 13)) * 1274126177;
    h ^= (h >> 16);
    return (h & 0x00FFFFFF) / 16777215.0f; // 0..1
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t); // smoothstep
}

static float valueNoise2D(float x, float z, int seed)
{
    int x0 = (int)floor(x);
    int z0 = (int)floor(z);
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    float sx = smooth(x - (float)x0);
    float sz = smooth(z - (float)z0);

    float n00 = hash2D(x0, z0, seed);
    float n10 = hash2D(x1, z0, seed);
    float n01 = hash2D(x0, z1, seed);
    float n11 = hash2D(x1, z1, seed);

    float ix0 = lerp(n00, n10, sx);
    float ix1 = lerp(n01, n11, sx);
    return lerp(ix0, ix1, sz); // 0..1
}

static float fbm(float x, float z, int seed, int octaves, float lacunarity, float gain)
{
    float amp = 0.5f;
    float freq = 1.0f;
    float sum = 0.0f;

    for (int i = 0; i < octaves; i++)
    {
        sum += amp * valueNoise2D(x * freq, z * freq, seed + i * 101);
        freq *= lacunarity;
        amp *= gain;
    }
    return sum; // approx 0..1
}

int main()
{
    // 1) Init GLFW
    if (!glfwInit())
    {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }

    // 2) Request OpenGL 4.x Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 3) Create window
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Procedural Island Prototype", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Mouse look
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // 4) Init GLEW
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        std::cerr << "Failed to init GLEW: " << glewGetErrorString(err) << "\n";
        glfwTerminate();
        return -1;
    }
    glGetError(); // clear harmless error from glewInit

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << "\n";
    std::cout << "GPU: " << glGetString(GL_RENDERER) << "\n";

    glEnable(GL_DEPTH_TEST);

    // 5) Load shader
    Shader shader("shaders/basic.vert", "shaders/basic.frag");

    //  Generate island terrain 
    const int gridSize = 200;     // quads per side
    const float spacing = 0.2f;   // distance between vertices
    const float half = (gridSize * spacing) * 0.5f;

    const int seed = 1337;
    const float radius = half * 0.9f;  // island edge inside grid
    const float heightScale = 6.0f;    // hill height
    const float noiseScale = 0.08f;    // noise zoom

    std::vector<Vertex> verts;
    verts.reserve((gridSize + 1) * (gridSize + 1));

    std::vector<unsigned int> indices;
    indices.reserve(gridSize * gridSize * 6);

    // vertices (now with height!)
    for (int z = 0; z <= gridSize; z++)
    {
        for (int x = 0; x <= gridSize; x++)
        {
            float worldX = x * spacing - half;
            float worldZ = z * spacing - half;

            float nx = worldX * noiseScale;
            float nz = worldZ * noiseScale;

            float n = fbm(nx, nz, seed, 5, 2.0f, 0.5f); // 0..1

            float dist = sqrt(worldX * worldX + worldZ * worldZ);
            float t = dist / radius;
            t = glm::clamp(t, 0.0f, 1.0f);

            float mask = 1.0f - t;
            mask = mask * mask * (3.0f - 2.0f * mask); // smooth falloff

            float height = (n * 2.0f - 1.0f) * heightScale * mask;

            Vertex v;
            v.pos = glm::vec3(worldX, height, worldZ);
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);

            verts.push_back(v);
        }
    }

    // indices (2 triangles per quad)
    for (int z = 0; z < gridSize; z++)
    {
        for (int x = 0; x < gridSize; x++)
        {
            int row1 = z * (gridSize + 1);
            int row2 = (z + 1) * (gridSize + 1);

            unsigned int i0 = row1 + x;
            unsigned int i1 = row1 + x + 1;
            unsigned int i2 = row2 + x;
            unsigned int i3 = row2 + x + 1;

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    //  Recompute normals 
    for (auto& v : verts)
        v.normal = glm::vec3(0.0f);

    for (size_t i = 0; i < indices.size(); i += 3)
    {
        unsigned int ia = indices[i];
        unsigned int ib = indices[i + 1];
        unsigned int ic = indices[i + 2];

        const glm::vec3& a = verts[ia].pos;
        const glm::vec3& b = verts[ib].pos;
        const glm::vec3& c = verts[ic].pos;

        glm::vec3 e1 = b - a;
        glm::vec3 e2 = c - a;

        glm::vec3 fn = glm::normalize(glm::cross(e1, e2));

        verts[ia].normal += fn;
        verts[ib].normal += fn;
        verts[ic].normal += fn;
    }

    for (auto& v : verts)
        v.normal = glm::normalize(v.normal);

    //  Upload terrain to GPU 
    GLuint VAO = 0, VBO = 0, EBO = 0;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    //  Main loop 
    float lastFrame = 0.0f;

    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Input
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        float camSpeed = 10.0f * deltaTime;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            gCamPos += camSpeed * gCamFront;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            gCamPos -= camSpeed * gCamFront;

        glm::vec3 camRight = glm::normalize(glm::cross(gCamFront, gCamUp));
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            gCamPos -= camRight * camSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            gCamPos += camRight * camSpeed;

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            gCamPos -= gCamUp * camSpeed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            gCamPos += gCamUp * camSpeed;

        // Render
        glClearColor(0.05f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = glm::lookAt(gCamPos, gCamPos + gCamFront, gCamUp);
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1280.0f / 720.0f, 0.1f, 500.0f);

        shader.Use();
        shader.SetMat4("uModel", glm::value_ptr(model));
        shader.SetMat4("uView", glm::value_ptr(view));
        shader.SetMat4("uProj", glm::value_ptr(proj));

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteBuffers(1, &EBO);
    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);

    glfwTerminate();
    return 0;
}
