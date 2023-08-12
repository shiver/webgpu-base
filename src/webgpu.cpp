#include <assert.h>
#include <stdio.h>

#include <webgpu/webgpu.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>

#endif

enum WebGPUState {
    WGPUState_None,
    WGPUState_InstanceAcquired,
    WGPUState_RequestingAdapter,
    WGPUState_AdapterAcquired,
    WGPUState_RequestingDevice,
    WGPUState_DeviceAcquired,
    WGPUState_Ready,
    WGPUState_Error,
};

struct Dimensions {
    uint32_t width;
    uint32_t height;
};

typedef void (*RenderCallback)();

struct WebGPU {
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUSwapChain swapChain;
    WGPURenderPipeline pipeline;

    RenderCallback render_callback;
    Dimensions dimensions;
    WebGPUState state;
};

// public functions
WebGPU webgpu_init(uint32_t width, uint32_t height);
void webgpu_poll_init_state(void *wgpu_ptr);
#if defined(__EMSCRIPTEN__)
void webgpu_create_surface(WebGPU *wgpu);
#elif defined(__WIN32)
void webgpu_create_surface(WebGPU *wgpu, GLFWwindow *window);
#endif

// private functions
static void request_adapter(WebGPU *wgpu);
static void request_device(WebGPU *wgpu);
static void create_pipeline(WebGPU *wgpu);
static void emscripten_switch_to_render_loop(void *arg);
static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const *message, void *userData);
static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, char const *message, void *userData);
static void onDeviceError(WGPUErrorType type, char const *message, void *userData);
static void onQueueDone(WGPUQueueWorkDoneStatus status, void *userData);
#if !defined(__EMSCRIPTEN__)
static void onDeviceLog(WGPULoggingType type, char const *message, void *userData);
#endif

const char shaderCode[] = R"(
    @vertex fn vertexMain(@builtin(vertex_index) i : u32) ->
      @builtin(position) vec4f {
        const pos = array(vec2f(0, 1), vec2f(-1, -1), vec2f(1, -1));
        return vec4f(pos[i], 0, 1);
    }
    @fragment fn fragmentMain() -> @location(0) vec4f {
        return vec4f(1, 0, 0, 1);
    }
)";

#if defined(__EMSCRIPTEN__)
void emscripten_switch_to_render_loop(void *wgpu_ptr) {
    WebGPU *wgpu = (WebGPU *)wgpu_ptr;
    emscripten_set_main_loop(wgpu->render_callback, 0, 0);
}

void webgpu_create_surface(WebGPU *wgpu, const char *selector) {
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {
        .chain = { .sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector },
        .selector = selector
    };
    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct *)&canvasDesc };
    wgpu->surface = wgpuInstanceCreateSurface(wgpu->instance, &surfaceDesc);
    assert(wgpu->surface != NULL);
}

#else

void webgpu_create_surface(WebGPU *wgpu, GLFWwindow *window) {
    WGPUSurfaceDescriptorFromWindowsHWND sdDesc = {};
    sdDesc.chain = { .sType = WGPUSType_SurfaceDescriptorFromWindowsHWND };
    sdDesc.hwnd = glfwGetWin32Window(window);
    sdDesc.hinstance = GetModuleHandle(NULL);

    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct *)&sdDesc };
    wgpu->surface = wgpuInstanceCreateSurface(wgpu->instance, &surfaceDesc);
    assert(wgpu->surface != NULL);
}

#endif

void webgpu_poll_init_state(void *wgpu_ptr) {
    WebGPU *wgpu = (WebGPU *)wgpu_ptr;

    switch (wgpu->state) {
        case WGPUState_None:
        case WGPUState_RequestingAdapter:
        case WGPUState_RequestingDevice:
            break;

        case WGPUState_InstanceAcquired: {
            request_adapter(wgpu);
            break;
        }

        case WGPUState_AdapterAcquired: {
            request_device(wgpu);
            break;
        }

        case WGPUState_DeviceAcquired: {
            create_pipeline(wgpu);
            break;
        }

        case WGPUState_Ready: {
#if defined(__EMSCRIPTEN__)
            emscripten_async_call(emscripten_switch_to_render_loop, wgpu, 0);
            emscripten_cancel_main_loop();
#endif
            break;
        }

        case WGPUState_Error: {
            fprintf(stderr, "Unexpected error\n");
            break;
        }
    }
}

WebGPU webgpu_init(uint32_t width, uint32_t height) {
    Dimensions dims = { .width = width, .height = height };
    WebGPU wgpu = { .dimensions = dims };

    WGPUInstanceDescriptor desc = {.nextInChain = NULL};
    wgpu.instance = wgpuCreateInstance(&desc);
    assert(wgpu.instance);


    wgpu.state = WGPUState_InstanceAcquired;
    printf("WebGPU instance = %p\n", (void *)wgpu.instance);

    return wgpu;
}

static void request_adapter(WebGPU *wgpu) {
    wgpu->state = WGPUState_RequestingAdapter;
    WGPURequestAdapterOptions options = {
        .compatibleSurface = wgpu->surface,
        .backendType = WGPUBackendType_Vulkan,
    };
    wgpuInstanceRequestAdapter(wgpu->instance, &options, onAdapterRequestEnded, (void *)wgpu);

}

static void request_device(WebGPU *wgpu) {
    wgpu->state = WGPUState_RequestingDevice;
    WGPUDeviceDescriptor dDesc = {
        .nextInChain = NULL,
        .label = "Device",
        .requiredFeaturesCount = 0,
        .requiredLimits = NULL,
        .defaultQueue = { .nextInChain = NULL, .label = "Default queue" }
    };

    wgpuAdapterRequestDevice(wgpu->adapter, &dDesc, onDeviceRequestEnded, (void *)wgpu);
}

static void create_pipeline(WebGPU *wgpu) {
    wgpuDeviceSetUncapturedErrorCallback(wgpu->device, onDeviceError, NULL);
#if !defined(__EMSCRIPTEN__)
    wgpuDeviceSetLoggingCallback(wgpu->device, onDeviceLog, NULL);
#endif
    WGPUQueue queue = wgpuDeviceGetQueue(wgpu->device);
    wgpuQueueOnSubmittedWorkDone(queue, 0, onQueueDone, NULL);

    WGPUAdapterProperties properties = {};
    wgpuAdapterGetProperties(wgpu->adapter, &properties);
    printf("\nAdapter:\n");
    printf("- vendorID: %d\n", properties.vendorID);
    printf("- deviceID: %d\n", properties.deviceID);
    printf("- vendorName: %s\n", properties.vendorName);
    printf("- name: %s\n", properties.name);
    printf("- arch: %s\n", properties.architecture);
    printf("- adapterType: %d\n", properties.adapterType);
    printf("- backendType: %d\n", properties.backendType);
    if (properties.driverDescription)
        printf("- driverDescription: %s\n", properties.driverDescription);

    WGPUSupportedLimits limits = {};
    wgpuDeviceGetLimits(wgpu->device, &limits);
    printf("\nDevice:\n");
    printf("- maxTextureDimension1D: %d\n", limits.limits.maxTextureDimension1D);
    printf("- maxTextureDimension2D: %d\n", limits.limits.maxTextureDimension2D);
    printf("- maxTextureDimension3D: %d\n", limits.limits.maxTextureDimension3D);

    WGPUSwapChainDescriptor scDesc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = WGPUTextureFormat_BGRA8Unorm,
        .width = wgpu->dimensions.width,
        .height = wgpu->dimensions.height,
        .presentMode = WGPUPresentMode_Fifo
    };
    WGPUSwapChain swapChain = wgpuDeviceCreateSwapChain(wgpu->device, wgpu->surface, &scDesc);
    assert(swapChain != NULL);
    wgpu->swapChain = swapChain;

    WGPUShaderModuleWGSLDescriptor wgslDesc = { .code = shaderCode };
    WGPUChainedStruct *chained = (WGPUChainedStruct *)&wgslDesc;
    chained->sType = WGPUSType_ShaderModuleWGSLDescriptor;
    WGPUShaderModuleDescriptor smDesc = { .nextInChain = chained };
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(wgpu->device, &smDesc);

    WGPUBlendState blendState = {
        .color = { .operation = WGPUBlendOperation_Add , .srcFactor = WGPUBlendFactor_SrcAlpha, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha },
        .alpha = { .operation = WGPUBlendOperation_Add , .srcFactor = WGPUBlendFactor_Zero, .dstFactor = WGPUBlendFactor_One },
    };
    WGPUColorTargetState colorTargetState = {
        .format = WGPUTextureFormat_BGRA8Unorm, // TODO: Get this from the swap chain
        .blend = &blendState,
        .writeMask = WGPUColorWriteMask_All,
    };

    WGPUFragmentState fragmentState = {
        .module = shaderModule,
        .entryPoint = "fragmentMain",
        .targetCount = 1,
        .targets = &colorTargetState
    };

    WGPURenderPipelineDescriptor rpDesc = {
        .vertex = { .module = shaderModule, .entryPoint = "vertexMain" },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList, .stripIndexFormat = WGPUIndexFormat_Undefined, .frontFace = WGPUFrontFace_CCW, .cullMode = WGPUCullMode_None },
        .multisample = { .count = 1, .mask = ~0u, .alphaToCoverageEnabled = false },
        .fragment = &fragmentState,
    };

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(wgpu->device, &rpDesc);
    assert(renderPipeline != NULL);
    wgpu->pipeline = renderPipeline;

#if defined(__EMSCRIPTEN__)
#else
    // Error callbacks are only triggered when the device "ticks". Right now
    // we don't have anything that triggers a tick, so if there is something wrong
    // that would prevent the render loop from working, we won't see any errors.
    // To make sure we don't have errors during WebGPU setup we force a tick to happen.
    wgpuDeviceTick(wgpu->device);
#endif

    wgpu->state = WGPUState_Ready;
}

static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const *message, void *pUserData) {
    WebGPU *wgpu = (WebGPU *)(pUserData);
    if (status != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "Request for adapter failed: %s\n", message);
        exit(1);
    }

    assert(adapter);
    wgpu->adapter = adapter;
    wgpu->state = WGPUState_AdapterAcquired;
}

static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, const char *message, void *pUserData) {
    WebGPU *wgpu = (WebGPU *)(pUserData);
    if (status != WGPURequestDeviceStatus_Success) {
        printf("Request for device failed: %s\n", message);
        exit(1);
    }

    assert(device);
    wgpu->device = device;
    wgpu->state = WGPUState_DeviceAcquired;
}

static void onDeviceError(WGPUErrorType type, char const *message, void * /*userData*/) {
    fprintf(stderr, "Uncaptured error: %d\n", type);
    if (message) fprintf(stderr, "  %s\n", message);
}

#if !defined(__EMSCRIPTEN__)
static void onDeviceLog(WGPULoggingType type, char const *message, void * /*userData*/) {
    printf("Device log: [%d] %s\n", type, message);
};
#endif

static void onQueueDone(WGPUQueueWorkDoneStatus status, void * /*userData*/) {
    printf("Queue done: %d\n", status);
};
