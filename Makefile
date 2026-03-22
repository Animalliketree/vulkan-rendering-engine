test:
	gcc src/vulkan/render.cpp src/main.cpp -o test.out -lvulkan -lSDL3 -lstdc++