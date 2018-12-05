#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

//#define PIN_NUM_MISO 12
//#define PIN_NUM_MOSI 13
//#define PIN_NUM_CLK  14
//#define PIN_NUM_CS   15
#define PIN_NUM_MISO 25
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  19
#define PIN_NUM_CS   22

static const char *TAG = "spimaster";

void send_stuff(spi_device_handle_t spi) {
	uint8_t buf[4];
	buf[0] = 1;
	buf[1] = 2;
	buf[2] = 3;
	buf[3] = 4;

	spi_transaction_t trans;

	memset(&trans, 0, sizeof(spi_transaction_t));
	trans.length=8*4;
	trans.tx_buffer=&buf;
	trans.flags=SPI_TRANS_USE_TXDATA;
	trans.user=(void*)0;

	ESP_ERROR_CHECK(spi_device_queue_trans(spi, &trans, portMAX_DELAY));

	spi_transaction_t *recPayload;
	recPayload = &trans;
	ESP_ERROR_CHECK(spi_device_get_trans_result(spi, &recPayload, portMAX_DELAY));

	//uint8_t cmd[3];
	//cmd[0] = 1;
	//cmd[1] = 2;
	//cmd[2] = 3;

	//spi_transaction_t t;
	//memset(&t, 0, sizeof(t));
	//t.length=24;
	//t.tx_buffer=&cmd;
	//t.user=(void*)0;
	//ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}

static void light_control(void *arg) {
	ESP_LOGI(TAG, "[APP] Init SPI ");
	spi_device_handle_t spi;
	spi_bus_config_t buscfg = {
		.miso_io_num=PIN_NUM_MISO,
		.mosi_io_num=PIN_NUM_MOSI,
		.sclk_io_num=PIN_NUM_CLK,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1,
		.max_transfer_sz=32
	};
	spi_device_interface_config_t devcfg = {
		.clock_speed_hz=10*1000*1000,
		.mode=0,
		.spics_io_num=PIN_NUM_CS,
		.queue_size=7,
	};
	ESP_ERROR_CHECK(spi_bus_initialize(HSPI_HOST, &buscfg, 1));
	ESP_ERROR_CHECK(spi_bus_add_device(HSPI_HOST, &devcfg, &spi));
	ESP_LOGI(TAG, "[APP] SPI init done");
	while (1) {
		ESP_LOGI(TAG, "[APP] Send packet");
		send_stuff(spi);
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
}

void app_main() {
	ESP_LOGI(TAG, "[APP] Startup");
	xTaskCreate(light_control, "light_control", 4096, NULL, 5, NULL);
}

