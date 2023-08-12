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

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, "WebGPU Base", NULL, NULL);

    wgpu = webgpu_init(WIDTH, HEIGHT);
    wgpu.render_callback = &render;

#if defined(__EMSCRIPTEN__)
    (void)window;
    webgpu_create_surface(&wgpu, "#canvas");
    emscripten_set_main_loop_arg(webgpu_poll_init_state, &wgpu, 0, false);
#else
    webgpu_create_surface(&wgpu, window);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        webgpu_poll_init_state((void *)&wgpu);
        if (wgpu.state == WGPUState_Ready || wgpu.state == WGPUState_Error) break;
    }

    if (wgpu.state != WGPUState_Error) {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            render();
            wgpuSwapChainPresent(wgpu.swapChain);

            // TODO: Not sure if this is necessary or not, or if something in the renderer
            // will be triggering this for us. However, when looking through the dawn code
            // it appears that we're potentially not incurring any costs by calling tick
            // more than once anyway, so no harm it having here even once the render loop is
            // closer to completion.
            wgpuDeviceTick(wgpu.device);
        }
    }
#endif

    return 0;
}

