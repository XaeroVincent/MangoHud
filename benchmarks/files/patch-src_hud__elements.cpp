--- src/hud_elements.cpp.orig
+++ src/hud_elements.cpp
@@ -374,7 +374,21 @@
 }
 
 void HudElements::ram(){
-#ifdef __linux__
+#if defined(__linux__) || defined(__FreeBSD__)
+    // "swap" without "ram": show it on its own row
+    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_swap] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram]){
+        ImGui::TableNextColumn();
+        ImGui::TextColored(HUDElements.colors.swap, "SWAP");
+        ImGui::TableNextColumn();
+        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", swapused);
+        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
+            ImGui::SameLine(0,1.0f);
+            ImGui::PushFont(HUDElements.sw_stats->font1);
+            ImGui::Text("GiB");
+            ImGui::PopFont();
+        }
+    }
+
     if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram]){
         ImGui::TableNextColumn();
         ImGui::TextColored(HUDElements.colors.ram, "RAM");
@@ -401,7 +415,7 @@
 
 void HudElements::procmem()
 {
-#ifdef __linux__
+#if defined(__linux__) || defined(__FreeBSD__)
     const char* unit = nullptr;
     if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_procmem])
         return;
@@ -728,7 +742,7 @@
 }
 
 void HudElements::battery(){
-#ifdef __linux__
+#if defined(__linux__) || defined(__FreeBSD__)
     if (Battery_Stats.batt_count > 0) {
         if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery]) {
             ImGui::TableNextColumn();
@@ -758,9 +772,8 @@
                 ImGui::SameLine(0,1.0f);
                 ImGui::Text("%%");
             }
-            if (Battery_Stats.current_watt != 0) {
-                printf("%i\n", HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery_watt]);
-                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery_watt]){
+            if (Battery_Stats.discharging) {
+                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery_watt] && Battery_Stats.current_watt != 0){
                     ImguiNextColumnOrNewRow();
                     right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", Battery_Stats.current_watt);
                     ImGui::SameLine(0,1.0f);
@@ -1072,7 +1085,7 @@
         HUDElements.min = 0;
         ImGui::TextColored(HUDElements.colors.engine, "%s", "VRAM");
     }
-#ifdef __linux__
+#if defined(__linux__) || defined(__FreeBSD__)
     if (value == "ram"){
         if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram])
             HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram] = true;
@@ -1140,7 +1153,14 @@
     if (param == "show_fps_limit")  { ordered_functions.push_back({show_fps_limit, value});         }
 #endif
     if (param == "vram")            { ordered_functions.push_back({vram, value});                   }
-    if (param == "ram")             { ordered_functions.push_back({ram, value});                    }
+    if (param == "ram" || param == "swap") {
+        // ram() draws both; only register it once whichever comes first
+        bool have_ram = false;
+        for (const auto& f : ordered_functions)
+            if (f.first == ram) have_ram = true;
+        if (!have_ram)
+            ordered_functions.push_back({ram, value});
+    }
     if (param == "fps")             { ordered_functions.push_back({fps, value});                    }
     if (param == "gpu_name")        { ordered_functions.push_back({gpu_name, value});               }
     if (param == "frame_timing")    { ordered_functions.push_back({frame_timing, value});           }
