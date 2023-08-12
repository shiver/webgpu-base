#include <webgpu/webgpu_glfw.h>
#include <QuartzCore/CAMetalLayer.h>
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

// Adapted from utils_metal.mm in the dawn library
void set_metal_surface_descriptor(GLFWwindow *window, WGPUSurfaceDescriptorFromMetalLayer *desc) {
    @autoreleasepool {
        NSWindow *ns_window = glfwGetCocoaWindow(window);
        NSView *view = [ns_window contentView];

        [view setWantsLayer:YES];
        [view setLayer:[CAMetalLayer layer]];
        [[view layer] setContentsScale:[ns_window backingScaleFactor]];

        desc->layer = [view layer];
    }
}
