#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/rmt.h"

#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define RMT_TX_GPIO 18

static const char *TAG = "spimaster";

typedef union {
	struct __attribute__ ((packed)) {
		uint8_t r, g, b;
	};
	uint32_t num;
} rgbVal;

inline rgbVal makeRGBVal(uint8_t r, uint8_t g, uint8_t b) {
	rgbVal v;
	v.r = r;
	v.g = g;
	v.b = b;
	return v;
}

rmt_item32_t items[] = {
	// E : dot
	{{{ 32767, 1, 32767, 0 }}}, // dot
	//
	{{{ 32767, 0, 32767, 0 }}}, // SPACE
	// S : dot, dot, dot
	{{{ 32767, 1, 32767, 0 }}}, // dot
	{{{ 32767, 1, 32767, 0 }}}, // dot
	{{{ 32767, 1, 32767, 0 }}}, // dot
	//
	{{{ 32767, 0, 32767, 0 }}}, // SPACE
	// P : dot, dash, dash, dot
	{{{ 32767, 1, 32767, 0 }}}, // dot
	{{{ 32767, 1, 32767, 1 }}},
	{{{ 32767, 1, 32767, 0 }}}, // dash
	{{{ 32767, 1, 32767, 1 }}},
	{{{ 32767, 1, 32767, 0 }}}, // dash
	{{{ 32767, 1, 32767, 0 }}}, // dot
	// RMT end marker
	{{{ 0, 1, 0, 0 }}}
};

#define NUM(a) (sizeof(a) / sizeof(*a))
#define RMT_BLOCK_LEN	32
#define data_one  {{{ 255, 1, 255, 0 }}}
#define data_zero {{{ 255, 0, 255, 1 }}}
#define data_5usblank {{{ 20000, 0, 18000, 0 }}}

rmt_item32_t packet_resetdevice[] = {
	data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one,
	data_zero,
	data_one,
	data_zero,
	data_zero
}; //Reset device (19 bits, 15 x 0b1, 1 x 0b0, 1 x 0b1 & 2 x 0b0)

rmt_item32_t packet_blank[] = {
	data_5usblank
}; //Blank

rmt_item32_t packet_delayresetsync[] = {
	data_5usblank, data_5usblank
}; //Delay (1mS) between Reset device and Synchronize device

rmt_item32_t packet_syncdevice[] = {
	data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one,
	data_zero, data_zero, data_zero,
	data_one,
	data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero
}; //Synchronize device (30 bits, 15 x 0b1, 3 x 0b0, 1 x 0b1 & 11 x 0b0)

rmt_item32_t packet_startdata[] = {
	data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one,
	data_zero, data_zero,
	data_one,
	data_zero
}; //Start of data (19 bits, 15 x 0b1, 2 x 0b0, 1 x 0b1 & 1 x 0b0)

//Convert uint8_t type of data to rmt format data.
static void IRAM_ATTR u8_to_rmt(const void* src, rmt_item32_t* dest, size_t src_size, size_t wanted_num, size_t* translated_size, size_t* item_num) {
	if(src == NULL || dest == NULL) {
		*translated_size = 0;
		*item_num = 0;
		return;
	}
	const rmt_item32_t bit0 = {{{ 32767, 1, 15000, 0 }}}; //Logical 0
	const rmt_item32_t bit1 = {{{ 32767, 1, 32767, 0 }}}; //Logical 1
	size_t size = 0;
	size_t num = 0;
	uint8_t *psrc = (uint8_t *)src;
	rmt_item32_t* pdest = dest;
	while (size < src_size && num < wanted_num) {
		for(int i = 0; i < 8; i++) {
			if(*psrc & (0x1 << i)) {
				pdest->val =  bit1.val;
			} else {
				pdest->val =  bit0.val;
			}
			num++;
			pdest++;
		}
		size++;
		psrc++;
	}
	*translated_size = size;
	*item_num = num;
}

static void light_control(void *arg) {
	ESP_LOGI(TAG, "[APP] Init");

	rmt_config_t config;
	config.rmt_mode = RMT_MODE_TX;
	config.channel = RMT_TX_CHANNEL;
	config.gpio_num = RMT_TX_GPIO;
	config.mem_block_num = 1;
	config.tx_config.loop_en = 0;
	config.tx_config.carrier_en = 0;
	config.tx_config.idle_output_en = 1;
	config.tx_config.idle_level = 0;
	config.clk_div = 1;

	ESP_ERROR_CHECK(rmt_config(&config));
	ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
	//ESP_ERROR_CHECK(rmt_translator_init(config.channel, u8_to_rmt));

	//int number_of_items = sizeof(items) / sizeof(items[0]);
	//int number_of_items = sizeof(packet_startdata) / sizeof(packet_startdata[0]);

	//Blank RMT buffer
	//for (int i = 0; i<64; i++) {
	//	ESP_ERROR_CHECK(rmt_fill_tx_items(RMT_TX_CHANNEL, packet_blank, 1, i*RMT_BLOCK_LEN));
	//}

	ESP_LOGI(TAG, "[APP] Init done");
	while (1) {
		ESP_LOGI(TAG, "[APP] Send packet");
		//ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL, items, number_of_items, true));
		ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL, packet_resetdevice, NUM(packet_resetdevice), true));
		ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL, packet_delayresetsync, NUM(packet_delayresetsync), true));
		ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL, packet_syncdevice, NUM(packet_syncdevice), true));
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}

void app_main() {
	ESP_LOGI(TAG, "[APP] Startup");
	xTaskCreate(light_control, "light_control", 4096, NULL, 5, NULL);
}

