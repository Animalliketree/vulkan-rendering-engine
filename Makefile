SHADERS_DIR=./shaders
SPIRV_PROFILE=spirv_1_4
ENTRY_POINTS=-entry vertMain -entry fragMain

test:
	gcc src/vulkan/render.cpp src/main.cpp -o test.out -lvulkan -lSDL3 -lstdc++

shader:
	slangc ${SHADERS_DIR}/graphics.slang -target spirv -profile ${SPIRV_PROFILE} -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o shaders/slang.spv