/*
 * battery.c
 *
 *  Created on: 18 нояб. 2023 г.
 *      Author: pvvx
 */

#include "tl_common.h"
#include "app_main.h"
#include "battery.h"
#if USE_SENSOR_TH
#include "sensor_th.h"
#endif
#if USE_SENSOR_XBR818
#include "sensor_xbr818.h"
#endif
#include "lcd.h"

measured_battery_t measured_battery;
#if defined(USE_BATTERY) && (USE_BATTERY == BATTERY_2AAA)
// {empty, full} mV mapped onto 0..200 (0.5% units). The alkaline pair
// reproduces the original (mv - BATTERY_SAFETY_THRESHOLD) / 4 curve exactly.
static const u16 battery_curve_mv[][2] = {
	[BATTERY_CHEM_ALKALINE] = {BATTERY_SAFETY_THRESHOLD, BATTERY_SAFETY_THRESHOLD + BATTERY_ALKALINE_SPAN_MV},
	[BATTERY_CHEM_NIMH]     = {BATTERY_NIMH_EMPTY_MV, BATTERY_NIMH_FULL_MV},
};

// Kept private so the only path to the index is this clamp.
static u8 battery_chemistry = BATTERY_CHEM_ALKALINE;

u8 battery_set_chemistry(u8 chem) {
	battery_chemistry = (chem > BATTERY_CHEM_NIMH) ? BATTERY_CHEM_ALKALINE : chem;
	return battery_chemistry;
}
#endif

// Maps the running average onto measured_battery.level. Split out of
// battery_detect() so a chemistry change can be applied without touching the
// ADC or the low-voltage shutdown path.
void battery_recalc_level(void)
{
#if defined(USE_BATTERY) && (USE_BATTERY == BATTERY_2AAA)
	u16 empty_mv = battery_curve_mv[battery_chemistry][0];
	u16 span_mv = battery_curve_mv[battery_chemistry][1] - empty_mv;
#else
	const u16 empty_mv = BATTERY_SAFETY_THRESHOLD;
	const u16 span_mv = BATTERY_ALKALINE_SPAN_MV;
#endif
	u16 battery_level = 0;
	if(measured_battery.average_mv > empty_mv) {
		battery_level = (u32)(measured_battery.average_mv - empty_mv) * 200 / span_mv;
		if(battery_level > 200)
			battery_level = 200;
	}
	measured_battery.level = (u8)battery_level;
#if USE_BLE
	measured_battery.batVal = (u8)(battery_level >> 1);
#endif
}

#define _BAT_SPEED_CODE_SEC_ //_attribute_ram_code_sec_ // for speed

#define BAT_AVERAGE_COUNT_SHL	9 // 4,5,6,7,8,9,10,11,12 -> 16,32,64,128,256,512,1024,2048,4096

_BAT_SPEED_CODE_SEC_
__attribute__((optimize("-Os")))
void battery_detect(bool startup_flg)
{
	u16 battery_level = BATTERY_LOW_POWER;
	if(startup_flg)
		battery_level = BATTERY_SAFETY_THRESHOLD;
	adc_channel_init(SHL_ADC_VBAT);
	measured_battery.mv = get_adc_mv(0);
	if(measured_battery.mv < battery_level){
#if PM_ENABLE
#if USE_DISPLAY
		display_off();
#endif
#if USE_SENSOR_TH
		sensor_go_sleep();
#endif
#if USE_SENSOR_XBR818
		xbr818_go_sleep();
#endif
		drv_pm_longSleep(PM_SLEEP_MODE_DEEPSLEEP, PM_WAKEUP_SRC_TIMER, LOW_POWER_SLEEP_TIME_ms);
#else
		SYSTEM_RESET();
#endif
	}
	measured_battery.summ += measured_battery.mv;
	measured_battery.cnt++;
	if(measured_battery.cnt >= (1<<BAT_AVERAGE_COUNT_SHL)) {
		measured_battery.average_mv = measured_battery.summ >> BAT_AVERAGE_COUNT_SHL;
		measured_battery.summ -= measured_battery.average_mv;
		measured_battery.cnt--;
	} else {
		measured_battery.average_mv = measured_battery.summ / measured_battery.cnt;
	}
	battery_recalc_level();
    measured_battery.flag = 0xff;
}
