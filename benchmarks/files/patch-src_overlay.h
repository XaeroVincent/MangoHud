--- src/overlay.h.orig
+++ src/overlay.h
@@ -102,6 +102,7 @@
 void init_cpu_stats(overlay_params& params);
 void check_keybinds(overlay_params& params, uint32_t vendorID);
 void init_system_info(void);
+void init_wine_info(void);
 void FpsLimiter(struct fps_limit& stats);
 std::string get_device_name(uint32_t vendorID, uint32_t deviceID);
 void create_fonts(ImFontAtlas* font_atlas, const overlay_params& params, ImFont*& small_font, ImFont*& text_font);
