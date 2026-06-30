/*
 * BringupScreen.cpp — LVGL diagnostics screen for HUB board bring-up.
 * Shows per-IC status dots (green/red/grey) + live telemetry lines.
 * Only uses lv_label and lv_obj (colored rectangles); no bar/btn/arc/led.
 * create() builds the widget tree once; update() only sets text/colors.
 */
#include "features/hub/ui/BringupScreen.hpp"
#include <lvgl.h>
#include <stdio.h>

namespace BringupScreen {

static constexpr lv_coord_t kDotSize = 12;
static constexpr lv_coord_t kRowH    = 20;
static constexpr lv_coord_t kMX      = 6;
static constexpr lv_coord_t kLX      = kMX + kDotSize + 4;

static const lv_color_t kGreen = LV_COLOR_MAKE(0x00, 0xCC, 0x44);
static const lv_color_t kRed   = LV_COLOR_MAKE(0xCC, 0x22, 0x00);
static const lv_color_t kGrey  = LV_COLOR_MAKE(0x55, 0x55, 0x55);

static lv_obj_t* s_dots[7]  = {};
static lv_obj_t* s_powerLbl = nullptr;
static lv_obj_t* s_touchLbl = nullptr;
static lv_obj_t* s_imuLbl   = nullptr;
static lv_obj_t* s_rtcLbl   = nullptr;
static lv_obj_t* s_sysLbl   = nullptr;

static lv_color_t stateColor(uint8_t st) {
    return st == 1 ? kGreen : st == 2 ? kRed : kGrey;
}

static lv_obj_t* makeDot(lv_obj_t* parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t* d = lv_obj_create(parent);
    lv_obj_set_size(d, kDotSize, kDotSize);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d, kGrey, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    return d;
}

static lv_obj_t* makeLbl(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

void create() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, LV_COLOR_MAKE(0x10, 0x10, 0x10), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    makeLbl(scr, kMX, 4, "HUB BRINGUP DIAGNOSTICS");

    static const char* const kNames[7] = {
        "AXP2101", "QMI8658", "PCF85063", "CST9217", "TCA9554", "ES8311", "ES7210"
    };
    for (int i = 0; i < 7; ++i) {
        const lv_coord_t y = static_cast<lv_coord_t>(28 + i * kRowH);
        s_dots[i] = makeDot(scr, kMX, static_cast<lv_coord_t>(y + (kRowH - kDotSize) / 2));
        makeLbl(scr, kLX, static_cast<lv_coord_t>(y + 2), kNames[i]);
    }

    const lv_coord_t yInfo = static_cast<lv_coord_t>(28 + 7 * kRowH + 8);
    s_powerLbl = makeLbl(scr, kMX, yInfo,                                  "BATT: ---");
    s_touchLbl = makeLbl(scr, kMX, static_cast<lv_coord_t>(yInfo + kRowH), "TOUCH: ---");
    s_imuLbl   = makeLbl(scr, kMX, static_cast<lv_coord_t>(yInfo + 2 * kRowH), "IMU: x=0.00 y=0.00 z=0.00");
    s_rtcLbl   = makeLbl(scr, kMX, static_cast<lv_coord_t>(yInfo + 3 * kRowH), "RTC: --:--:--");
    s_sysLbl   = makeLbl(scr, kMX, static_cast<lv_coord_t>(yInfo + 4 * kRowH), "FW: ---");
}

void update(const Snapshot& s) {
    if (!s_powerLbl) return;  // guard: create() not called yet

    for (int i = 0; i < 7; ++i)
        lv_obj_set_style_bg_color(s_dots[i], stateColor(s.ics[i].state), LV_PART_MAIN);

    char buf[80];

    if (s.batteryPercent >= 0)
        snprintf(buf, sizeof(buf), "BATT:%d%% %umV%s%s",
                 s.batteryPercent, (unsigned)s.batteryMv,
                 s.charging ? " CHG" : "", s.vbus ? " VBUS" : "");
    else
        snprintf(buf, sizeof(buf), "BATT:n/a%s", s.vbus ? " VBUS" : "");
    lv_label_set_text(s_powerLbl, buf);

    if (s.touchPressed)
        snprintf(buf, sizeof(buf), "TOUCH:(%d,%d)", (int)s.touchX, (int)s.touchY);
    else
        snprintf(buf, sizeof(buf), "TOUCH:---");
    lv_label_set_text(s_touchLbl, buf);

    snprintf(buf, sizeof(buf), "IMU:x=%.2f y=%.2f z=%.2f",
             s.accelX, s.accelY, s.accelZ);
    lv_label_set_text(s_imuLbl, buf);

    snprintf(buf, sizeof(buf), "RTC:%02u:%02u:%02u",
             (unsigned)s.rtcHours, (unsigned)s.rtcMinutes, (unsigned)s.rtcSeconds);
    lv_label_set_text(s_rtcLbl, buf);

    snprintf(buf, sizeof(buf), "FW:%s H:%uk P:%uk",
             s.firmware ? s.firmware : "?",
             (unsigned)(s.freeHeap  / 1024),
             (unsigned)(s.freePsram / 1024));
    lv_label_set_text(s_sysLbl, buf);
}

}  // namespace BringupScreen
