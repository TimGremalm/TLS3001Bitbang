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
#define SAMPLE_CNT  (10)

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
	// enable the carrier to be able to hear the Morse sound
	// if the RMT_TX_GPIO is connected to a speaker
	config.tx_config.carrier_en = 1;
	config.tx_config.idle_output_en = 1;
	config.tx_config.idle_level = 0;
	config.tx_config.carrier_duty_percent = 50;
	// set audible career frequency of 611 Hz
	// actually 611 Hz is the minimum, that can be set
	// with current implementation of the RMT API
	config.tx_config.carrier_freq_hz = 611;
	config.tx_config.carrier_level = 1;
	// set the maximum clock divider to be able to output
	// RMT pulses in range of about one hundred milliseconds
	config.clk_div = 255;

	ESP_ERROR_CHECK(rmt_config(&config));
	ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
	ESP_ERROR_CHECK(rmt_translator_init(config.channel, u8_to_rmt));

	int number_of_items = sizeof(items) / sizeof(items[0]);
	const uint8_t sample[SAMPLE_CNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

	ESP_LOGI(TAG, "[APP] Init done");
	while (1) {
		ESP_LOGI(TAG, "[APP] Send packet");
		ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL, items, number_of_items, true));
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		ESP_ERROR_CHECK(rmt_write_sample(RMT_TX_CHANNEL, sample, SAMPLE_CNT, true));
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}
}

void app_main() {
	ESP_LOGI(TAG, "[APP] Startup");
	xTaskCreate(light_control, "light_control", 4096, NULL, 5, NULL);
}

