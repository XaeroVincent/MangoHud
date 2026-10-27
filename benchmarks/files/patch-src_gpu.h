--- src/gpu.h.orig
+++ src/gpu.h
@@ -43,6 +43,10 @@
 void getNvidiaGpuInfo(const struct overlay_params& params);
 void getAmdGpuInfo(void);
 void getIntelGpuInfo();
+#if defined(__FreeBSD__) && defined(HAVE_LIBDRM_AMDGPU)
+bool amdgpu_freebsd_init(uint32_t reported_deviceID, uint32_t& deviceID_out);
+void amdgpu_freebsd_update();
+#endif
 bool checkNvidia(const char *pci_dev);
 extern void nvapi_util();
 extern bool checkNVAPI();
