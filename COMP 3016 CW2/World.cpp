#include "World.h"

#include <iostream>
#include <algorithm>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/constants.hpp>

//
// IMPORTANT: World.cpp must be able to see the definitions of these types.
// Since you currently have them inside main.cpp, Step 1 assumes you will
// MOVE these structs/classes to headers soon.
// For now, easiest “minimal change” is:
//
//   - Temporarily include your main header that defines Island/Terrain/Water/GLModel
//   - OR copy the struct/class definitions into their own headers.
//
// If you don’t have those headers yet, do this:
//   1) Create "WorldTypes.h"
//   2) Move Island, Terrain, Water, TreeSystem, GLModel, WorldConfig, IslandBiome into it
//   3) Include it here and in main.cpp
//

#include "WorldTypes.h"
