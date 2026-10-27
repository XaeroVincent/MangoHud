--- src/real_dlsym.cpp.orig
+++ src/real_dlsym.cpp
@@ -18,11 +18,15 @@
 static void get_real_functions()
 {
     eh_obj_t libdl;
-    int ret;
+    int ret = 0;
 
     const char* libs[] = {
 #if defined(__GLIBC__)
         "*libdl.so*",
+#elif defined(__FreeBSD__)
+        // rtld has the real dlopen/dlsym; libc only has stubs.
+        // The wildcard also matches ld-elf32.so.1 for 32-bit processes.
+        "*ld-elf*.so*",
 #endif
         "*libc.so*",
         "*libc.*.so*",
@@ -44,7 +48,7 @@
         __dlsym = nullptr;
     }
 
-    if (!__dlopen && !__dlsym)
+    if (!__dlopen || !__dlsym)
     {
         fprintf(stderr, "MANGOHUD: Can't get dlopen() and dlsym()\n");
         exit(ret ? ret : 1);
