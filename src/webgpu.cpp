#include <assert.h>
#include <stdio.h>

#include <webgpu/webgpu.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
//#include <webgpu/webgpu_glfw.h>
#endif

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const *message, void *userData);
static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, char const *message, void *userData);
static void onDeviceError(WGPUErrorType type, char const *message, void *userData);
static void onDeviceLog(WGPULoggingType type, char const *message, void *userData);
static void onQueueDone(WGPUQueueWorkDoneStatus status, void *userData);

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

struct WebGPU {
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUSwapChain swapChain;
    WGPURenderPipeline pipeline;
    bool requestComplete;
};

struct UserData {
    WGPUAdapter adapter = NULL;
    bool requestEnded = false;
};

WebGPU webgpu_init(GLFWwindow *window, uint32_t width, uint32_t height) {
    WebGPU wgpu = {.requestComplete = false};

    WGPUInstanceDescriptor desc = {.nextInChain = nullptr};
    wgpu.instance = wgpuCreateInstance(&desc);
    assert(wgpu.instance);

    printf("WebGPU instance = %p\n", wgpu.instance);

    WGPUSurfaceDescriptorFromWindowsHWND sdDesc = {};
    sdDesc.chain = { .next = NULL, .sType = WGPUSType_SurfaceDescriptorFromWindowsHWND };
    sdDesc.hwnd = glfwGetWin32Window(window);
    sdDesc.hinstance = GetModuleHandle(NULL);

    WGPUSurfaceDescriptor surfaceDesc = { .nextInChain = (WGPUChainedStruct *)&sdDesc };
    wgpu.surface = wgpuInstanceCreateSurface(wgpu.instance, &surfaceDesc);
    assert(wgpu.surface != NULL);

    WGPURequestAdapterOptions options = { .compatibleSurface = wgpu.surface };
    wgpuInstanceRequestAdapter(wgpu.instance, &options, onAdapterRequestEnded, (void *)&wgpu);

    // Apparently we can assume that the call to `onAdapterRequestEnded` in synchronous, but
    // just to be sure we check the request has indeed been fulfilled
    assert(wgpu.requestComplete);
    assert(wgpu.adapter);

    WGPUDeviceDescriptor dDesc = {
        .nextInChain = NULL,
        .label = "Device",
        .requiredFeaturesCount = 0,
        .requiredLimits = NULL,
        .defaultQueue = { .nextInChain = NULL, .label = "Default queue" }
    };

    wgpu.requestComplete = false;
    wgpuAdapterRequestDevice(wgpu.adapter, &dDesc, onDeviceRequestEnded, (void *)&wgpu);
    assert(wgpu.requestComplete);
    assert(wgpu.device);

    wgpu.requestComplete = false;

    wgpuDeviceSetUncapturedErrorCallback(wgpu.device, onDeviceError, NULL);
    wgpuDeviceSetLoggingCallback(wgpu.device, onDeviceLog, NULL);
    WGPUQueue queue = wgpuDeviceGetQueue(wgpu.device);
    wgpuQueueOnSubmittedWorkDone(queue, 0, onQueueDone, NULL);

    WGPUAdapterProperties properties = {};
    wgpuAdapterGetProperties(wgpu.adapter, &properties);
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
    assert(wgpuDeviceGetLimits(wgpu.device, &limits));
    printf("\nDevice:\n");
    printf("- maxTextureDimension1D: %d\n", limits.limits.maxTextureDimension1D);
    printf("- maxTextureDimension2D: %d\n", limits.limits.maxTextureDimension2D);
    printf("- maxTextureDimension3D: %d\n", limits.limits.maxTextureDimension3D);

    WGPUSwapChainDescriptor scDesc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = WGPUTextureFormat_BGRA8Unorm,
        .width = width,
        .height = height,
        .presentMode = WGPUPresentMode_Fifo
    };
    WGPUSwapChain swapChain = wgpuDeviceCreateSwapChain(wgpu.device, wgpu.surface, &scDesc);
    assert(swapChain != NULL);
    wgpu.swapChain = swapChain;

    WGPUShaderModuleWGSLDescriptor wgslDesc = { .code = shaderCode };
    WGPUChainedStruct *chained = (WGPUChainedStruct *)&wgslDesc;
    chained->sType = WGPUSType_ShaderModuleWGSLDescriptor;
    WGPUShaderModuleDescriptor smDesc = { .nextInChain = chained };
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(wgpu.device, &smDesc);

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

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(wgpu.device, &rpDesc);
    assert(renderPipeline != NULL);
    wgpu.pipeline = renderPipeline;

    // Error callbacks are only triggered when the device "ticks". Right now
    // we don't have anything that triggers a tick, so if there is something wrong
    // that would prevent the render loop from working, we won't see any errors.
    // To make sure we don't have errors during WebGPU setup we force a tick to happen.
    wgpuDeviceTick(wgpu.device);
    return wgpu;
}

static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const *message, void *pUserData) {
    WebGPU *userData = (WebGPU *)(pUserData);
    if (status != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "Request for adapter failed: %s\n", message);
        exit(1);
    }

    userData->adapter = adapter;
    userData->requestComplete = true;
}

static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, const char *message, void *pUserData) {
    WebGPU *userData = (WebGPU *)(pUserData);
    if (status != WGPURequestDeviceStatus_Success) {
        printf("Request for device failed: %s\n", message);
        exit(1);
    }

    userData->device = device;
    userData->requestComplete = true;
}

static void onDeviceError(WGPUErrorType type, char const *message, void * /*userData*/) {
    fprintf(stderr, "Uncaptured error: %d\n", type);
    if (message) fprintf(stderr, "  %s\n", message);
}

static void onDeviceLog(WGPULoggingType type, char const *message, void * /*userData*/) {
    printf("Device log: [%d] %s\n", type, message);
};

static void onQueueDone(WGPUQueueWorkDoneStatus status, void * /*userData*/) {
    printf("Queue done: %d\n", status);
};
