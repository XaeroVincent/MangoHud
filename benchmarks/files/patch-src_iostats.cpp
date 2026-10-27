--- src/iostats.cpp.orig
+++ src/iostats.cpp
@@ -2,8 +2,40 @@
 #include "string_utils.h"
 #include <fstream>
 
+#ifdef __FreeBSD__
+#include <sys/param.h>
+#include <sys/mount.h>
+#include <sys/resource.h>
+#endif
+
 struct iostats g_io_stats;
 
+#ifdef __FreeBSD__
+// FreeBSD has no per-process byte counters (no /proc/self/io). The closest
+// thing is getrusage(): ru_inblock/ru_oublock count filesystem I/O operations
+// (UFS: buffer-cache misses and flushes; OpenZFS: ARC misses and dirtied
+// records). Scale them by the I/O size of the filesystem the process works in.
+static unsigned long long io_block_size()
+{
+    static unsigned long long bs = 0;
+    if (!bs) {
+        struct statfs sfs;
+        if (statfs(".", &sfs) == 0 && sfs.f_iosize > 0)
+            bs = sfs.f_iosize;
+        else if (statfs("/", &sfs) == 0 && sfs.f_iosize > 0)
+            bs = sfs.f_iosize;
+        else
+            bs = 32768;
+
+        if (bs < 4096)
+            bs = 4096;
+        if (bs > 1024 * 1024)
+            bs = 1024 * 1024;
+    }
+    return bs;
+}
+#endif
+
 void getIoStats(iostats& io) {
     Clock::time_point now = Clock::now(); /* ns */
     std::chrono::duration<float> time_diff = now - io.last_update;
@@ -11,6 +43,14 @@
     io.prev.read_bytes  = io.curr.read_bytes;
     io.prev.write_bytes = io.curr.write_bytes;
 
+#ifdef __FreeBSD__
+    struct rusage ru;
+    if (getrusage(RUSAGE_SELF, &ru) == 0) {
+        const unsigned long long bs = io_block_size();
+        io.curr.read_bytes  = (unsigned long long)ru.ru_inblock * bs;
+        io.curr.write_bytes = (unsigned long long)ru.ru_oublock * bs;
+    }
+#else
     std::string line;
     std::ifstream f("/proc/self/io");
     while (std::getline(f, line)) {
@@ -21,6 +61,7 @@
             try_stoull(io.curr.write_bytes, line.substr(13));
         }
     }
+#endif
 
     if (io.last_update.time_since_epoch().count()) {
         io.diff.read  = (io.curr.read_bytes  - io.prev.read_bytes) / (1024.f * 1024.f);
