## For build
Install imgui
```bash
git submodule init
git submodule update
```
Build project first time:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
After:
```bash
cmake --build build

cmake --build build --target shaders    // Compile only shaders
```
## Run
```bash
build/bin/VulkanEngine
```
