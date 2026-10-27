--- src/file_utils.h.orig
+++ src/file_utils.h
@@ -17,9 +17,13 @@
 bool dir_exists(const std::string& path);
 std::string read_symlink(const char * link);
 std::string read_symlink(const std::string&& link);
+template <typename T> T read_sysctl(const char*);
+template <> int read_sysctl<int>(const char*);
+template <> std::string read_sysctl<std::string>(const char*);
 std::string get_basename(const std::string&& path); //prefix so it doesn't conflict libgen
 std::string get_exe_path();
 std::string get_wine_exe_name(bool keep_ext = false);
+bool is_wine_loader(const std::string& name);
 std::string get_home_dir();
 std::string get_data_dir();
 std::string get_config_dir();
