--- src/battery.cpp.orig
+++ src/battery.cpp
@@ -2,9 +2,87 @@
 #include <filesystem.h>
 #include "battery.h"
 
+#ifdef __FreeBSD__
+#include <sys/types.h>
+#include <sys/ioctl.h>
+#include <dev/acpica/acpiio.h>
+#include <fcntl.h>
+#include <unistd.h>
+#include "file_utils.h"
+#endif
+
 namespace fs = ghc::filesystem;
 using namespace std;
 
+#ifdef __FreeBSD__
+// ACPI battery state comes from the hw.acpi.battery sysctls (percentage, minutes
+// remaining, state bits); power draw needs the ACPI ioctl on /dev/acpi, same as
+// acpiconf(8), which works with a read-only open.
+void BatteryStats::numBattery() {
+    int units = read_sysctl<int>("hw.acpi.battery.units");
+    batt_count = units > 0 ? units : 0;
+    batt_check = true;
+}
+
+float BatteryStats::getPower() {
+    static int fd = -2;
+    if (fd == -2)
+        fd = open("/dev/acpi", O_RDWR);
+    if (fd < 0)
+        fd = open("/dev/acpi", O_RDONLY);
+    if (fd < 0)
+        return 0;
+
+    float watts = 0;
+    for (int unit = 0; unit < batt_count; unit++) {
+        union acpi_battery_ioctl_arg io;
+
+        io.unit = unit;
+        if (ioctl(fd, ACPIIO_BATT_GET_BIX, &io) == -1)
+            continue;
+        const uint32_t power_units = io.bix.units;
+
+        io.unit = unit;
+        if (ioctl(fd, ACPIIO_BATT_GET_BST, &io) == -1)
+            continue;
+        if (io.bst.state == ACPI_BATT_STAT_NOT_PRESENT
+            || io.bst.rate == ACPI_BATT_UNKNOWN)
+            continue;
+
+        if (power_units == ACPI_BIX_UNITS_MA) {
+            if (io.bst.volt != ACPI_BATT_UNKNOWN)
+                watts += (float)io.bst.rate * (float)io.bst.volt / 1000000.f; // mA * mV
+        } else {
+            watts += (float)io.bst.rate / 1000.f; // mW
+        }
+    }
+    return watts;
+}
+
+void BatteryStats::update() {
+    if (!batt_check) {
+        numBattery();
+        if (batt_count == 0)
+            SPDLOG_ERROR("No battery found");
+    }
+    if (batt_count <= 0)
+        return;
+
+    const int life = read_sysctl<int>("hw.acpi.battery.life");   // percent, -1 unknown
+    const int mins = read_sysctl<int>("hw.acpi.battery.time");   // minutes, -1 unknown
+    const int state = read_sysctl<int>("hw.acpi.battery.state"); // ACPI_BATT_STAT_* bits
+
+    current_percent = life >= 0 ? (float)life : 0.f;
+    discharging = state >= 0 && (state & ACPI_BATT_STAT_DISCHARG);
+    remaining_time = (discharging && mins > 0) ? mins / 60.f : 0.f; // hours, like the Linux code
+    current_watt = discharging ? getPower() : 0.f;
+
+    if (state >= 0 && (state & ACPI_BATT_STAT_CHARGING))
+        current_status = "Charging";
+    else
+        current_status = discharging ? "Discharging" : "Full";
+}
+#else
 void BatteryStats::numBattery() {
     int batteryCount = 0;
     if (!fs::exists("/sys/class/power_supply/")) {
@@ -34,6 +112,7 @@
         current_watt = getPower();
         current_percent = getPercent();
         remaining_time = getTimeRemaining();
+        discharging = current_watt != 0;
     }
 }
 
@@ -195,4 +274,6 @@
     return charge / current;
 }
 
+#endif // __FreeBSD__
+
 BatteryStats Battery_Stats;
