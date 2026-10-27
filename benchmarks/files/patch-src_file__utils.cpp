--- src/file_utils.cpp.orig
+++ src/file_utils.cpp
@@ -2,12 +2,18 @@
 #include "string_utils.h"
 #include <sys/types.h>
 #include <sys/stat.h>
+#include <sys/param.h>
+#include <sys/sysctl.h>
+#include <sys/user.h>
+#include <link.h>
 #include <unistd.h>
 #include <dirent.h>
 #include <limits.h>
 #include <fstream>
+#include <sstream>
 #include <cstring>
 #include <string>
+#include <utility>
 #include <spdlog/spdlog.h>
 #include <filesystem.h>
 
@@ -35,7 +41,6 @@
     return path;
 }
 
-#ifdef __linux__
 std::vector<std::string> ls(const char* root, const char* prefix, LS_FLAGS flags)
 {
     std::vector<std::string> list;
@@ -109,28 +114,106 @@
     return read_symlink(link.c_str());
 }
 
+template <>
+int read_sysctl(const char* name)
+{
+    int value;
+    size_t len = sizeof(value);
+
+    if (sysctlbyname(name, &value, &len, NULL, 0) == 0)
+        return value;
+    else return -1;
+}
+
+template <>
+std::string read_sysctl(const char* name)
+{
+    size_t len;
+
+    // How large buffer do we need?
+    if (sysctlbyname(name, NULL, &len, NULL, 0) != 0)
+        return "";
+
+    std::string value(len, '\0');
+    // Now read the actual value into it.
+    if (sysctlbyname(name, &value[0], &len, NULL, 0) != 0)
+        return "";
+    return std::string(value.c_str()); // drop the trailing NUL
+}
+
 std::string get_exe_path()
 {
-    return read_symlink("/proc/self/exe");
+    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
+    char buf[PATH_MAX];
+    size_t len = sizeof(buf);
+
+    // Works without procfs/linprocfs mounted
+    if (sysctl(mib, 4, buf, &len, NULL, 0) == 0 && len > 0)
+        return std::string(buf);
+
+    return read_symlink(PROCDIR "/self/exe");
+}
+
+bool is_wine_loader(const std::string& name)
+{
+    // FreeBSD's Wine is normally started without the preloader, so the
+    // process is plain wine/wine64 there.
+    return name == "wine" || name == "wine64"
+        || name == "wine-preloader" || name == "wine64-preloader";
+}
+
+// Process name (like /proc/self/comm) without needing procfs/linprocfs
+static std::string get_proc_comm()
+{
+    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)getpid() };
+    struct kinfo_proc kp;
+    size_t len = sizeof(kp);
+    if (sysctl(mib, 4, &kp, &len, NULL, 0) == 0 && len >= sizeof(kp))
+        return std::string(kp.ki_comm);
+
+    return read_line(PROCDIR "/self/comm");
+}
+
+// Command line, arguments separated by NUL (like /proc/self/cmdline)
+static std::string get_proc_args()
+{
+    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ARGS, (int)getpid() };
+    size_t len = 0;
+    if (sysctl(mib, 4, NULL, &len, NULL, 0) == 0 && len > 0) {
+        std::string buf(len, '\0');
+        if (sysctl(mib, 4, &buf[0], &len, NULL, 0) == 0 && len > 0) {
+            buf.resize(len);
+            return buf;
+        }
+    }
+
+    std::ifstream file(PROCDIR "/self/cmdline");
+    std::stringstream ss;
+    ss << file.rdbuf();
+    return ss.str();
 }
 
 std::string get_wine_exe_name(bool keep_ext)
 {
     const std::string exe_path = get_exe_path();
-    if (!ends_with(exe_path, "wine-preloader") && !ends_with(exe_path, "wine64-preloader")) {
+    if (!is_wine_loader(get_basename(std::string(exe_path)))) {
         return std::string();
     }
 
-    std::string line = read_line("/proc/self/comm"); // max 16 characters though
+    std::string line = get_proc_comm(); // max 16 characters though
     if (ends_with(line, ".exe", true))
     {
         auto dot = keep_ext ? std::string::npos : line.find_last_of('.');
         return line.substr(0, dot);
     }
 
-    std::ifstream cmdline("/proc/self/cmdline");
+    std::istringstream cmdline(get_proc_args());
     // Iterate over arguments (separated by NUL byte).
     while (std::getline(cmdline, line, '\0')) {
+        // skip the Wine loader itself (argv[0])
+        if (is_wine_loader(get_basename(std::string(line))))
+            continue;
+
         auto n = std::string::npos;
         if (!line.empty()
             && ((n = line.find_last_of("/\\")) != std::string::npos)
@@ -184,17 +267,20 @@
     return path;
 }
 
-bool lib_loaded(const std::string& lib) {
-    fs::path path("/proc/self/map_files/");
-    for (auto& p : fs::directory_iterator(path)) {
-        auto file = p.path().string();
-        auto sym = read_symlink(file.c_str());
-        if (sym.find(lib) != std::string::npos) {
-            return true;
-        }
+static int lib_loaded_cb(struct dl_phdr_info *info, size_t, void *data)
+{
+    auto *args = static_cast<std::pair<const std::string *, bool> *>(data);
+    if (info->dlpi_name && *info->dlpi_name
+        && strstr(info->dlpi_name, args->first->c_str())) {
+        args->second = true;
+        return 1;
     }
-    return false;
-
+    return 0;
 }
 
-#endif // __linux__
+bool lib_loaded(const std::string& lib) {
+    // Look at the loaded objects instead of procfs/linprocfs
+    std::pair<const std::string *, bool> args(&lib, false);
+    dl_iterate_phdr(lib_loaded_cb, &args);
+    return args.second;
+}
