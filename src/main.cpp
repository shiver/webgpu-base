#include <assert.h>

#include <GLFW/glfw3.h>
#include "webgpu.cpp"

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

static WebGPU wgpu;

void render() {
    WGPUTextureView textureView = wgpuSwapChainGetCurrentTextureView(wgpu.swapChain);
    WGPURenderPassColorAttachment attachment = {
        .view = textureView,
        .resolveTarget = NULL,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = WGPUColor { 0.1, 0.1, 0.1, 1.0 },
    };

    WGPURenderPassDescriptor rpDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment
    };

    WGPUCommandEncoderDescriptor encoderDesc = { .label = "Command Encoder" };
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(wgpu.device, &encoderDesc);

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);
    wgpuRenderPassEncoderSetPipeline(pass, wgpu.pipeline);

    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdBufferDesc = { .label = "Command Buffer" };
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    WGPUQueue queue = wgpuDeviceGetQueue(wgpu.device);
    wgpuQueueSubmit(queue, 1, &commands);

    wgpuCommandEncoderRelease(encoder);
    wgpuCommandBufferRelease(commands);
    wgpuTextureViewRelease(textureView);
}

#if defined(__EMSCRIPTEN__)

void emscripten_switch_to_render_loop(void *) {
    emscripten_set_main_loop(render, 0, 0);
}

#endif

void init_loop(void *continue_loop) {
    switch (wgpu.state) {
        case WGPUState_None:
        case WGPUState_RequestingAdapter:
        case WGPUState_RequestingDevice:
            break;

        case WGPUState_InstanceAcquired: {
            webgpu_request_adapter(&wgpu);
            break;
        }

        case WGPUState_AdapterAcquired: {
            webgpu_request_device(&wgpu);
            break;
        }

        case WGPUState_DeviceAcquired: {
            webgpu_create_pipeline(&wgpu, WIDTH, HEIGHT);
            break;
        }

        case WGPUState_Ready: {
#if defined(__EMSCRIPTEN__)
            emscripten_async_call(emscripten_switch_to_render_loop, NULL, 0);
            emscripten_cancel_main_loop();
#endif
            *((bool *)continue_loop) = false;
            return;
        }

        case WGPUState_Error: {
            fprintf(stderr, "Unexpected error\n");
            *((bool*)continue_loop) = false;
            return;
        }
    }
}

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, "WebGPU Base", NULL, NULL);

    wgpu = webgpu_init(window);

    bool continue_loop = true;
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(init_loop, &continue_loop, 0, false);
#else
    while (continue_loop && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        init_loop((void *)&continue_loop);
    }

    if (wgpu.state != WGPUState_Error) {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            render();
            wgpuSwapChainPresent(wgpu.swapChain);

            // TODO: Not sure if this is necessary or not, or if something in the renderer
            // will be triggering this for us. However, when looking through the dawn code
            // it appears that we're potentially not incurring any costs by calling tick
            // more than once.
            wgpuDeviceTick(wgpu.device);
        }
    }
#endif

    return 0;
}

