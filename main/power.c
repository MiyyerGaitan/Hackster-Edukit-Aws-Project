/*
 * AWS IoT EduKit - Core2 for AWS IoT EduKit
 * Factory Firmware v2.1.0
 * power.c
 * 
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "core2forAWS.h"

#include "power.h"
#include "Image.c"

static void led_event_handler(lv_obj_t* obj, lv_event_t event);
static void vibration_event_handler(lv_obj_t* obj, lv_event_t event);
static void brightness_event_handler(lv_obj_t* slider, lv_event_t event);

static const char* TAG = POWER_TAB_NAME;

lv_obj_t* power_tab;
TaskHandle_t power_handle;

void display_power_tab(lv_obj_t* tv, lv_obj_t* core2forAWS_screen_obj){
    xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);

    power_tab = lv_tabview_add_tab(tv, POWER_TAB_NAME);  // Create a tab
    lv_obj_t* aws_img_obj = lv_img_create(power_tab, NULL);   // Creates an LVGL image object and assigns it as a child of the opener_scr parent screen.    
    lv_img_set_src(aws_img_obj, &Image);  // Sets the image object with the image data from the powered_by_aws_logo file which contains hex pixel matrix of the image.
    lv_obj_align(aws_img_obj, NULL, LV_ALIGN_CENTER, 0, 0); // Aligns the image object to the center of the parent screen.
    xSemaphoreGive(xGuiSemaphore);
    xTaskCreatePinnedToCore(battery_task, "batteryTask", configMINIMAL_STACK_SIZE * 2, (void*) core2forAWS_screen_obj, 0, &power_handle, 1);
}

static void brightness_event_handler(lv_obj_t* obj, lv_event_t event){
    if(event == LV_EVENT_VALUE_CHANGED) {
        uint8_t value = lv_btn_get_state(obj);

        if(value == 0)
            Core2ForAWS_Display_SetBrightness(40);
        else
            Core2ForAWS_Display_SetBrightness(80);
        
        ESP_LOGI(TAG, "Screen brightness: %x", value);
    }
}

static void led_event_handler(lv_obj_t* obj, lv_event_t event){
    if(event == LV_EVENT_VALUE_CHANGED) {
        uint8_t value = lv_btn_get_state(obj);

        Core2ForAWS_LED_Enable(value);
        ESP_LOGI(TAG, "LED state: %x", value);
    }
}

static void vibration_event_handler(lv_obj_t* obj, lv_event_t event){
    if(event == LV_EVENT_VALUE_CHANGED) {
        uint8_t value = lv_btn_get_state(obj);

        if(value == 0)
            Core2ForAWS_Motor_SetStrength(0);
        else
            Core2ForAWS_Motor_SetStrength(60);
        
        ESP_LOGI(TAG, "Vibration motor state: %x", value);
    }
}

void battery_task(void* pvParameters){
    xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
    lv_obj_t* battery_label = lv_label_create((lv_obj_t*)pvParameters, NULL);
    lv_label_set_text(battery_label, LV_SYMBOL_BATTERY_FULL);
    lv_label_set_recolor(battery_label, true);
    lv_label_set_align(battery_label, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(battery_label, (lv_obj_t*)pvParameters, LV_ALIGN_IN_TOP_RIGHT, -20, 10);
    lv_obj_t* charge_label = lv_label_create(battery_label, NULL);
    lv_label_set_recolor(charge_label, true);
    lv_label_set_text(charge_label, "");
    lv_obj_align(charge_label, battery_label, LV_ALIGN_CENTER, -4, 0);
    xSemaphoreGive(xGuiSemaphore);

    for(;;){
        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
        float battery_voltage = Core2ForAWS_PMU_GetBatVolt();
        if(battery_voltage >= 4.100){
            lv_label_set_text(battery_label, "#0ab300 " LV_SYMBOL_BATTERY_FULL "#");
        } else if(battery_voltage >= 3.95){
            lv_label_set_text(battery_label, "#0ab300 " LV_SYMBOL_BATTERY_3 "#");
        } else if(battery_voltage >= 3.80){
            lv_label_set_text(battery_label, "#ff9900 " LV_SYMBOL_BATTERY_2 "#");
        } else if(battery_voltage >= 3.25){
            lv_label_set_text(battery_label, "#ff0000 " LV_SYMBOL_BATTERY_1 "#");
        } else{
            lv_label_set_text(battery_label, "#ff0000 " LV_SYMBOL_BATTERY_EMPTY "#");
        }

        if(Core2ForAWS_PMU_GetBatCurrent() >= 0.00){
            lv_label_set_text(charge_label, "#0000cc " LV_SYMBOL_CHARGE "#");
        } else{
            lv_label_set_text(charge_label, "");
        }
        xSemaphoreGive(xGuiSemaphore);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    vTaskDelete(NULL); // Should never get to here...
}