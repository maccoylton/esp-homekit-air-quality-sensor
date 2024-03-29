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
#define TEMPERATURE_SENSOR_GPIO 5
#define TEMPERATURE_POLL_PERIOD 10000

#include <stdio.h>
#include <math.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <sysparam.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <esp8266_mq135.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <dht/dht.h>

#include <stdout_redirect.h>
#include <led_codes.h>
#include <custom_characteristics.h>
#include <wifi_config.h>
#include <adv_button.h>
#include <udplogger.h>
#include <shared_functions.h>


// add this section to make your device OTA capable
// create the extra characteristic &ota_trigger, at the end of the primary service (before the NULL)
// it can be used in Eve, which will show it, where Home does not
// and apply the four other parameters in the accessories_information section

#include <ota-api.h>

const int LED_GPIO = 13;
const int RESET_BUTTON_GPIO = 0;
const int status_led_gpio = 13;

/* global varibale to support LEDs set to 0 wehre the LED is connected to GND, 1 where +3.3v */
int led_off_value=1;



homekit_characteristic_t wifi_reset   = HOMEKIT_CHARACTERISTIC_(CUSTOM_WIFI_RESET, false, .setter=wifi_reset_set);
homekit_characteristic_t wifi_check_interval   = HOMEKIT_CHARACTERISTIC_(CUSTOM_WIFI_CHECK_INTERVAL, 10, .setter=wifi_check_interval_set);
/* checks the wifi is connected and flashes status led to indicated connected */

homekit_characteristic_t task_stats   = HOMEKIT_CHARACTERISTIC_(CUSTOM_TASK_STATS, false , .setter=task_stats_set);

homekit_characteristic_t ota_beta     = HOMEKIT_CHARACTERISTIC_(CUSTOM_OTA_BETA, false, .setter=ota_beta_set);
homekit_characteristic_t lcm_beta    = HOMEKIT_CHARACTERISTIC_(CUSTOM_LCM_BETA, false, .setter=lcm_beta_set);
homekit_characteristic_t preserve_state   = HOMEKIT_CHARACTERISTIC_(CUSTOM_PRESERVE_STATE, false, .setter=preserve_state_set);

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
TaskHandle_t temperature_sensor_task_handle, air_quality_sensor_task_handle;

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
            &wifi_reset,
            &wifi_check_interval,
            &task_stats,
            &ota_beta,
            &lcm_beta,
            NULL
        }),
        NULL
    }),
    NULL
};

void temperature_sensor_task(void *_args) {
    
    bool success;
    
    /*gpio_set_pullup(TEMPERATURE_SENSOR_GPIO, false, false); */
    
    while (1) {
        success = dht_read_float_data(
                                      DHT_TYPE_DHT22, TEMPERATURE_SENSOR_GPIO,
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
            printf("Couldnt read data from temperate & humidity sensor\n");
        }
        vTaskDelay(TEMPERATURE_POLL_PERIOD / portTICK_PERIOD_MS);
    }
}

void temperature_sensor_init() {
    gpio_enable(TEMPERATURE_SENSOR_GPIO, GPIO_INPUT);
    xTaskCreate(temperature_sensor_task, "Temperature", 256, NULL, 2, &temperature_sensor_task_handle);
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
        homekit_characteristic_notify(&lpg_level, HOMEKIT_FLOAT(lpg_val));
        homekit_characteristic_notify(&methane_level, HOMEKIT_FLOAT(methane_val));
        homekit_characteristic_notify(&ammonium_level, HOMEKIT_FLOAT(nh4_val));
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

void air_quality_sensor_init_task (void *_args) {
    MQInit();
    xTaskCreate(air_quality_sensor_task, "Air Quality Sensor", 300, NULL, 2, &air_quality_sensor_task_handle);
    vTaskDelete (NULL);
}


void air_quality_sensor_init() {
    led_code (LED_GPIO,FUNCTION_D );
    xTaskCreate(air_quality_sensor_init_task, "Air Quality init", 512, NULL, 2, NULL);
}


void gpio_init (){
    
    
    printf("%s: Start, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());
    
    adv_button_set_evaluate_delay(10);
    
    /* GPIO for button, pull-up resistor, inverted */
    printf("Initialising reset buttons\n");
    
    adv_button_create(RESET_BUTTON_GPIO, true, false);
    adv_button_register_callback_fn(RESET_BUTTON_GPIO, reset_button_callback, VERYLONGPRESS_TYPE, NULL, 0);
    
    
    printf("Initialising led\n");
    gpio_enable(LED_GPIO, GPIO_OUTPUT);
    gpio_write(LED_GPIO, led_off_value);
    
    printf("%s: End, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());
}


void accessory_init(){
    /* initalise anything you don't want started until wifi and pairing is confirmed */
    printf("%s: Start, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());

    gpio_init();
    air_quality_sensor_init();
    temperature_sensor_init();

    printf("%s: End, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());
}


void accessory_init_not_paired (void) {
    /* initalise anything you don't want started until wifi and homekit imitialisation is confirmed, but not paired */
}


void recover_from_reset (int reason){
    /* called if we restarted abnormally */
    printf ("%s: reason %d\n", __func__, reason);
}


void save_characteristics (){
    
    /* called if we restarted abnormally */
    printf ("%s:\n", __func__);
    save_characteristic_to_flash(&wifi_check_interval, wifi_check_interval.value);
}


homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111",
    .setupId = "1234",
    .on_event = on_homekit_event
};


void user_init(void) {

    printf ("User Init\n");
    
    standard_init (&name, &manufacturer, &model, &serial, &revision);
    
    wifi_config_init(DEVICE_NAME, NULL, on_wifi_ready);
}

