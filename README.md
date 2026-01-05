Procedural Island Exploration (OpenGL C++)
David Wilson - 10781607

GitHub Repository: https://github.com/DavidWilson2000/COMP-3016-CW2

Video Showcase:

Project Overview


This project is a playable OpenGL C++ scene demonstrating real-time rendering, procedural content generation, textured 3D models, lighting, audio, and player interaction.

The app presents a procedurally generated island environment surrounded by ocean, featuring multiple biomes, collectible objects, lighthouse navigation cues, a scoring system and audio prompts. The player explores the world using keyboard and mouse controls with the objective of collecting all rings while exploring the scene.

Gameplay Description

The player explores a procedurally generated island environment using first-person camera controls. The primary objective is to locate and collect all rings distributed across the islands. Environmental conditions such as fog, storms, and lighting can be toggled, affecting visibility and navigation. Lighthouses act as visual navigation aids, particularly during poor visibility. Progress is tracked via an on-screen score counter.

## Dependencies Used

- OpenGL – core graphics API
  
- GLFW – window creation and input handling
  
- GLEW – OpenGL extension loading
  
- GLM – mathematics and matrix operations
  
- Assimp – 3D model loading (.glb)

- stb_image – texture loading (.png)
  
- irrKlang – audio playback  


## To build:
Open the project in Visual Studio (the sln called COMP3016 CW2.sln)
<img width="661" height="206" alt="image" src="https://github.com/user-attachments/assets/6e7e9ef6-215c-4daa-8d32-065f585aa3dd" />

Ensure all required libraries are linked
Build and run main.cpp


## Controls
Keyboard
W A S D – Move camera

Space – Rise up

Shift - Sprint

R - Rebuild the world

F- Toggle Fog

P - Toggle Wireframe

O - Toggle Storm

H - Toggle Help

ESC - Quit

B – Toggle lighthouse beam / lighting effects

<img width="988" height="660" alt="image" src="https://github.com/user-attachments/assets/e8d83e90-8611-40fa-9279-5ec6e172a127" />

Mouse
Mouse movement controls camera rotation (fluid first-person navigation)

## Features Implemented 
Core Requirements 

OpenGL C++ application with vertex and fragment shaders

Compilable code with working GL window

Textured 3D scene

Signature displayed within the scene (shown in video)

Git repository with matching submission

10-minute video showcase

External resources cited

## MVP Features 
1. Textures (loaded using shader loaders for basic textures and then using STB_IMAGE_IMPLEMENTATION for more complex textures)

Multiple textures applied across terrain, models, and collectibles

Terrain uses biome-based texturing (sand, grass, rock, snow)

Textured models loaded via Assimp (using glb models) and loaded using OBJ loading

2. 3D Polygons with Scene Animation 

Scene composed of multiple 3D meshes (terrain, trees, lighthouse, rings, houses)

Time-based animation using glfwGetTime() (environmental movement and effects)

3. Keyboard and Mouse Movement 

Classic keyboard movement for full 3D navigation using WASD

Mouse-based camera rotation that allows the player full freedom in movement 

4. Model Loading with Textures 

Multiple 3D models loaded using Assimp

Models use different formats (e.g. .obj, .glb)

Textures correctly bound and rendered

5. Procedural Content Generation 

Procedural island terrain generated using height variation to help decide biome

Multiple biomes implemented (sand, grass, rock, snow) and islands assigned a "biome" when generated to allow for more variation 

Biome distribution based on height and noise functions

## Advanced Features 
1. Dynamic Lighting – Blinn-Phong 

Blinn-Phong lighting model implemented in shaders

Dynamic lighthouse lighting that cuts throught the fog and dynamically roatates around the lighthouse to help with navigation when fog is on

Lighting changes over time with a day night system 

Lighting can be impaced by the fog making the world dimmer and the storm function tht further increases fog 

2. Audio 

Background ambient audio of the waves

Interactive sound effects triggered by player actions (e.g. collecting rings)

Audio system integrated into gameplay loop that help increase immersion

-

## Gamification

Collectible rings distributed throughout the procedurally generated world

On-screen score counter tracking player progress


The project functions as a playable exploration game, not just a static scene

Ring Collection System (Legacy-Inspired Gamification)

The core gamification mechanic in this project is the ring collection system, which was inspired by the flight ring challenges in Superman (Nintendo 64, 1999). In that game, rings were used as spatial navigation challenges, encouraging players to traverse 3D space.

In this project, that legacy mechanic is reinterpreted using modern game design principles. Rings are placed procedurally across island biomes to act as:

Clear short-term goals

Spatial navigation guides

Feedback-driven rewards (score increase + audio cue)

This aligns with contemporary research and industry discussion on collectibles as intrinsic motivation tools, where simple, repeatable objectives reinforce player engagement through feedback loops rather than punishment-based failure states.

Recent game design research highlights that collectibles are most effective when they:

Provide immediate audiovisual feedback

Encourage exploration rather than linear completion

Support player autonomy and self-directed goals


More recent industry-facing discussion reinforces this approach, noting that collectible-based navigation mechanics remain effective when combined with modern feedback systems and procedural placement:

Example https://medium.com/@rakeshroyakula/designing-reward-loops-that-keep-players-hooked-without-manipulation-58447c858d4a

In implementation, rings are collected using bounding-radius distance checks between the player camera and ring positions. Upon collection:

The score counter is updated

An audio cue is triggered

Visual confirmation is provided through ring removal

This system transforms a historically criticised mechanic into a low-pressure, exploration-focused reward loop, suitable for a modern procedural environment.

This approach also aligns with modern interpretations of the Mechanics–Dynamics–Aesthetics (MDA) framework, which continues to be referenced in contemporary game design research and teaching (https://netlibrary.aau.at/obvuklhs/content/titleinfo/10084329/full.pdf).


## Game Mechanics and Implementation

- Ring Collection: (see above)
  
- Procedural Terrain Generation: Terrain height values are generated using noise functions, with biome assignment determined by height thresholds.
  
- Environmental Toggles: Fog, storms, wireframe mode, and lighting effects are controlled via keyboard input and implemented through shader uniforms.
  
- Dynamic Lighting: Lighthouse lighting rotates over time using trigonometric functions applied to the light direction vector.

-

## Game Programming Patterns Used

- Object-Oriented Programming (OOP): Core systems such as Camera, World, AudioSystem, and RingSystem are encapsulated into separate classes with clear responsibilities.
  
- Central Game Loop: The application follows a traditional update–render loop structure.
  
- System-Based Design: Rendering, audio, input handling, and world generation are separated into logical systems.
  
- Event-Driven Input: Keyboard and mouse input is handled via GLFW callbacks and state polling.

-

## Code Structure & OOP Design

The project follows an object-oriented design approach:

Camera – Handles player view and movement

World – Manages terrain generation and environment setup

Ring collection: implemented via bounding radius checks between the player camera and ring positions, updating score state when triggered.

Shader – Encapsulates shader compilation and uniform handling

AudioSystem – Manages sound playback and events

Each class has a clear responsibility and is documented within the code.

-

## Exception Handling and Testing

- Centralised logging is used throughout the application via `LogInfo`, `LogWarn`, and `LogError`, and GLFW runtime errors are captured using `glfwSetErrorCallback(GLFWErrorCallback)`.
<img width="808" height="107" alt="image" src="https://github.com/user-attachments/assets/c58311c2-6370-465b-8fd5-6301a715c4cd" />

- Critical startup failures are handled safely using early returns:
  - If `glfwInit()` fails the program logs the error and exits `Init()`.
  - If `glfwCreateWindow()` fails the program terminates GLFW and exits `Init()`.
  - If `glewInit()` fails the program logs the specific GLEW error string and exits `Init()`.

- OpenGL diagnostics are supported using the KHR_debug extension where available (`GLEW_KHR_debug`). When supported, a debug callback is enabled to output OpenGL errors at runtime; if not supported, the application continues safely with warning output.
<img width="415" height="184" alt="image" src="https://github.com/user-attachments/assets/39b30be3-7492-4468-bd0a-d532dc1aa050" />

- Shader setup is validated before use with `RequireShader(...)`. If a shader pointer is null or fails to link (`linkedOk == false`), an error is logged and the feature is disabled. If critical shaders fail (terrain/sky/water), the application stops early to prevent running in an invalid state.
<img width="705" height="238" alt="image" src="https://github.com/user-attachments/assets/da4c56b3-73a7-4653-801d-40cfce2085a4" />

- Texture loading is protected with a safe loader (`LoadTexture2D_Safe`). Missing or failed texture loads are detected (`FileExists` / `stbi_load` failure) and replaced with a fallback texture, preventing crashes and making missing assets visually obvious.
<img width="700" height="254" alt="image" src="https://github.com/user-attachments/assets/453681d9-62bb-406e-ac3f-bb5cefebeafc" />

- Model loading includes validation and logging:
  - OBJ loading checks file open success (`std::ifstream`), logs failures, and returns `false` safely so the application can continue without that asset.
<img width="372" height="183" alt="image" src="https://github.com/user-attachments/assets/06dda18e-98b0-4234-aff2-cd52e5091658" />

- Manual test cases were performed to verify stability across:
  - Player movement (keyboard + mouse), sprinting, and quitting
    
  - Ring collection and score updates
    
  - Audio triggers (UI clicks, ring collect, storm thunder, regen)
    
  - Procedural regeneration (`R`) and biome variation
    
  - Fog, storm, wireframe, help overlay, and lighthouse beam toggles
 

-
All this is loaded in the console to make it easy to understand any issues that may arise
<img width="979" height="513" alt="image" src="https://github.com/user-attachments/assets/054d80da-db43-4e65-8d14-d0330c81c587" />
<img width="1045" height="708" alt="image" src="https://github.com/user-attachments/assets/e8645123-e8fb-42bc-b42c-eaeafa0344b5" />


## External Resources & References

All external assets and tutorials are properly credited.

Examples:

LearnOpenGL – shader structure and lighting models

Assimp documentation – model loading

Texture assets – sourced from free, non-commercial libraries (see comments in code)

Full citations are included in the report.

-

## Video Demonstration

The accompanying 10-minute video demonstrates:

Scene overview

Procedural generation

Biomes

Camera controls

Lighting and audio

Collectibles and scoring system

All claimed features are visibly demonstrated.

## Evaluation and Reflection

This project successfully demonstrates a complete real-time 3D OpenGL application featuring procedural content generation, interaction, lighting, and audio. I am particularly satisfied with the procedural biome system and dynamic lighting effects. If extended further, I would improve biome blending transitions, introduce AI-driven entities (villegers?), and add a graphical UI for real-time parameter adjustment to make it feel more like a game. Overall, this project significantly improved my understanding of modern OpenGL rendering pipelines and real-time game system architecture and was also very fun to make.



## Use of AI
Uses of AI have been valuable in this project, it has been mainly used for 3 distinct factors

- Troubleshooting 

AI has been used when the code written has not compiled in a way that i expected and i was having issues with correcting it

<img width="882" height="798" alt="image" src="https://github.com/user-attachments/assets/23757b66-9bdc-4b62-a332-d5e727125aaf" />

This was helpful as it allowed me to spend more time on the aspects of the game that I found enjoyable while also allowing me to not get caught stuck on one segment of code for extended periods. 

- Mark Estimation and ensuring that I had all the features needed
- <img width="564" height="795" alt="image" src="https://github.com/user-attachments/assets/146d0726-3832-4915-9b86-b1a9d9443f77" />
<img width="773" height="751" alt="image" src="https://github.com/user-attachments/assets/84443577-8764-46a9-9372-7e71578782e9" />

This has been helpful in ensuring that I had all the features and more importantly in identifying where in the marking rubric my work was a little weaker, early on it was able to identify that I had missed the last 10% and was making a game with no Gamification which allowed me to add the ring system 

- Code Structure

- I have had issues in the past where I do not follow proper OOP structure and instead of splitting my code into different files <img width="518" height="415" alt="image" src="https://github.com/user-attachments/assets/3b40a017-3372-4d93-abe6-be0d88e63b5e" />
- I would instead put all the code into one main "block" (main.cpp) and that makes it much worse. I had noticed i was doing that this time and used ChatGPT to help me split up the code into more files to make the code easiser to read instead of one giant block
- <img width="958" height="628" alt="image" src="https://github.com/user-attachments/assets/84ce418d-7eb4-446a-be42-59f69b7f7fb3" />



