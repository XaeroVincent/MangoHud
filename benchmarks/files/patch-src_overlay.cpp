--- src/overlay.cpp.orig
+++ src/overlay.cpp
@@ -25,7 +25,7 @@
 #include "amdgpu.h"
 
 
-#ifdef __linux__
+#ifndef _WIN32
 #include <libgen.h>
 #include <unistd.h>
 #endif
@@ -106,11 +106,11 @@
    if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_stats] || logger->is_active()) {
       cpuStats.UpdateCPUData();
 
-#ifdef __linux__
       if (params.enabled[OVERLAY_PARAM_ENABLED_core_load] || params.enabled[OVERLAY_PARAM_ENABLED_cpu_mhz])
          cpuStats.UpdateCoreMhz();
       if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_temp] || logger->is_active() || params.enabled[OVERLAY_PARAM_ENABLED_graphs])
          cpuStats.UpdateCpuTemp();
+#ifdef __linux__
       if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_power])
          cpuStats.UpdateCpuPower();
 #endif
@@ -129,21 +129,23 @@
          getIntelGpuInfo();
    }
 
-#ifdef __linux__
+#if defined(__linux__) || defined(__FreeBSD__)
+   if (params.enabled[OVERLAY_PARAM_ENABLED_ram] || params.enabled[OVERLAY_PARAM_ENABLED_swap] || logger->is_active())
+      update_meminfo();
+   if (params.enabled[OVERLAY_PARAM_ENABLED_procmem])
+      update_procmem();
+   if (params.enabled[OVERLAY_PARAM_ENABLED_io_read] || params.enabled[OVERLAY_PARAM_ENABLED_io_write])
+      getIoStats(g_io_stats);
    if (params.enabled[OVERLAY_PARAM_ENABLED_battery])
       Battery_Stats.update();
+#endif
+#ifdef __linux__
    if (params.enabled[OVERLAY_PARAM_ENABLED_gamepad_battery]) {
       gamepad_update();
       if (gamepad_found) {
             gamepad_info();
       }
    }
-   if (params.enabled[OVERLAY_PARAM_ENABLED_ram] || params.enabled[OVERLAY_PARAM_ENABLED_swap] || logger->is_active())
-      update_meminfo();
-   if (params.enabled[OVERLAY_PARAM_ENABLED_procmem])
-      update_procmem();
-   if (params.enabled[OVERLAY_PARAM_ENABLED_io_read] || params.enabled[OVERLAY_PARAM_ENABLED_io_write])
-      getIoStats(g_io_stats);
 #endif
 
    currentLogData.gpu_load = gpu_info.load;
@@ -152,7 +154,7 @@
    currentLogData.gpu_mem_clock = gpu_info.MemClock;
    currentLogData.gpu_vram_used = gpu_info.memoryUsed;
    currentLogData.gpu_power = gpu_info.powerUsage;
-#ifdef __linux__
+#if defined(__linux__) || defined(__FreeBSD__)
    currentLogData.ram_used = memused;
 #endif
 
@@ -646,14 +648,19 @@
 
 void init_cpu_stats(overlay_params& params)
 {
-#ifdef __linux__
    auto& enabled = params.enabled;
    enabled[OVERLAY_PARAM_ENABLED_cpu_stats] = cpuStats.Init()
                            && enabled[OVERLAY_PARAM_ENABLED_cpu_stats];
+#ifdef __linux__
    enabled[OVERLAY_PARAM_ENABLED_cpu_temp] = cpuStats.GetCpuFile()
                            && enabled[OVERLAY_PARAM_ENABLED_cpu_temp];
    enabled[OVERLAY_PARAM_ENABLED_cpu_power] = cpuStats.InitCpuPowerData()
                            && enabled[OVERLAY_PARAM_ENABLED_cpu_power];
+#elif defined(__FreeBSD__)
+   enabled[OVERLAY_PARAM_ENABLED_cpu_temp] = cpuStats.GetCpuFile()
+                           && enabled[OVERLAY_PARAM_ENABLED_cpu_temp];
+   // No unprivileged CPU power interface on FreeBSD (RAPL/MSRs need cpuctl + root)
+   enabled[OVERLAY_PARAM_ENABLED_cpu_power] = false;
 #endif
 }
 
@@ -803,27 +810,97 @@
          params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
       }
    }
+#elif defined(__FreeBSD__) && defined(HAVE_LIBDRM_AMDGPU)
+   // No sysfs on FreeBSD: talk to the amdgpu DRM node through libdrm_amdgpu
+   if (vendorID == 0x1002) {
+      uint32_t found_id = 0;
+      if (amdgpu_freebsd_init(reported_deviceID, found_id)) {
+         deviceID = found_id;
+      } else {
+         // don't bother then
+         params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
+      }
+   }
 #endif
    if (!params.permit_upload)
       SPDLOG_INFO("Uploading is disabled (permit_upload = 0)");
 }
 
+void init_wine_info()
+{
+   // Cheap and only needs to run once per process. Kept separate from
+   // init_system_info() so the Vulkan layer can call it without spawning
+   // glxinfo/pciconf.
+   static bool done = false;
+   if (done)
+      return;
+   done = true;
+
+   wineProcess = get_exe_path();
+   auto n = wineProcess.find_last_of('/');
+   string preloader = wineProcess.substr(n + 1);
+   if (!is_wine_loader(preloader)) {
+      wineVersion = "";
+      return;
+   }
+
+   // Check if using Proton
+   if (wineProcess.find("/dist/bin/wine") != std::string::npos || wineProcess.find("/files/bin/wine") != std::string::npos) {
+      stringstream ss;
+      ss << wineProcess.substr(0, n) << "/../../version";
+      string protonVersion = ss.str();
+      ss.str(""); ss.clear();
+      ss << read_line(protonVersion);
+      std::getline(ss, wineVersion, ' '); // skip first number string
+      std::getline(ss, wineVersion, ' ');
+      trim(wineVersion);
+      string toReplace = "proton-";
+      size_t pos = wineVersion.find(toReplace);
+      if (pos != std::string::npos) {
+         // If found replace
+         wineVersion.replace(pos, toReplace.length(), "Proton ");
+      }
+      else {
+         // If not found insert for non official proton builds
+         wineVersion.insert(0, "Proton ");
+      }
+   }
+   else {
+      // "wine64-preloader" -> "wine64", "wine" stays "wine"
+      string loader = preloader;
+      const string suffix = "-preloader";
+      if (loader.size() > suffix.size() && loader.compare(loader.size() - suffix.size(), suffix.size(), suffix) == 0)
+         loader.erase(loader.size() - suffix.size());
+
+      stringstream findVersion;
+      findVersion << "\"" << wineProcess.substr(0, n) << "/" << loader << "\" --version";
+      const char *wine_env = getenv("WINELOADERNOEXEC");
+      if (wine_env)
+         unsetenv("WINELOADERNOEXEC");
+      wineVersion = exec(findVersion.str());
+      trim(wineVersion);
+      SPDLOG_DEBUG("WINE version: {}", wineVersion);
+      if (wine_env)
+         setenv("WINELOADERNOEXEC", wine_env, 1);
+   }
+}
+
 void init_system_info(){
-   #ifdef __linux__
       const char* ld_preload = getenv("LD_PRELOAD");
       if (ld_preload)
          unsetenv("LD_PRELOAD");
 
-      ram =  exec("sed -n 's/^MemTotal: *\\([0-9]*\\).*/\\1/p' /proc/meminfo");
-      trim(ram);
-      cpu =  exec("sed -n 's/^model name.*: \\(.*\\)/\\1/p' /proc/cpuinfo | sed 's/([^)]*)//g' | tail -n1");
-      trim(cpu);
-      kernel = exec("uname -r");
-      trim(kernel);
-      os = exec("sed -n 's/PRETTY_NAME=\\(.*\\)/\\1/p' /etc/*-release");
-      os.erase(remove(os.begin(), os.end(), '\"' ), os.end());
-      trim(os);
-      cpusched = read_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
+      ram = to_string(sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE)
+            / 1024 / 1024) + " MB";
+      cpu = read_sysctl<string>("hw.model");
+      kernel = to_string(read_sysctl<int>("kern.osreldate"));
+      os = read_sysctl<string>("kern.ostype") + " " +
+           read_sysctl<string>("kern.osrelease");
+      gpu = exec("pciconf -lv | grep -A2 ^vgapci0 | tail -1 | cut -d\\' -f2");
+      trim(gpu);
+      driver = exec("glxinfo -B | grep 'OpenGL version' | sed 's/^.*: //'");
+      trim(driver);
+      cpusched = read_sysctl<string>("kern.sched.name");
 
       const char* mangohud_recursion = getenv("MANGOHUD_RECURSION");
       if (!mangohud_recursion) {
@@ -835,53 +912,9 @@
          driver = "MangoHud glxinfo recursion detected";
       }
 
-// Get WINE version
+      init_wine_info();
 
-      wineProcess = get_exe_path();
-      auto n = wineProcess.find_last_of('/');
-      string preloader = wineProcess.substr(n + 1);
-      if (preloader == "wine-preloader" || preloader == "wine64-preloader") {
-         // Check if using Proton
-         if (wineProcess.find("/dist/bin/wine") != std::string::npos || wineProcess.find("/files/bin/wine") != std::string::npos) {
-            stringstream ss;
-            ss << dirname((char*)wineProcess.c_str()) << "/../../version";
-            string protonVersion = ss.str();
-            ss.str(""); ss.clear();
-            ss << read_line(protonVersion);
-            std::getline(ss, wineVersion, ' '); // skip first number string
-            std::getline(ss, wineVersion, ' ');
-            trim(wineVersion);
-            string toReplace = "proton-";
-            size_t pos = wineVersion.find(toReplace);
-            if (pos != std::string::npos) {
-               // If found replace
-               wineVersion.replace(pos, toReplace.length(), "Proton ");
-            }
-            else {
-               // If not found insert for non official proton builds
-               wineVersion.insert(0, "Proton ");
-            }
-         }
-         else {
-            char *dir = dirname((char*)wineProcess.c_str());
-            stringstream findVersion;
-            if (preloader == "wine-preloader")
-               findVersion << "\"" << dir << "/wine\" --version";
-            else
-               findVersion << "\"" << dir << "/wine64\" --version";
-            const char *wine_env = getenv("WINELOADERNOEXEC");
-            if (wine_env)
-               unsetenv("WINELOADERNOEXEC");
-            wineVersion = exec(findVersion.str());
-            trim(wineVersion);
-            SPDLOG_DEBUG("WINE version: {}", wineVersion);
-            if (wine_env)
-               setenv("WINELOADERNOEXEC", wine_env, 1);
-         }
-      }
-      else {
-           wineVersion = "";
-      }
+#ifdef __linux__
       // check for gamemode and vkbasalt
       fs::path path("/proc/self/map_files/");
       for (auto& p : fs::directory_iterator(path)) {
@@ -894,6 +927,7 @@
          if (HUDElements.gamemode_bol && HUDElements.vkbasalt_bol)
             break;
       }
+#endif
 
       if (ld_preload)
          setenv("LD_PRELOAD", ld_preload, 1);
@@ -904,7 +938,6 @@
       SPDLOG_DEBUG("Os:{}", os);
       SPDLOG_DEBUG("Driver:{}", driver);
       SPDLOG_DEBUG("CPU Scheduler:{}", cpusched);
-#endif
 }
 
 std::string get_device_name(uint32_t vendorID, uint32_t deviceID)
@@ -928,6 +961,7 @@
 }
 
 void update_fan(){
+#ifdef __linux__
    // This just handles steam deck fan for now
    static bool init;
    string hwmon_path;
@@ -948,6 +982,7 @@
       fan_speed = stoi(read_line(hwmon_path));
    else
       fan_speed = -1;
+#endif
 }
 
 void next_hud_position(struct overlay_params& params){
