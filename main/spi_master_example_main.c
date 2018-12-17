#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/rmt.h"

static const char *TAG = "spimaster";

typedef union {
	struct __attribute__ ((packed)) {
		uint8_t r, g, b;
	};
	uint32_t num;
} rgbVal;

typedef struct {
	rmt_config_t config;
	rmt_item32_t *pPacket;
	int packetSize;
	int numberOfLeds;
	int indexReset;
	int indexDelayResetSync;
	int indexSync;
	int indexDelaySyncStart;
	int indexStart;
	int indexPacket;
	int indexStartEnd;
} TLSCONFIG;

#define NUM(a) (sizeof(a) / sizeof(*a))
#define RMT_BLOCK_LEN	32
#define RMT_MAX_DELAY 32767
#define blank_187us 187
#define data_one  {{{ 3, 1, 3, 0 }}}
#define data_zero {{{ 3, 0, 3, 1 }}}
#define data_1msblank {{{ 500, 0, 500, 0 }}}
#define data_rgbdelay {{{ 20, 0, 20, 0 }}}

rmt_item32_t packet_resetdevice[] = {
	data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one, data_one,
	data_zero,
	data_one,
	data_zero,
	data_zero
}; //Reset device (19 bits, 15 x 0b1, 1 x 0b0, 1 x 0b1 & 2 x 0b0)

rmt_item32_t packet_delayresetsync[] = {
	data_1msblank
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

rmt_item32_t packet_startandblackrgb[] = {
	data_zero,
	data_zero, data_zero, data_zero, data_zero, data_one, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_one,
	data_zero,
	data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_one,
	data_zero,
	data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_zero, data_one //Data for each pixel (39 bits, 1 x 0b0, 12bits pixel data, 1 x 0b0, 12bits pixel data, 1 x 0b0 & 12bits pixel data)
}; 

void generate_packet_startreset_silence(rmt_item32_t packet_startreset_silence[], int numberof_max_delays, int remainderdelay) {
	rmt_item32_t delaypacket;
	for (int i = 0; i<numberof_max_delays; i++) {
		delaypacket.duration0 = RMT_MAX_DELAY;
		packet_startreset_silence[i] = delaypacket;
	}
	delaypacket.duration0 = remainderdelay;
	packet_startreset_silence[numberof_max_delays] = delaypacket;
}

void generate_big_package(TLSCONFIG *conf) {
	//Calculate size of delay for buildning the packet
	rmt_item32_t delaypacket;
	delaypacket.duration0 = blank_187us*conf->numberOfLeds-1;
	delaypacket.level0 = 0;
	delaypacket.duration1 = 1;
	delaypacket.level1 = 0;

	//Calculate start indexes, delay between Sync and Star arrays numberofleds*187µs (delay = numberofleds/baudMHz * 30)
	int startrgbSize = sizeof(packet_startandblackrgb)/sizeof(rmt_item32_t);
	conf->packetSize = (sizeof(packet_resetdevice)+
							sizeof(packet_delayresetsync)+
							sizeof(packet_syncdevice))/
						sizeof(rmt_item32_t);
	conf->packetSize += sizeof(delaypacket)/sizeof(rmt_item32_t);
	conf->packetSize += sizeof(packet_startdata)/sizeof(rmt_item32_t);
	conf->packetSize += startrgbSize*conf->numberOfLeds;
	conf->packetSize += sizeof(packet_startdata)/sizeof(rmt_item32_t);
	conf->indexReset = 0;
	conf->indexDelayResetSync = conf->indexReset + sizeof(packet_resetdevice)/sizeof(rmt_item32_t);
	conf->indexSync = conf->indexDelayResetSync + sizeof(packet_delayresetsync)/sizeof(rmt_item32_t);
	conf->indexDelaySyncStart = conf->indexSync + sizeof(packet_syncdevice)/sizeof(rmt_item32_t);
	conf->indexStart = conf->indexDelaySyncStart + sizeof(delaypacket)/sizeof(rmt_item32_t);
	conf->indexPacket = conf->indexStart + sizeof(packet_startdata)/sizeof(rmt_item32_t);
	conf->indexStartEnd = conf->indexPacket + (startrgbSize*conf->numberOfLeds);

	//Allocate the big TLS3001 packet
	conf->pPacket = calloc(conf->packetSize, sizeof(rmt_item32_t));

	//Fill packet with reset, start, sync and delays
	memcpy(&conf->pPacket[conf->indexReset], packet_resetdevice, sizeof(packet_resetdevice));
	memcpy(&conf->pPacket[conf->indexDelayResetSync], packet_delayresetsync, sizeof(packet_delayresetsync));
	memcpy(&conf->pPacket[conf->indexSync], packet_syncdevice, sizeof(packet_syncdevice));
	memcpy(&conf->pPacket[conf->indexDelaySyncStart], &delaypacket, sizeof(delaypacket));
	memcpy(&conf->pPacket[conf->indexStart], packet_startdata, sizeof(packet_startdata));

	//Fill the rest of the packet with start and black RGB packets
	//memcpy(&conf->pPacket[conf->indexStart], packet_startandblackrgb, sizeof(packet_startandblackrgb));
	for (int i = 0; i < conf->numberOfLeds; i++) {
		memcpy(&conf->pPacket[conf->indexPacket+(startrgbSize*i)], packet_startandblackrgb, sizeof(packet_startandblackrgb));
	}

	memcpy(&conf->pPacket[conf->indexStartEnd], packet_startdata, sizeof(packet_startdata));
}

static void light_control(void *arg) {
	ESP_LOGI(TAG, "[APP] Init");

	int number_of_leds = 2;

	int total_delay = number_of_leds * blank_187us;
	int numberof_max_delays = total_delay / RMT_MAX_DELAY;
	int remainderdelay = total_delay % RMT_MAX_DELAY;
	rmt_item32_t packet_led_silence[numberof_max_delays+1];
	generate_packet_startreset_silence(packet_led_silence, numberof_max_delays, remainderdelay);
	ESP_LOGI(TAG, "[APP] Total leds: %d, number of max delays: %d, remainder: %d", number_of_leds, numberof_max_delays, remainderdelay);

	TLSCONFIG tlsconf;
	tlsconf.numberOfLeds = 10;
	tlsconf.config.rmt_mode = RMT_MODE_TX;
	tlsconf.config.channel = RMT_CHANNEL_0;
	tlsconf.config.gpio_num = 18;
	tlsconf.config.mem_block_num = 1;
	tlsconf.config.tx_config.loop_en = 0;
	tlsconf.config.tx_config.carrier_en = 0;
	tlsconf.config.tx_config.idle_output_en = 1;
	tlsconf.config.tx_config.idle_level = 0;
	tlsconf.config.clk_div = 80; //Gives: 1 duration = 1µs

	ESP_ERROR_CHECK(rmt_config(&tlsconf.config));
	ESP_ERROR_CHECK(rmt_driver_install(tlsconf.config.channel, 0, 0));

	generate_big_package(&tlsconf);

	ESP_LOGI(TAG, "[APP] Init done");
	while (1) {
		ESP_LOGI(TAG, "[APP] Send packet");
		ESP_ERROR_CHECK(rmt_write_items(tlsconf.config.channel, tlsconf.pPacket, tlsconf.packetSize, true));
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}

void app_main() {
	ESP_LOGI(TAG, "[APP] Startup");
	xTaskCreate(light_control, "light_control", 4096, NULL, 5, NULL);
}

