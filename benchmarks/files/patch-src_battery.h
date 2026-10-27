--- src/battery.h.orig
+++ src/battery.h
@@ -12,6 +12,7 @@
         float current_watt = 0;
         float current_percent = 0;
         float remaining_time = 0;
+        bool discharging = false;
         std::string current_status;
         std::string state [2];
         int batt_count=0;
