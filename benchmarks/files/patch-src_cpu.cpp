--- src/cpu.cpp.orig
+++ src/cpu.cpp
@@ -27,6 +27,12 @@
 
 #include "file_utils.h"
 
+#ifdef __FreeBSD__
+#include <sys/param.h>
+#include <sys/resource.h>
+#include <sys/sysctl.h>
+#endif
+
 static void calculateCPUData(CPUData& cpuData,
     unsigned long long int usertime,
     unsigned long long int nicetime,
@@ -103,6 +109,33 @@
         fclose(m_cpuTempFile);
 }
 
+#ifdef __FreeBSD__
+bool CPUStats::Init()
+{
+    if (m_inited)
+        return true;
+
+    // kern.cp_times has one slot per CPU id; absent CPUs are reported as zeros
+    size_t len = 0;
+    if (sysctlbyname("kern.cp_times", NULL, &len, NULL, 0) != 0
+        || len < CPUSTATES * sizeof(long)) {
+        SPDLOG_ERROR("Failed to query kern.cp_times");
+        return false;
+    }
+
+    m_cpuData.clear();
+    for (size_t i = 0; i < len / (CPUSTATES * sizeof(long)); i++) {
+        CPUData cpu = {};
+        cpu.totalTime = 1;
+        cpu.totalPeriod = 1;
+        cpu.cpu_id = (int)i;
+        m_cpuData.push_back(cpu);
+    }
+
+    m_inited = true;
+    return UpdateCPUData();
+}
+#else
 bool CPUStats::Init()
 {
     if (m_inited)
@@ -151,6 +184,7 @@
     m_inited = true;
     return UpdateCPUData();
 }
+#endif
 
 bool CPUStats::Reinit()
 {
@@ -158,6 +192,44 @@
     return Init();
 }
 
+#ifdef __FreeBSD__
+bool CPUStats::UpdateCPUData()
+{
+    if (!m_inited)
+        return false;
+
+    // Counters are in stathz ticks; only ratios matter here.
+    // Order: CP_USER, CP_NICE, CP_SYS, CP_INTR, CP_IDLE
+    long total[CPUSTATES];
+    size_t len = sizeof(total);
+    if (sysctlbyname("kern.cp_time", total, &len, NULL, 0) != 0)
+        return false;
+
+    calculateCPUData(m_cpuDataTotal,
+        total[CP_USER], total[CP_NICE], total[CP_SYS], total[CP_IDLE],
+        0 /* iowait */, total[CP_INTR], 0 /* softirq */, 0 /* steal */, 0 /* guest */, 0 /* guestnice */);
+
+    std::vector<long> times(m_cpuData.size() * CPUSTATES);
+    len = times.size() * sizeof(long);
+    if (sysctlbyname("kern.cp_times", times.data(), &len, NULL, 0) != 0)
+        return false;
+
+    size_t n = std::min(m_cpuData.size(), len / (CPUSTATES * sizeof(long)));
+    if (n == 0)
+        return false;
+
+    for (size_t i = 0; i < n; i++) {
+        const long *t = &times[i * CPUSTATES];
+        calculateCPUData(m_cpuData[i],
+            t[CP_USER], t[CP_NICE], t[CP_SYS], t[CP_IDLE],
+            0, t[CP_INTR], 0, 0, 0, 0);
+    }
+
+    m_cpuPeriod = (double)m_cpuData[0].totalPeriod / m_cpuData.size();
+    m_updatedCPUs = true;
+    return true;
+}
+#else
 //TODO take sampling interval into account?
 bool CPUStats::UpdateCPUData()
 {
@@ -222,9 +294,11 @@
     m_updatedCPUs = true;
     return ret;
 }
+#endif
 
 bool CPUStats::UpdateCoreMhz() {
     m_coreMhz.clear();
+#if defined(__linux__)
     FILE *fp;
     for (auto& cpu : m_cpuData)
     {
@@ -237,6 +311,21 @@
             fclose(fp);
         }
     }
+#elif defined(__FreeBSD__)
+    // Needs a cpufreq driver (hwpstate_amd, hwpstate_intel, est, ...).
+    // This is the selected P-state, not a measured clock. Some drivers
+    // only expose dev.cpu.0.freq, which controls all CPUs, so CPUs
+    // without their own sysctl fall back to cpu0's value.
+    char sysctl_name[40];
+    int cpu0_mhz = read_sysctl<int>("dev.cpu.0.freq");
+    for (size_t i = 0; i < m_cpuData.size(); i++) {
+        snprintf(sysctl_name, sizeof(sysctl_name), "dev.cpu.%zu.freq", i);
+        int mhz = read_sysctl<int>(sysctl_name);
+        if (mhz <= 0)
+            mhz = cpu0_mhz;
+        m_cpuData[i].mhz = mhz > 0 ? mhz : 0;
+    }
+#endif
 
     m_cpuDataTotal.cpu_mhz = 0;
     for (auto data : m_cpuData)
@@ -259,6 +348,7 @@
 }
 
 bool CPUStats::UpdateCpuTemp() {
+#if defined(__linux__)
 	if (cpu_type == "APU"){
         m_cpuDataTotal.temp = gpu_info.apu_cpu_temp;
         return true;
@@ -269,6 +359,24 @@
 
         return ret;
     }
+#elif defined(__FreeBSD__)
+    // coretemp(4)/amdtemp(4) report dev.cpu.N.temperature in deci-Kelvin
+    char sysctl_name[40];
+    long sum = 0;
+    long n = 0;
+    for (size_t i = 0; i < m_cpuData.size(); i++) {
+        snprintf(sysctl_name, sizeof(sysctl_name), "dev.cpu.%zu.temperature", i);
+        int dK = read_sysctl<int>(sysctl_name);
+        if (dK <= 0)
+            continue; // missing sensor (or CPU slot), skip it
+        sum += dK - 2731;
+        n++;
+    }
+    if (n == 0)
+        return false;
+    m_cpuDataTotal.temp = (int)(sum / (10 * n));
+    return true;
+#endif
 }
 
 static bool get_cpu_power_k10temp(CPUPowerData* cpuPowerData, float& power) {
@@ -426,6 +534,7 @@
     return false;
 }
 
+#ifdef __linux__
 bool CPUStats::GetCpuFile() {
     if (m_cpuTempFile)
         return true;
@@ -466,6 +575,12 @@
     }
     return true;
 }
+#elif defined(__FreeBSD__)
+bool CPUStats::GetCpuFile() {
+    // Usable if coretemp(4)/amdtemp(4) provides the sensor
+    return read_sysctl<int>("dev.cpu.0.temperature") > 0;
+}
+#endif
 
 static CPUPowerData_k10temp* init_cpu_power_data_k10temp(const std::string path) {
     auto powerData = std::make_unique<CPUPowerData_k10temp>();
