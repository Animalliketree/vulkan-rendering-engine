# Vulkan Rendering Engine
[![Static Badge][version-badge]][changelog]
[![Static Badge][license-badge]][license]

A 3D Vulkan rendering engine that can render two rotating squares.

## Table of Contents
- [Main Objective](#main-objective)
- [Installation](#installation)
  - [Using CMake](#using-cmake)
- [Running the Program](#running-the-program)
- [Using the Program](#using-the-program)
- [How to Contribute](#how-to-contribute)
- [Gallery](#gallery)

## Main Objective
The project is unfinished! Why do I call this project an engine? The main objective of this project is to develop a library that can be integrated with other modules in a game engine. This would include an audio system, particle system, etc. With this in mind, since the project currently runs on its own and will be one of the main pillars of the larger picture, it can be considered an engine.

## Installation
The following libraries are required to run the program:
- [OpenGL Mathematics](https://github.com/g-truc/glm)
- [Quill](https://github.com/odygrd/quill)
- [SDL](https://www.libsdl.org/)
- [Volk](https://github.com/zeux/volk)
- [Vulkan](https://www.vulkan.org/)

### Using CMake
1. Download and extract the .zip file
2. Open a terminal in the extracted folder directory
3. Run `cmake CMakeLists.txt`.
4. Run `make`.

## Running the Program
To run the program, simply go to the repository and run `./VoxelEngine`.

## Using the Program
The program is currently only usable by running it. If you want to add code to the program, see the contributions section.

## How to Contribute
You can contribute with pull requests. If there's any part of the code that you can rewrite, then that works too. I will review every request by hand before adding it to the next version.

I (try to) follow [Google's C++ Style Guide](https://google.github.io/styleguide/cppguide.html).

## Gallery
![A really impressive image of two loosely-stacked warm-coloured squares](./screenshots/VulkanEngine_1.png)
![A really impressive image of two loosely-stacked cool-coloured squares rotated a bit more than the previous image](./screenshots/VulkanEngine_2.png)

[version-badge]: https://img.shields.io/badge/version-v0.0.1-blue
[changelog]: ./CHANGELOG.md
[license-badge]: https://img.shields.io/badge/license-MIT-blue
[license]: ./LICENSE
