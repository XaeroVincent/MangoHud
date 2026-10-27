--- src/memory.cpp.orig
+++ src/memory.cpp
@@ -2,10 +2,19 @@
 #include "memory.h"
 #include <iomanip>
 #include <cstring>
+#include <algorithm>
+#include <chrono>
+#include <vector>
 #include <stdio.h>
 #include <iostream>
 #include <thread>
 #include <unistd.h>
+#ifdef __FreeBSD__
+#include <sys/types.h>
+#include <sys/param.h>
+#include <sys/sysctl.h>
+#include <sys/user.h>
+#endif
 
 struct memory_information mem_info;
 float memused, memmax, swapused, swapmax;
@@ -27,6 +36,154 @@
   return fp;
 }
 
+#ifdef __FreeBSD__
+// Reads a fixed-size sysctl; fails if the kernel's type has a different size.
+template <typename T>
+static bool read_sysctl_val(const char *name, T& out) {
+  T v {};
+  size_t len = sizeof(v);
+  if (sysctlbyname(name, &v, &len, NULL, 0) != 0 || len != sizeof(v))
+    return false;
+  out = v;
+  return true;
+}
+
+// Mirrors struct xswdev from <vm/swap_pager.h> (vm.swap_info, XSWDEV_VERSION 2).
+// Checked against the returned size at runtime.
+struct mh_xswdev {
+  unsigned int xsw_version;
+  uint64_t xsw_dev;
+  int xsw_flags;
+  int xsw_nblks;
+  int xsw_used;
+};
+
+static void read_swap(unsigned long long pagesize, unsigned long long& total_kb, unsigned long long& used_kb) {
+  total_kb = used_kb = 0;
+  int mib[16];
+  size_t miblen = sizeof(mib) / sizeof(mib[0]) - 1;
+  if (sysctlnametomib("vm.swap_info", mib, &miblen) != 0)
+    return;
+
+  for (int n = 0; ; n++) {
+    mh_xswdev xsw {};
+    size_t len = sizeof(xsw);
+    mib[miblen] = n;
+    if (sysctl(mib, miblen + 1, &xsw, &len, NULL, 0) != 0)
+      break; // ENOENT after the last device
+    if (len != sizeof(xsw) || xsw.xsw_version != 2)
+      break; // unknown layout, don't guess
+    total_kb += (unsigned long long)xsw.xsw_nblks * pagesize / 1024;
+    used_kb += (unsigned long long)xsw.xsw_used * pagesize / 1024;
+  }
+}
+
+void update_meminfo(void) {
+  /* All values in kB, like the Linux implementation. */
+  int pagesize_i = 0;
+  unsigned int page_count = 0, free_count = 0, inactive_count = 0;
+  uint64_t arc_size = 0;
+
+  if (!read_sysctl_val("hw.pagesize", pagesize_i) || pagesize_i <= 0)
+    return;
+  const unsigned long long pagesize = pagesize_i;
+
+  if (!read_sysctl_val("vm.stats.vm.v_page_count", page_count))
+    return;
+  read_sysctl_val("vm.stats.vm.v_free_count", free_count);
+  read_sysctl_val("vm.stats.vm.v_inactive_count", inactive_count);
+  // ZFS ARC is counted as wired memory but is given back under pressure.
+  // Missing when ZFS isn't loaded.
+  read_sysctl_val("kstat.zfs.misc.arcstats.size", arc_size);
+
+  unsigned long long memmax_kb = page_count * pagesize / 1024;
+  unsigned long long free_kb = free_count * pagesize / 1024;
+  unsigned long long reclaimable_kb = inactive_count * pagesize / 1024 + arc_size / 1024;
+  unsigned long long easyfree_kb = std::min(free_kb + reclaimable_kb, memmax_kb);
+
+  unsigned long long swapmax_kb = 0, swapused_kb = 0;
+  read_swap(pagesize, swapmax_kb, swapused_kb);
+
+  mem_info.memmax = memmax_kb;
+  mem_info.memfree = free_kb;
+  mem_info.memwithbuffers = memmax_kb - free_kb;
+  mem_info.mem = memmax_kb - easyfree_kb;
+  mem_info.bufmem = reclaimable_kb;
+  mem_info.buffers = 0;
+  mem_info.cached = reclaimable_kb;
+  mem_info.memdirty = 0;
+  mem_info.memeasyfree = easyfree_kb;
+  mem_info.swapmax = swapmax_kb;
+  mem_info.swapfree = swapmax_kb - std::min(swapused_kb, swapmax_kb);
+  mem_info.swap = swapused_kb;
+
+  memused = (float(mem_info.memmax) - float(mem_info.memeasyfree)) / (1024 * 1024);
+  memmax = float(mem_info.memmax) / (1024 * 1024);
+  swapused = float(swapused_kb) / (1024 * 1024);
+  swapmax = float(swapmax_kb) / (1024 * 1024);
+}
+
+// Shared memory like Linux's statm "shared": resident pages that don't belong
+// to a private object, summed over all mappings. The VM map dump is large for
+// Wine processes (thousands of entries), so only refresh it every few seconds.
+static int64_t get_shared_bytes(long pagesize)
+{
+    using clk = std::chrono::steady_clock;
+    static int64_t cached = 0;
+    static clk::time_point last;
+    static std::vector<char> buf;
+
+    const clk::time_point now = clk::now();
+    if (last.time_since_epoch().count() != 0 && now - last < std::chrono::seconds(5))
+        return cached;
+    last = now;
+
+    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, (int)getpid() };
+    size_t len = 0;
+    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0)
+        return cached;
+
+    // the map can grow between the two calls
+    len += len / 4 + 16 * sizeof(struct kinfo_vmentry);
+    buf.resize(len);
+    if (sysctl(mib, 4, buf.data(), &len, NULL, 0) != 0)
+        return cached;
+
+    int64_t pages = 0;
+    char *p = buf.data();
+    char *end = p + len;
+    while (p + sizeof(int) <= end) {
+        const struct kinfo_vmentry *kve = reinterpret_cast<const struct kinfo_vmentry *>(p);
+        if (kve->kve_structsize <= 0 || p + kve->kve_structsize > end)
+            break;
+        if (kve->kve_resident > kve->kve_private_resident)
+            pages += kve->kve_resident - kve->kve_private_resident;
+        p += kve->kve_structsize; // entries are packed, size varies with the path
+    }
+
+    cached = pages * pagesize;
+    return cached;
+}
+
+void update_procmem()
+{
+    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)getpid() };
+    struct kinfo_proc kp;
+    size_t len = sizeof(kp);
+    if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 || len < sizeof(kp))
+        return;
+
+    static long pageSize = sysconf(_SC_PAGESIZE);
+    if (pageSize < 0) pageSize = 4096;
+
+    proc_mem.virt = (int64_t)kp.ki_size;
+    proc_mem.resident = (int64_t)kp.ki_rssize * pageSize;
+    proc_mem.shared = get_shared_bytes(pageSize);
+    proc_mem.text = kp.ki_tsize;  // pages, like /proc/self/statm
+    proc_mem.data = kp.ki_dsize;
+    proc_mem.dirty = 0;
+}
+#else
 void update_meminfo(void) {
   FILE *meminfo_fp;
   static int reported = 0;
@@ -129,3 +286,4 @@
     }
     fclose(statm);
 }
+#endif // __FreeBSD__
