/*
 * Example of using esp-homekit library to implement an air quality sensor usign and MQ135
 *
 * Copyright 2018 David B Brown @maccoylton
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * The use and OTA mechanis created by HomeACcessoryKid
 *
 */

#define DEVICE_MANUFACTURER "David B Brown"
#define DEVICE_NAME "Air-Quality"
#define DEVICE_MODEL "1"
#define DEVICE_SERIAL "12345678"
#define FW_VERSION "1.0"
#define TEMPERATURE_SENSOR_PIN 4
#define TEMPERATURE_POLL_PERIOD 10000

#include <stdio.h>
#include <math.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <button.h>
#include <esp8266_mq135.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <dht/dht.h>
#include <udplogger.h>
#include <stdout_redirect.h>
#include <led_codes.h>
#include <custom_characteristics.h>


// add this section to make your device OTA capable
// create the extra characteristic &ota_trigger, at the end of the primary service (before the NULL)
// it can be used in Eve, which will show it, where Home does not
// and apply the four other parameters in the accessories_information section

#include "ota-api.h"

const int LED_GPIO = 13;
const int button_gpio = 0;
int button_pressed_value=0; /*set to o when botton is connect to gound, 1 when button is providing +3.3V */


/* global varibale to support LEDs set to 0 wehre the LED is connected to GND, 1 where +3.3v */
int led_off_value=1;


homekit_characteristic_t ota_trigger      = API_OTA_TRIGGER;
homekit_characteristic_t name             = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer     = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial           = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model            = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision         = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);


//temperature sensor
homekit_characteristic_t current_temperature = HOMEKIT_CHARACTERISTIC_( CURRENT_TEMPERATURE, 0 );

//humidity ensor
homekit_characteristic_t current_relative_humidity    = HOMEKIT_CHARACTERISTIC_( CURRENT_RELATIVE_HUMIDITY, 0 );

//air quality sensor
homekit_characteristic_t air_quality                = HOMEKIT_CHARACTERISTIC_( AIR_QUALITY, 0 );
homekit_characteristic_t ozone_density              = HOMEKIT_CHARACTERISTIC_( OZONE_DENSITY, 0 );
homekit_characteristic_t nitrogen_dioxide_density	= HOMEKIT_CHARACTERISTIC_( NITROGEN_DIOXIDE_DENSITY, 0 );
homekit_characteristic_t sulphur_dioxide_density	= HOMEKIT_CHARACTERISTIC_( SULPHUR_DIOXIDE_DENSITY, 0 );
homekit_characteristic_t pm25_density               = HOMEKIT_CHARACTERISTIC_( PM25_DENSITY, 0 );
homekit_characteristic_t pm10_density               = HOMEKIT_CHARACTERISTIC_( PM10_DENSITY, 0 );
homekit_characteristic_t carbon_monoxide_level      = HOMEKIT_CHARACTERISTIC_( CARBON_MONOXIDE_LEVEL, 0 );

homekit_characteristic_t lpg_level                  = HOMEKIT_CHARACTERISTIC_( CUSTOM_LPG_LEVEL, 0 );
homekit_characteristic_t methane_level              = HOMEKIT_CHARACTERISTIC_( CUSTOM_METHANE_LEVEL, 0 );
homekit_characteristic_t ammonium_level             = HOMEKIT_CHARACTERISTIC_( CUSTOM_AMMONIUM_LEVEL, 0 );



float humidity_value, temperature_value;



void identify_task(void *_args) {
    led_code (LED_GPIO, IDENTIFY_ACCESSORY);
    vTaskDelete(NULL);
}

void identify(homekit_value_t _value) {
    printf("identify\n");
    xTaskCreate(identify_task, "identify", 128, NULL, 2, NULL);
}


void reset_configuration_task() {
    //Flash the LED first before we start the reset
    led_code (LED_GPIO, WIFI_CONFIG_RESET);
    
    //    printf("Resetting Wifi Config\n");
    
    //    wifi_config_reset();
    
    //    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    printf("Resetting HomeKit Config\n");
    
    homekit_server_reset();
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    printf("Restarting\n");
    
    sdk_system_restart();
    
    vTaskDelete(NULL);
}

void reset_configuration() {
    printf("Resetting Sonoff configuration\n");
    xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}



homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_sensor, .services=(homekit_service_t*[]){
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
            &name,
            &manufacturer,
            &serial,
            &model,
            &revision,
            HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),
            NULL
        }),
        HOMEKIT_SERVICE(TEMPERATURE_SENSOR, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Temperature Sensor"),
            &current_temperature,
            NULL
        }),
        HOMEKIT_SERVICE(HUMIDITY_SENSOR, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Humidity Sensor"),
            &current_relative_humidity,
            NULL
        }),
        HOMEKIT_SERVICE(AIR_QUALITY_SENSOR, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Air Quality Sensor"),
            &air_quality,
            &ozone_density,
            &nitrogen_dioxide_density,
            &sulphur_dioxide_density,
            &pm25_density,
            &pm10_density,
            &carbon_monoxide_level,
            &lpg_level,
            &methane_level,
            &ammonium_level,
            &ota_trigger,
            NULL
        }),
        NULL
    }),
    NULL
};



void button_callback(uint8_t gpio, button_event_t event) {
    switch (event) {
        case button_event_single_press:
            printf("\n\n***Button single press event: %d, doing nothin\n", event);
            break;
        case button_event_double_press:
            printf("\n\n***Button double press event: %d, doing nothin\n", event);
            break;
        case button_event_long_press:
            printf("\n\n***Button long press event: %d, resetting homekit config\n", event);
            reset_configuration();
            break;
        default:
            printf("\n\n***Unknown button event: %d\n", event);
    }
}

void create_accessory_name() {
    
    int serialLength = snprintf(NULL, 0, "%d", sdk_system_get_chip_id());
    
    char *serialNumberValue = malloc(serialLength + 1);
    
    snprintf(serialNumberValue, serialLength + 1, "%d", sdk_system_get_chip_id());
    
    int name_len = snprintf(NULL, 0, "%s-%s-%s",
                            DEVICE_NAME,
                            DEVICE_MODEL,
                            serialNumberValue);
    
    if (name_len > 63) {
        name_len = 63;
    }
    
    char *name_value = malloc(name_len + 1);
    
    snprintf(name_value, name_len + 1, "%s-%s-%s",
             DEVICE_NAME, DEVICE_MODEL, serialNumberValue);
    
    
    name.value = HOMEKIT_STRING(name_value);
    serial.value = name.value;
}


void temperature_sensor_task(void *_args) {
    
    bool success;
    
    gpio_set_pullup(TEMPERATURE_SENSOR_PIN, false, false);
    
    while (1) {
        success = dht_read_float_data(
                                      DHT_TYPE_DHT22, TEMPERATURE_SENSOR_PIN,
                                      &humidity_value, &temperature_value
                                      );
        
        if (success) {
            printf("Got readings: temperature %g, humidity %g\n", temperature_value, humidity_value);
            current_temperature.value = HOMEKIT_FLOAT(temperature_value);
            current_relative_humidity.value = HOMEKIT_FLOAT(humidity_value);
            
            homekit_characteristic_notify(&current_temperature, current_temperature.value);
            homekit_characteristic_notify(&current_relative_humidity, current_relative_humidity.value);
            
        } else {
            led_code(LED_GPIO, SENSOR_ERROR);
            printf("Couldnt read data from sensor\n");
        }
        vTaskDelay(TEMPERATURE_POLL_PERIOD / portTICK_PERIOD_MS);
    }
}

void temperature_sensor_init() {
    xTaskCreate(temperature_sensor_task, "Temperature", 256, NULL, 2, NULL);
}

static ssize_t  udplogger_stdout_write(struct _reent *r, int fd, const void *ptr, size_t len) {
    
    UDPLOG ("Length %d\n", len);
    UDPLOG (ptr);
    return len;
}

void air_quality_sensor_task(void *_args) {
    
    
    while (1) {
        MQGetReadings( temperature_value, humidity_value);
        printf("Got air quality level: %i\n", air_quality_val);
        
        if (co_val < *carbon_monoxide_level.min_value ){
            co_val = *carbon_monoxide_level.min_value;
        }
        if (co_val > *carbon_monoxide_level.max_value ){
            co_val = *carbon_monoxide_level.max_value;
        }
        carbon_monoxide_level.value.float_value = co_val;
        
        if (pm10_val < *pm10_density.min_value ){
            pm10_val = *pm10_density.min_value;
        }
        if (pm10_val > *pm10_density.max_value ){
            pm10_val = *pm10_density.max_value;
        }
        pm10_density.value.float_value = pm10_val;
        
        
        if (lpg_val < *lpg_level.min_value ){
            lpg_val = *lpg_level.min_value;
        }
        if (lpg_val > *lpg_level.max_value ){
            lpg_val = *lpg_level.max_value;
        }
        lpg_level.value.float_value = lpg_val;
        
        if (methane_val < *methane_level.min_value ){
            methane_val = *methane_level.min_value;
        }
        if (methane_val > *methane_level.max_value ){
            methane_val = *methane_level.max_value;
        }
        methane_level.value.float_value = methane_val;
        
        if (nh4_val < *ammonium_level.min_value ){
            nh4_val = *ammonium_level.min_value;
        }
        if (nh4_val > *ammonium_level.max_value ){
            nh4_val = *ammonium_level.max_value;
        }
        ammonium_level.value.float_value = nh4_val;
        
        air_quality.value.int_value = air_quality_val;
        homekit_characteristic_notify(&carbon_monoxide_level, HOMEKIT_FLOAT(co_val));
        homekit_characteristic_notify(&pm10_density, HOMEKIT_FLOAT(pm10_val));
        homekit_characteristic_notify(&air_quality, air_quality.value);
        homekit_characteristic_notify(&carbon_monoxide_level, HOMEKIT_FLOAT(lpg_val));
        homekit_characteristic_notify(&carbon_monoxide_level, HOMEKIT_FLOAT(lpg_val));
        homekit_characteristic_notify(&carbon_monoxide_level, HOMEKIT_FLOAT(lpg_val));
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

void air_quality_sensor_init() {
    led_code (LED_GPIO,FUNCTION_D );
    MQInit();
    xTaskCreate(air_quality_sensor_task, "Air Quality Sensor", 512, NULL, 2, NULL);
}

void accessory_init(){
    gpio_enable(LED_GPIO, GPIO_OUTPUT);
    gpio_write(LED_GPIO, led_off_value);
    air_quality_sensor_init();
    temperature_sensor_init();
}

void on_homekit_event(homekit_event_t event) {
    if (event == HOMEKIT_EVENT_PAIRING_ADDED) {
        printf("Pairing added so init accessory\n");
        accessory_init();
    } else if (event == HOMEKIT_EVENT_PAIRING_REMOVED) {
        if (!homekit_is_paired())
            sdk_system_restart();
    }
}

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111",
    .on_event = on_homekit_event
};

void user_init(void) {
    
    uart_set_baud(0, 115200);
    xTaskCreate(udplog_send, "logsend", 512, NULL, 4, NULL);
    
    /*   UDPLOG ("hello world\n\n\n");
     
     set_write_stdout(udplogger_stdout_write);
     
     printf ("hello world 2\n");
     printf ("hello world 3\n");
     printf ("hello world 4\n");
     printf ("hello world 5\n");
     printf ("hello world 6\n");
     printf ("hello world 7\n");
     printf ("hello world 8\n");
     printf ("hello world 9\n");
     
     set_write_stdout(NULL);
     */
    
    
    
    create_accessory_name();
    
    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
                                 &model.value.string_value,&revision.value.string_value);
    if (c_hash==0) c_hash=1;
    config.accessories[0]->config_number=c_hash;
    
    if (button_create(button_gpio, button_callback)) {
        printf("Failed to initialize button\n");
    }
    
    if (homekit_is_paired()) {
        printf("Pairing in place so init accessory\n");
        accessory_init();
    }
    
    homekit_server_init(&config);
}

