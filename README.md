<div align="center">

<img src="media/loading.gif">

**A from-scratch Vulkan renderer, built as a clean template to start your own engine from.**

<br>

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3-AC162C?style=for-the-badge&logo=vulkan&logoColor=white)
![Slang](https://img.shields.io/badge/Shaders-Slang-7B61FF?style=for-the-badge)
![CMake](https://img.shields.io/badge/CMake-3.22+-064F8C?style=for-the-badge&logo=cmake&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-MoltenVK-000000?style=for-the-badge&logo=apple&logoColor=white)
![Status](https://img.shields.io/badge/status-in%20development-F59E0B?style=for-the-badge)

<br>

<img src="media/black_glass_ad.gif" alt="Black Glass rendering a concept car" width="860">

</div>

<br>

## What is it

Black Glass is a small, readable physically based renderer written directly against Vulkan. There is no engine framework underneath it: windowing, memory, descriptors, passes and shaders are all in plain view.

It is meant to be a **standard template to start from**. Clone it, load your own glTF models, then cut it down or build on top of it.

<br>

<div align="center">
<img src="media/glass.gif" alt="Flying through the car's glass roof" width="860">
<br>
<sub>Real transmission: the camera looks through the glass roof into the cabin.</sub>
</div>

<br>

## Features

| | |
|---|---|
| **Lighting** | GGX PBR, image-based lighting, clearcoat, directional shadows |
| **Glass** | Transmission and sorted alpha blending |
| **Post** | HDR pipeline, 4x MSAA, bloom, SSAO, ACES / AgX / Khronos Neutral tonemapping |
| **Scene** | glTF loading, model library, click-to-place, glossy floor reflections |
| **Tooling** | Dear ImGui panel, runtime debug views, VMA memory allocation |

<br>

<div align="center">
<img src="media/selection_test.gif" alt="Selection test" width="860">
<br>
<sub>Selection: The selection tool in action.</sub>
</div>

<br>


## Quick start

You need the Vulkan SDK (for `slangc`), GLFW and CMake.

```bash
./build.sh
```

Models are read from `src/assets/`. Open the **Model library** panel to import one, then place it in the scene.

<br>

## Controls

| Input | Action |
|---|---|
| Hold **right mouse** + `W A S D` | Fly the camera (`Space` up, `Shift` down) |
| **Left click** | Select a model, or place the ghost preview |
| **Drag** / **Scroll** | Move a model / rotate it |
| `Delete` | Remove the selected model |
| `Tab` | Toggle fly mode |
| `F1` | Hide the UI |
| `Esc` | Quit (asks first) |

<br>

<div align="center">
<sub>Built with Vulkan, GLFW, GLM, tinygltf, stb_image, Dear ImGui and VMA.</sub>
</div>
