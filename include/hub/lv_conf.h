#pragma once

/*
 * Minimal LVGL configuration for the hub AMOLED prototype.
 *
 * LVGL's lv_conf_internal.h supplies defaults for any option omitted here, so
 * we only override the settings that matter for the hub board's 466x466 RGB565
 * panel and modest particle-scene renderer.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 1

#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "include/hub/LvglMemory.h"
#define LV_MEM_CUSTOM_ALLOC  hubLvglMalloc
#define LV_MEM_CUSTOM_FREE   hubLvglFree
#define LV_MEM_CUSTOM_REALLOC hubLvglRealloc
#define LV_MEM_BUF_MAX_NUM 16

#define LV_DISP_DEF_REFR_PERIOD 16
#define LV_INDEV_DEF_READ_PERIOD 16
#define LV_DPI_DEF 130

#define LV_DRAW_COMPLEX 1
#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEFAULT &lv_font_montserrat_12

#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0

#define LV_USE_ARC 0
#if defined(DIYR_CONSOLE)
#  define LV_USE_BAR 1
#  define LV_USE_BTN 1
#  define LV_USE_SLIDER 1
#else
#  define LV_USE_BAR 0
#  define LV_USE_BTN 0
#  define LV_USE_SLIDER 0
#endif
#define LV_USE_BTNMATRIX 0
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 0
#define LV_USE_ROLLER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif  // LV_CONF_H
