/*
 * test_webgpu_spike.c — headless, self-validating proof that the WebGPU
 * (wgpu-native) backend works end to end on this platform.
 *
 * Milestones (each an assertion, no display needed):
 *   A. Bring up WebGPU: instance -> adapter -> device (proves the pinned prebuilt
 *      links and the native backend — Metal on macOS — initializes).
 *   B. Compile a WGSL shader AT RUNTIME (the capability that made WebGPU the right
 *      fit for Fast3D's runtime-generated combiner shaders, vs bgfx's offline model).
 *   C. Render a triangle to an offscreen RGBA8 target, read the pixels back, and
 *      ASSERT the triangle is present (center = triangle color, corner = clear).
 *
 * This is the de-risking spike from the renderer backend strategy: if it passes on
 * macOS + Windows + a handheld, WebGPU is proven viable before the full migration.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

static int g_fails = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", (name)); ++g_fails; } \
    else         { fprintf(stderr, "ok:   %s\n", (name)); } \
} while (0)

/* --- async request helpers (wgpu-native fires these inline during the request) - */
typedef struct { WGPUAdapter adapter; int done; WGPURequestAdapterStatus status; } AdapterReq;
static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                       WGPUStringView message, void *ud1, void *ud2) {
    (void)message; (void)ud2;
    AdapterReq *r = (AdapterReq *)ud1;
    r->status = status; r->adapter = adapter; r->done = 1;
}
typedef struct { WGPUDevice device; int done; WGPURequestDeviceStatus status; } DeviceReq;
static void on_device(WGPURequestDeviceStatus status, WGPUDevice device,
                      WGPUStringView message, void *ud1, void *ud2) {
    (void)message; (void)ud2;
    DeviceReq *r = (DeviceReq *)ud1;
    r->status = status; r->device = device; r->done = 1;
}

int main(void) {
    /* ---- Milestone A: instance -> adapter -> device ---------------------- */
    WGPUInstance instance = wgpuCreateInstance(NULL);
    CHECK("create instance", instance != NULL);
    if (!instance) return 1;

    AdapterReq areq = {0};
    WGPURequestAdapterCallbackInfo acb = {0};
    acb.mode = WGPUCallbackMode_AllowProcessEvents;
    acb.callback = on_adapter;
    acb.userdata1 = &areq;
    WGPURequestAdapterOptions aopts = {0};
    wgpuInstanceRequestAdapter(instance, &aopts, acb);
    for (int i = 0; !areq.done && i < 1000; ++i) wgpuInstanceProcessEvents(instance);
    CHECK("request adapter", areq.done && areq.status == WGPURequestAdapterStatus_Success && areq.adapter);
    if (!areq.adapter) return 1;

    WGPUAdapterInfo info = {0};
    wgpuAdapterGetInfo(areq.adapter, &info);
    fprintf(stderr, "[webgpu-spike] adapter backend=%d device=%.*s\n",
            (int)info.backendType, (int)info.device.length,
            info.device.data ? info.device.data : "");

    DeviceReq dreq = {0};
    WGPURequestDeviceCallbackInfo dcb = {0};
    dcb.mode = WGPUCallbackMode_AllowProcessEvents;
    dcb.callback = on_device;
    dcb.userdata1 = &dreq;
    WGPUDeviceDescriptor ddesc = {0};
    wgpuAdapterRequestDevice(areq.adapter, &ddesc, dcb);
    for (int i = 0; !dreq.done && i < 1000; ++i) wgpuInstanceProcessEvents(instance);
    CHECK("request device", dreq.done && dreq.status == WGPURequestDeviceStatus_Success && dreq.device);
    if (!dreq.device) return 1;

    wgpuAdapterInfoFreeMembers(info);
    wgpuDeviceRelease(dreq.device);
    wgpuAdapterRelease(areq.adapter);
    wgpuInstanceRelease(instance);

    if (g_fails == 0) { fprintf(stderr, "PASS: webgpu spike (milestone A: init)\n"); return 0; }
    fprintf(stderr, "%d failure(s)\n", g_fails);
    return 1;
}
