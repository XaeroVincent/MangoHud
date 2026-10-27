--- src/vulkan.cpp.orig
+++ src/vulkan.cpp
@@ -1909,6 +1909,11 @@
       init_system_info();
       instance_data->notifier.params = &instance_data->params;
       start_notifier(instance_data->notifier);
+#elif defined(__FreeBSD__)
+      // Only the Wine version here: init_system_info() also spawns
+      // glxinfo/pciconf. The config-file notifier stays off (starting it
+      // here without stopping it in DestroyInstance aborts the process).
+      init_wine_info();
 #endif
 
       init_cpu_stats(instance_data->params);
