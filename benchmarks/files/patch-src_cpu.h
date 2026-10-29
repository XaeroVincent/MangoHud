--- src/cpu.h.orig
+++ src/cpu.h
@@ -5,6 +5,9 @@
 #include <vector>
 #include <cstdint>
 #include <cstdio>
+#ifdef __FreeBSD__
+#include <unistd.h>
+#endif
 #include <memory>
 #include <string>
 #ifdef WIN32
@@ -51,7 +54,8 @@
    CPU_POWER_K10TEMP,
    CPU_POWER_ZENPOWER,
    CPU_POWER_RAPL,
-   CPU_POWER_AMDGPU
+   CPU_POWER_AMDGPU,
+   CPU_POWER_CPUCTL
 };
 
 struct CPUPowerData {
@@ -119,6 +123,27 @@
    };
 };
 
+#ifdef __FreeBSD__
+// Package energy counter (RAPL-style MSR) read through cpuctl(4)
+struct CPUPowerData_cpuctl : public CPUPowerData {
+   CPUPowerData_cpuctl() {
+      this->source = CPU_POWER_CPUCTL;
+   };
+
+   ~CPUPowerData_cpuctl() {
+      if (this->fd >= 0)
+         close(this->fd);
+   };
+
+   int fd {-1};
+   uint32_t energy_msr {0};
+   double joules_per_count {0};
+   uint32_t lastCounterValue {0}; // the counter is 32 bits wide
+   bool haveLast {false};
+   Clock::time_point lastCounterValueTime;
+};
+#endif
+
 class CPUStats
 {
 public:
