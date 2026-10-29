--- src/cpu.cpp.orig
+++ src/cpu.cpp
@@ -27,6 +27,19 @@
 
 #include "file_utils.h"
 
+#ifdef __FreeBSD__
+#include <sys/param.h>
+#include <sys/resource.h>
+#include <sys/sysctl.h>
+#include <sys/ioctl.h>
+#include <sys/cpuctl.h>
+#include <fcntl.h>
+#include <unistd.h>
+#include <cerrno>
+#include <cmath>
+#include <cstring>
+#endif
+
 static void calculateCPUData(CPUData& cpuData,
     unsigned long long int usertime,
     unsigned long long int nicetime,
@@ -103,6 +116,33 @@
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
@@ -151,6 +191,7 @@
     m_inited = true;
     return UpdateCPUData();
 }
+#endif
 
 bool CPUStats::Reinit()
 {
@@ -158,6 +199,44 @@
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
@@ -222,9 +301,11 @@
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
@@ -237,6 +318,18 @@
             fclose(fp);
         }
     }
+#elif defined(__FreeBSD__)
+    // Needs a cpufreq driver (hwpstate_amd, hwpstate_intel, est, ...).
+    // This is the selected P-state, not a measured clock. Each core reports
+    // its own dev.cpu.N.freq; cores without one show 0 (some drivers only
+    // expose dev.cpu.0.freq). The overall value below is the maximum.
+    char sysctl_name[40];
+    for (size_t i = 0; i < m_cpuData.size(); i++) {
+        snprintf(sysctl_name, sizeof(sysctl_name), "dev.cpu.%zu.freq", i);
+        int mhz = read_sysctl<int>(sysctl_name);
+        m_cpuData[i].mhz = mhz > 0 ? mhz : 0;
+    }
+#endif
 
     m_cpuDataTotal.cpu_mhz = 0;
     for (auto data : m_cpuData)
@@ -259,6 +352,7 @@
 }
 
 bool CPUStats::UpdateCpuTemp() {
+#if defined(__linux__)
 	if (cpu_type == "APU"){
         m_cpuDataTotal.temp = gpu_info.apu_cpu_temp;
         return true;
@@ -269,6 +363,24 @@
 
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
@@ -355,6 +467,44 @@
     return true;
 }
 
+#ifdef __FreeBSD__
+static bool cpuctl_rdmsr(int fd, uint32_t msr, uint64_t& out)
+{
+    cpuctl_msr_args_t args;
+    args.msr = (int)msr; // the kernel takes it back as an unsigned MSR number
+    args.data = 0;
+    if (ioctl(fd, CPUCTL_RDMSR, &args) != 0)
+        return false; // MSR not supported (or cpuctl not loaded)
+    out = args.data;
+    return true;
+}
+
+static bool get_cpu_power_cpuctl(CPUPowerData* cpuPowerData, float& power) {
+    CPUPowerData_cpuctl* pd = (CPUPowerData_cpuctl*)cpuPowerData;
+
+    uint64_t raw = 0;
+    if (pd->fd < 0 || !cpuctl_rdmsr(pd->fd, pd->energy_msr, raw))
+        return false;
+
+    const uint32_t counter = (uint32_t)raw;
+    const Clock::time_point now = Clock::now();
+
+    if (pd->haveLast) {
+        const double dt = std::chrono::duration<double>(now - pd->lastCounterValueTime).count();
+        if (dt < 0.1)
+            return false; // too soon for a meaningful reading, keep the old value
+
+        const uint32_t diff = counter - pd->lastCounterValue; // wraps correctly
+        power = (float)(diff * pd->joules_per_count / dt);
+    }
+
+    pd->lastCounterValue = counter;
+    pd->lastCounterValueTime = now;
+    pd->haveLast = true;
+    return true;
+}
+#endif
+
 static bool get_cpu_power_amdgpu(float& power) {
     power = gpu_info.apu_cpu_power;
     return true;
@@ -379,6 +529,11 @@
         case CPU_POWER_AMDGPU:
             if (!get_cpu_power_amdgpu(power)) return false;
             break;
+#ifdef __FreeBSD__
+        case CPU_POWER_CPUCTL:
+            if (!get_cpu_power_cpuctl(m_cpuPowerData.get(), power)) return false;
+            break;
+#endif
         default:
             return false;
     }
@@ -426,6 +581,7 @@
     return false;
 }
 
+#ifdef __linux__
 bool CPUStats::GetCpuFile() {
     if (m_cpuTempFile)
         return true;
@@ -466,6 +622,12 @@
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
@@ -519,6 +681,63 @@
     return powerData.release();
 }
 
+#ifdef __FreeBSD__
+// Package power from the RAPL energy counter, read with cpuctl(4):
+//   Intel:  MSR_RAPL_POWER_UNIT 0x606,      MSR_PKG_ENERGY_STATUS 0x611
+//   AMD:    MSR_AMD_RAPL_POWER_UNIT 0xc0010299, MSR_AMD_PKG_ENERGY_STATUS 0xc001029b
+// Energy unit = 1 / 2^((unit_msr >> 8) & 0x1f) joules, 32-bit counter.
+// Needs `kldload cpuctl` and read access to /dev/cpuctl0 (root:kmem 0640).
+static CPUPowerData_cpuctl* init_cpu_power_data_cpuctl() {
+    int fd = open("/dev/cpuctl0", O_RDONLY | O_CLOEXEC);
+    if (fd < 0) {
+        SPDLOG_ERROR("cpu_power: can't open /dev/cpuctl0 ({}); load cpuctl(4) and make the device readable", strerror(errno));
+        return nullptr;
+    }
+
+    struct Candidate { uint32_t unit_msr; uint32_t energy_msr; };
+    const Candidate amd   = { 0xc0010299, 0xc001029b };
+    const Candidate intel = { 0x606, 0x611 };
+
+    const std::string vendor = read_sysctl<std::string>("machdep.cpu_vendor");
+    const Candidate order[2] = {
+        vendor.find("Intel") != std::string::npos ? intel : amd,
+        vendor.find("Intel") != std::string::npos ? amd : intel
+    };
+
+    for (const auto& c : order) {
+        uint64_t unit = 0, energy = 0;
+        if (!cpuctl_rdmsr(fd, c.unit_msr, unit) || !cpuctl_rdmsr(fd, c.energy_msr, energy))
+            continue;
+
+        const unsigned esu = (unit >> 8) & 0x1f;
+        if (esu == 0)
+            continue; // implausible, not a RAPL-style unit register
+
+        auto powerData = std::make_unique<CPUPowerData_cpuctl>();
+        powerData->fd = fd;
+        powerData->energy_msr = c.energy_msr;
+        powerData->joules_per_count = std::ldexp(1.0, -(int)esu);
+        SPDLOG_DEBUG("cpu_power: using MSR {:#x}, {} joules per count", c.energy_msr, powerData->joules_per_count);
+        return powerData.release();
+    }
+
+    SPDLOG_ERROR("cpu_power: no readable package energy MSR on this CPU");
+    close(fd);
+    return nullptr;
+}
+
+bool CPUStats::InitCpuPowerData() {
+    if (m_cpuPowerData != nullptr)
+        return true;
+
+    CPUPowerData_cpuctl* powerData = init_cpu_power_data_cpuctl();
+    if (!powerData)
+        return false;
+
+    m_cpuPowerData.reset(powerData);
+    return true;
+}
+#else
 bool CPUStats::InitCpuPowerData() {
     if(m_cpuPowerData != nullptr)
         return true;
@@ -572,5 +791,6 @@
     m_cpuPowerData.reset(cpuPowerData);
     return true;
 }
+#endif
 
 CPUStats cpuStats;
