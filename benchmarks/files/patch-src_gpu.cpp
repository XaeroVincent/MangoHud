--- src/gpu.cpp.orig
+++ src/gpu.cpp
@@ -4,6 +4,7 @@
 #include <functional>
 #include <thread>
 #include <cstring>
+#include <cerrno>
 #include <spdlog/spdlog.h>
 #include "nvctrl.h"
 #include "timing.hpp"
@@ -13,6 +14,18 @@
 
 #include "amdgpu.h"
 
+#if defined(__FreeBSD__) && defined(HAVE_LIBDRM_AMDGPU)
+#include <libdrm/amdgpu.h>
+#include <libdrm/amdgpu_drm.h>
+#include <fcntl.h>
+#include <unistd.h>
+#include <dirent.h>
+#include <algorithm>
+#include <atomic>
+#include <string>
+#include <vector>
+#endif
+
 using namespace std::chrono_literals;
 
 struct gpuInfo gpu_info {};
@@ -71,7 +84,164 @@
 #endif
 }
 
+#if defined(__FreeBSD__) && defined(HAVE_LIBDRM_AMDGPU)
+// FreeBSD has no amdgpu sysfs files. Everything comes from libdrm_amdgpu:
+// sensors (clocks, temperature, power), VRAM/GTT usage, and GPU load from
+// polling GRBM_STATUS (same method as MangoHud's old libdrm path).
+namespace {
+
+constexpr uint32_t GRBM_STATUS = 0x8010;
+
+struct amdgpu_bsd {
+    amdgpu_device_handle dev = nullptr;
+    int fd = -1;
+    std::atomic<unsigned> gui_percent {0};
+    std::atomic<bool> quit {false};
+    std::atomic<bool> use_sensor_load {false};
+    std::thread collector;
+
+    ~amdgpu_bsd()
+    {
+        quit = true;
+        if (collector.joinable())
+            collector.join();
+        if (dev)
+            amdgpu_device_deinitialize(dev);
+        if (fd >= 0)
+            close(fd);
+    }
+
+    void poll()
+    {
+        // 120 samples/s, averaged over 500 ms
+        const unsigned ticks = 60;
+        const auto interval = std::chrono::nanoseconds(500000000) / ticks;
+        unsigned gui = 0, curr = 0;
+        uint32_t stat = 0;
+
+        while (!quit) {
+            if (!use_sensor_load) {
+                if (amdgpu_read_mm_registers(dev, GRBM_STATUS / 4, 1, 0xffffffff, 0, &stat) != 0) {
+                    // register access can be denied; fall back to the SMU sensor
+                    use_sensor_load = true;
+                } else if (stat & (1U << 31)) {
+                    gui++;
+                }
+            }
+
+            std::this_thread::sleep_for(interval);
+
+            if (++curr >= ticks) {
+                curr = 0;
+                if (!use_sensor_load)
+                    gui_percent = gui * 100 / ticks;
+                gui = 0;
+            }
+        }
+    }
+};
+
+std::unique_ptr<amdgpu_bsd> g_amdgpu;
+
+} // namespace
+
+bool amdgpu_freebsd_init(uint32_t reported_deviceID, uint32_t& deviceID_out)
+{
+    if (g_amdgpu)
+        return true;
+
+    DIR *dir = opendir("/dev/dri");
+    if (!dir)
+        return false;
+
+    std::vector<std::string> nodes;
+    while (struct dirent *e = readdir(dir)) {
+        if (!strncmp(e->d_name, "card", 4) || !strncmp(e->d_name, "renderD", 7))
+            nodes.push_back(std::string("/dev/dri/") + e->d_name);
+    }
+    closedir(dir);
+
+    // "cardN" sorts before "renderDN"
+    std::sort(nodes.begin(), nodes.end());
+
+    for (const auto& node : nodes) {
+        int fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
+        if (fd < 0) {
+            SPDLOG_DEBUG("amdgpu: can't open {}: {}", node, strerror(errno));
+            continue;
+        }
+
+        // amdgpu_device_initialize() fails on anything that isn't an amdgpu
+        // node, so no separate drmGetVersion() check (that would also need
+        // libdrm itself on the link line, not just libdrm_amdgpu).
+        uint32_t major, minor;
+        amdgpu_device_handle dev;
+        if (amdgpu_device_initialize(fd, &major, &minor, &dev) != 0) {
+            SPDLOG_DEBUG("amdgpu: {} is not a usable amdgpu node", node);
+            close(fd);
+            continue;
+        }
+
+        struct amdgpu_gpu_info info {};
+        if (amdgpu_query_gpu_info(dev, &info) != 0
+            || (reported_deviceID && info.asic_id != reported_deviceID)) {
+            SPDLOG_DEBUG("amdgpu: {} doesn't match the reported device id {:X}", node, reported_deviceID);
+            amdgpu_device_deinitialize(dev);
+            close(fd);
+            continue;
+        }
+
+        auto h = std::make_unique<amdgpu_bsd>();
+        h->dev = dev;
+        h->fd = fd;
+        h->collector = std::thread(&amdgpu_bsd::poll, h.get());
+        g_amdgpu = std::move(h);
+        deviceID_out = info.asic_id;
+        SPDLOG_DEBUG("amdgpu: using {} (device id {:X})", node, info.asic_id);
+        return true;
+    }
+
+    SPDLOG_ERROR("amdgpu: no usable DRM node found in /dev/dri");
+    return false;
+}
+
+void amdgpu_freebsd_update()
+{
+    if (!g_amdgpu)
+        return;
+
+    amdgpu_device_handle dev = g_amdgpu->dev;
+    uint32_t v = 0;
+
+    struct drm_amdgpu_memory_info mem {};
+    if (amdgpu_query_info(dev, AMDGPU_INFO_MEMORY, sizeof(mem), &mem) == 0) {
+        gpu_info.memoryTotal = float(mem.vram.total_heap_size) / (1024 * 1024 * 1024);
+        gpu_info.memoryUsed = float(mem.vram.heap_usage) / (1024 * 1024 * 1024);
+        gpu_info.gtt_used = float(mem.gtt.heap_usage) / (1024 * 1024 * 1024);
+    }
+
+    if (amdgpu_query_sensor_info(dev, AMDGPU_INFO_SENSOR_GFX_SCLK, sizeof(v), &v) == 0)
+        gpu_info.CoreClock = v;
+    if (amdgpu_query_sensor_info(dev, AMDGPU_INFO_SENSOR_GFX_MCLK, sizeof(v), &v) == 0)
+        gpu_info.MemClock = v;
+    if (amdgpu_query_sensor_info(dev, AMDGPU_INFO_SENSOR_GPU_TEMP, sizeof(v), &v) == 0)
+        gpu_info.temp = v / 1000;
+    if (amdgpu_query_sensor_info(dev, AMDGPU_INFO_SENSOR_GPU_AVG_POWER, sizeof(v), &v) == 0)
+        gpu_info.powerUsage = v;
+
+    if (g_amdgpu->use_sensor_load) {
+        if (amdgpu_query_sensor_info(dev, AMDGPU_INFO_SENSOR_GPU_LOAD, sizeof(v), &v) == 0)
+            gpu_info.load = v;
+    } else {
+        gpu_info.load = g_amdgpu->gui_percent;
+    }
+}
+#endif
+
 void getAmdGpuInfo(){
+#if defined(__FreeBSD__) && defined(HAVE_LIBDRM_AMDGPU)
+    amdgpu_freebsd_update();
+#endif
 #ifdef __linux__
     int64_t value = 0;
     if (metrics_path.empty()){
