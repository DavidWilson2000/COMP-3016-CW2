#include "RingSystem.h"
#include "Shader.h"  

class Camera { public: glm::vec3 pos; };

                     static void BuildTorus(std::vector<RingVertex>& outV,
                         std::vector<unsigned int>& outI,
                         float majorR, float minorR,
                         int segMajor, int segMinor)
                     {
                         outV.clear();
                         outI.clear();

                         outV.reserve((segMajor + 1) * (segMinor + 1));
                         outI.reserve(segMajor * segMinor * 6);

                         for (int i = 0; i <= segMajor; i++)
                         {
                             float u = (float)i / (float)segMajor;
                             float a = u * 6.2831853f;
                             float ca = cos(a), sa = sin(a);

                             glm::vec3 center(ca * majorR, 0.0f, sa * majorR);

                             for (int j = 0; j <= segMinor; j++)
                             {
                                 float v = (float)j / (float)segMinor;
                                 float b = v * 6.2831853f;
                                 float cb = cos(b), sb = sin(b);

                                 // local circle around the tube
                                 glm::vec3 n = glm::normalize(glm::vec3(ca * cb, sb, sa * cb));
                                 glm::vec3 p = center + n * minorR;

                                 RingVertex rv;
                                 rv.pos = p;
                                 rv.normal = n;
                                 outV.push_back(rv);
                             }
                         }

                         int stride = segMinor + 1;
                         for (int i = 0; i < segMajor; i++)
                         {
                             for (int j = 0; j < segMinor; j++)
                             {
                                 int i0 = i * stride + j;
                                 int i1 = (i + 1) * stride + j;
                                 int i2 = i * stride + (j + 1);
                                 int i3 = (i + 1) * stride + (j + 1);

                                 outI.push_back(i0); outI.push_back(i1); outI.push_back(i2);
                                 outI.push_back(i2); outI.push_back(i1); outI.push_back(i3);
                             }
                         }
                     }

                     void RingSystem::InitMesh(float majorR, float minorR, int segMajor, int segMinor)
                     {
                         std::vector<RingVertex> v;
                         std::vector<unsigned int> idx;
                         BuildTorus(v, idx, majorR, minorR, segMajor, segMinor);

                         mesh.Destroy();

                         glGenVertexArrays(1, &mesh.vao);
                         glGenBuffers(1, &mesh.vbo);
                         glGenBuffers(1, &mesh.ebo);

                         glBindVertexArray(mesh.vao);

                         glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
                         glBufferData(GL_ARRAY_BUFFER, (GLsizei)v.size() * sizeof(RingVertex), v.data(), GL_STATIC_DRAW);

                         glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
                         glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizei)idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

                         glEnableVertexAttribArray(0); // aPos
                         glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RingVertex), (void*)offsetof(RingVertex, pos));

                         glEnableVertexAttribArray(1); // aNormal
                         glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RingVertex), (void*)offsetof(RingVertex, normal));

                         glBindVertexArray(0);

                         mesh.indexCount = (GLsizei)idx.size();
                     }

                     void RingSystem::Destroy()
                     {
                         mesh.Destroy();
                         rings.clear();
                     }

                     void RingSystem::Reset()
                     {
                         rings.clear();
                         score = 0;
                         collectedCount = 0;
                         totalCount = 0;
                     }

                     glm::mat4 RingSystem::RingModelMatrix(const Ring& r) const
                     {
                         glm::mat4 M(1.0f);
                         M = glm::translate(M, r.posWS);
                         M = glm::rotate(M, r.yaw, glm::vec3(0, 1, 0));
                         M = glm::rotate(M, r.pitch, glm::vec3(1, 0, 0)); // NEW: tilt
                         M = glm::scale(M, glm::vec3(r.scale));
                         return M;
                     }


                     int RingSystem::UpdateCollect(const glm::vec3& playerPosWS)
                     {
                         int got = 0;

                         float r2 = collectRadius * collectRadius;

                         for (auto& ring : rings)
                         {
                             if (ring.collected) continue;

                             glm::vec3 d = playerPosWS - ring.posWS;
                             float dist2 = glm::dot(d, d);

                             if (dist2 <= r2)
                             {
                                 ring.collected = true;
                                 got++;
                                 collectedCount++;
                                 score += pointsPerRing;
                             }
                         }

                         return got;
                     }

                     void RingSystem::Draw(Shader& shader,
                         const glm::mat4& view,
                         const glm::mat4& proj,
                         const Camera& cam,
                         const glm::vec3& sunDir,
                         const glm::vec3& sunCol,
                         bool fogEnabled,
                         const glm::vec3& fogColor,
                         float fogDensity,
                         float nightFactor)
                     {
                         if (mesh.vao == 0 || mesh.indexCount == 0) return;

                         shader.Use();

                         shader.SetMat4("uView", (float*)&view[0][0]);
                         shader.SetMat4("uProj", (float*)&proj[0][0]);

                         shader.SetVec3("uViewPos", cam.pos.x, cam.pos.y, cam.pos.z);
                         shader.SetVec3("uLightDir", sunDir.x, sunDir.y, sunDir.z);
                         shader.SetVec3("uLightColor", sunCol.x, sunCol.y, sunCol.z);

                         // Make rings shiny / readable
                         shader.SetFloat("uAmbientStrength", 0.35f);
                         shader.SetFloat("uSpecStrength", 0.85f);
                         shader.SetFloat("uShininess", 96.0f);

                         shader.SetFloat("uFogEnabled", fogEnabled ? 1.0f : 0.0f);
                         shader.SetVec3("uFogColor", fogColor.x, fogColor.y, fogColor.z);
                         shader.SetFloat("uFogDensity", fogDensity);

                         // If your lighthouse shader expects these, keep them valid (but make lantern contribution 0)
                         shader.SetFloat("uNightFactor", nightFactor);
                         shader.SetVec3("uLanternPosWS", 0.0f, -99999.0f, 0.0f);
                         shader.SetVec3("uLanternColor", 1.0f, 1.0f, 1.0f);
                         shader.SetFloat("uLanternIntensity", 0.0f);

                         glBindVertexArray(mesh.vao);

                         for (const auto& ring : rings)
                         {
                             if (ring.collected) continue;

                             glm::mat4 M = RingModelMatrix(ring);
                             shader.SetMat4("uModel", (float*)&M[0][0]);

                             glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
                         }

                         glBindVertexArray(0);
                     }
