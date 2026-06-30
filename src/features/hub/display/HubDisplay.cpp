#include "features/hub/display/HubDisplay.hpp"
#include "features/hub/display/Co5300Panel.hpp"
#include "app_config.hpp"

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE
#endif
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {

Co5300Panel g_panel;
lv_color_t* g_drawBuffer = nullptr;
lv_disp_draw_buf_t g_drawBuf = {};
lv_disp_drv_t g_dispDrv = {};
esp_timer_handle_t g_lvglTickTimer = nullptr;

void lvglTickCallback(void*) {
  lv_tick_inc(AppConfig::HubDisplay::kLvglTickPeriodMs);
}

void rounderCallback(lv_disp_drv_t*, lv_area_t* area) {
  if (area == nullptr) return;
  if ((area->x1 & 0x1) != 0) area->x1 -= 1;
  if ((area->y1 & 0x1) != 0) area->y1 -= 1;
  if ((area->x2 & 0x1) == 0) area->x2 += 1;
  if ((area->y2 & 0x1) == 0) area->y2 += 1;
}

void flushDisplay(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  if (area != nullptr && color_p != nullptr) {
    g_panel.flush(*area, color_p);
  }
  lv_disp_flush_ready(disp);
}

}  // namespace

bool HubDisplay::begin() {
  String err;
  if (!g_panel.begin(err)) {
    Serial.printf("[HUBDISPLAY] panel init failed: %s\n", err.c_str());
    return false;
  }

  const size_t pixelCount =
      (static_cast<size_t>(AppConfig::HubDisplay::kWidth) *
       static_cast<size_t>(AppConfig::HubDisplay::kHeight)) /
      static_cast<size_t>(AppConfig::HubDisplay::kDrawBufferDivisor);

  g_drawBuffer = static_cast<lv_color_t*>(
      heap_caps_malloc(pixelCount * sizeof(lv_color_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_drawBuffer == nullptr) {
    g_drawBuffer = static_cast<lv_color_t*>(
        heap_caps_malloc(pixelCount * sizeof(lv_color_t), MALLOC_CAP_8BIT));
  }
  if (g_drawBuffer == nullptr) {
    Serial.println("[HUBDISPLAY] draw buffer alloc failed");
    return false;
  }

  lv_init();
  lv_disp_draw_buf_init(&g_drawBuf, g_drawBuffer, nullptr, pixelCount);
  lv_disp_drv_init(&g_dispDrv);
  g_dispDrv.hor_res = AppConfig::HubDisplay::kWidth;
  g_dispDrv.ver_res = AppConfig::HubDisplay::kHeight;
  g_dispDrv.flush_cb = flushDisplay;
  g_dispDrv.rounder_cb = rounderCallback;
  g_dispDrv.draw_buf = &g_drawBuf;
  if (lv_disp_drv_register(&g_dispDrv) == nullptr) {
    Serial.println("[HUBDISPLAY] lvgl display register failed");
    heap_caps_free(g_drawBuffer);
    g_drawBuffer = nullptr;
    return false;
  }

  const esp_timer_create_args_t tickArgs = {
      .callback = &lvglTickCallback,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "hub_lvgl_tick",
      .skip_unhandled_events = false,
  };
  if (esp_timer_create(&tickArgs, &g_lvglTickTimer) != ESP_OK ||
      esp_timer_start_periodic(
          g_lvglTickTimer,
          AppConfig::HubDisplay::kLvglTickPeriodMs * 1000ULL) != ESP_OK) {
    Serial.println("[HUBDISPLAY] lvgl tick timer failed");
    heap_caps_free(g_drawBuffer);
    g_drawBuffer = nullptr;
    return false;
  }

  ready_ = true;
  return true;
}

void HubDisplay::tick() {
  if (!ready_) return;
  lv_timer_handler();
}

bool HubDisplay::enterSleep() {
  if (!ready_) return false;
  if (g_lvglTickTimer != nullptr) {
    esp_timer_stop(g_lvglTickTimer);
  }
  return g_panel.enterSleep();
}

bool HubDisplay::exitSleep() {
  if (!ready_) return false;
  if (!g_panel.exitSleep()) return false;
  if (g_lvglTickTimer != nullptr) {
    esp_timer_start_periodic(g_lvglTickTimer,
                             AppConfig::HubDisplay::kLvglTickPeriodMs * 1000ULL);
  }
  return true;
}
