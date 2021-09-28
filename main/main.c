/*
 * AWS IoT EduKit - Core2 for AWS IoT EduKit
 * Cloud Connected Blinky v1.3.0
 * main.c
 * 
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Additions Copyright 2016 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
/**
 * @file main.c
 * @brief simple MQTT publish and subscribe for use with AWS IoT EduKit reference hardware.
 *
 * This example takes the parameters from the build configuration and establishes a connection to AWS IoT Core over MQTT.
 *
 * Some configuration is required. Visit https://edukit.workshop.aws
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"

#include "core2forAWS.h"

#include "home.h"
#include "wifi.h"
#include "blink.h"
#include "power.h"
#include "ui.h"
#include "powered_by_aws_logo.c"

/* The time between each MQTT message publish in milliseconds */
#define PUBLISH_INTERVAL_MS 3000

/* The time prefix used by the logger. */
static const char *TAG = "MAIN";

/* The FreeRTOS task handler for the blink task that can be used to control the task later */
TaskHandle_t xBlink;

/* CA Root certificate */
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");

/* Default MQTT HOST URL is pulled from the aws_iot_config.h */
char HostAddress[255] = "MQTTEndpointValue-ats.iot.us-east-1.amazonaws.com";
char Topic[20] = "mesa1";

/* Default MQTT port is pulled from the aws_iot_config.h */
uint32_t port = AWS_IOT_MQTT_PORT;

static lv_obj_t* tab_view;

void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData) {
    ESP_LOGI(TAG, "Subscribe callback");
    ESP_LOGI(TAG, "%.*s\t%.*s", topicNameLen, topicName, (int) params->payloadLen, (char *)params->payload);
    if (strstr(topicName, "/blink") != NULL) {
        // Get state of the FreeRTOS task, "blinkTask", using it's task handle.
        // Suspend or resume the task depending on the returned task state
        eTaskState blinkState = eTaskGetState(xBlink);
        if (blinkState == eSuspended){
            vTaskResume(xBlink);
        } else{
            vTaskSuspend(xBlink);
        }
    }
}

void disconnect_callback_handler(AWS_IoT_Client *pClient, void *data) {
    ESP_LOGW(TAG, "MQTT Disconnect");
    //ui_textarea_add("Disconnected from AWS IoT Core...", NULL, 0);
    IoT_Error_t rc = FAILURE;

    if(pClient == NULL) {
        return;
    }

    if(aws_iot_is_autoreconnect_enabled(pClient)) {
        ESP_LOGI(TAG, "Auto Reconnect is enabled, Reconnecting attempt will start now");
    } else {
        ESP_LOGW(TAG, "Auto Reconnect not enabled. Starting manual reconnect...");
        rc = aws_iot_mqtt_attempt_reconnect(pClient);
        if(NETWORK_RECONNECTED == rc) {
            ESP_LOGW(TAG, "Manual Reconnect Successful");
        } else {
            ESP_LOGW(TAG, "Manual Reconnect Failed - %d", rc);
        }
    }
}

static void publisher(AWS_IoT_Client *client, char *base_topic, uint16_t base_topic_len){
    char cPayload[100];
    int32_t i = 0;

    IoT_Publish_Message_Params paramsQOS0;
    IoT_Publish_Message_Params paramsQOS1;

    paramsQOS0.qos = QOS0;
    paramsQOS0.payload = (void *) cPayload;
    paramsQOS0.isRetained = 0;

    // Publish and ignore if "ack" was received or  from AWS IoT Core
    sprintf(cPayload, "%s : %d ", "Hello from AWS IoT EduKit (QOS0)", i++);
    paramsQOS0.payloadLen = strlen(cPayload);
    IoT_Error_t rc = aws_iot_mqtt_publish(client, base_topic, base_topic_len, &paramsQOS0);
    if (rc != SUCCESS){
        ESP_LOGE(TAG, "Publish QOS0 error %i", rc);
        rc = SUCCESS;
    }

    paramsQOS1.qos = QOS1;
    paramsQOS1.payload = (void *) cPayload;
    paramsQOS1.isRetained = 0;
    // Publish and check if "ack" was sent from AWS IoT Core
    sprintf(cPayload, "%s : %d ", "Hello from AWS IoT EduKit (QOS1)", i++);
    paramsQOS1.payloadLen = strlen(cPayload);
    rc = aws_iot_mqtt_publish(client, base_topic, base_topic_len, &paramsQOS1);
    if (rc == MQTT_REQUEST_TIMEOUT_ERROR) {
        ESP_LOGW(TAG, "QOS1 publish ack not received.");
        rc = SUCCESS;
    }
}

void aws_iot_task(void *param) {
    IoT_Error_t rc = FAILURE;

    AWS_IoT_Client client;
    IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
    IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    mqttInitParams.enableAutoReconnect = false; // We enable this later below
    mqttInitParams.pHostURL = HostAddress;
    mqttInitParams.port = port;    
    mqttInitParams.pRootCALocation = (const char *)aws_root_ca_pem_start;
    mqttInitParams.pDeviceCertLocation = "#";
    mqttInitParams.pDevicePrivateKeyLocation = "#0";
    
#define CLIENT_ID_LEN (ATCA_SERIAL_NUM_SIZE * 2)
#define SUBSCRIBE_TOPIC_LEN (CLIENT_ID_LEN + 3)
#define BASE_PUBLISH_TOPIC_LEN (CLIENT_ID_LEN + 2)

    char *client_id = malloc(CLIENT_ID_LEN + 1);
    ATCA_STATUS ret = Atecc608_GetSerialString(client_id);
    if (ret != ATCA_SUCCESS)
    {
        printf("Failed to get device serial from secure element. Error: %i", ret);
        abort();
    }

    char subscribe_topic[SUBSCRIBE_TOPIC_LEN];
    char base_publish_topic[BASE_PUBLISH_TOPIC_LEN];
    snprintf(subscribe_topic, SUBSCRIBE_TOPIC_LEN, "%s/#", client_id);
    snprintf(base_publish_topic, BASE_PUBLISH_TOPIC_LEN, "%s/", client_id);

    mqttInitParams.mqttCommandTimeout_ms = 20000;
    mqttInitParams.tlsHandshakeTimeout_ms = 5000;
    mqttInitParams.isSSLHostnameVerify = true;
    mqttInitParams.disconnectHandler = disconnect_callback_handler;
    mqttInitParams.disconnectHandlerData = NULL;

    rc = aws_iot_mqtt_init(&client, &mqttInitParams);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_init returned error : %d ", rc);
        abort();
    }

    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);    

    connectParams.keepAliveIntervalInSec = 10;
    connectParams.isCleanSession = true;
    connectParams.MQTTVersion = MQTT_3_1_1;

    connectParams.pClientID = client_id;
    connectParams.clientIDLen = CLIENT_ID_LEN;
    connectParams.isWillMsgPresent = false;
    //ui_textarea_add("Connecting to AWS IoT Core...\n", NULL, 0);
    ESP_LOGI(TAG, "Connecting to AWS IoT Core at %s:%d", mqttInitParams.pHostURL, mqttInitParams.port);
    do {
        rc = aws_iot_mqtt_connect(&client, &connectParams);
        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while(SUCCESS != rc);
    //ui_textarea_add("Successfully connected!\n", NULL, 0);
    ESP_LOGI(TAG, "Successfully connected to AWS IoT Core!");
    /*
     * Enable Auto Reconnect functionality. Minimum and Maximum time for exponential backoff for retries.
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
    if(SUCCESS != rc) {
        //ui_textarea_add("Unable to set Auto Reconnect to true\n", NULL, 0);
        ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d", rc);
        abort();
    }

    ESP_LOGI(TAG, "Subscribing to '%s'", subscribe_topic);
    rc = aws_iot_mqtt_subscribe(&client, subscribe_topic, strlen(subscribe_topic), QOS0, iot_subscribe_callback_handler, NULL);
    if(SUCCESS != rc) {
        //ui_textarea_add("Error subscribing\n", NULL, 0);
        ESP_LOGE(TAG, "Error subscribing : %d ", rc);
        abort();
    } else{
        //ui_textarea_add("Subscribed to topic: %s\n\n", subscribe_topic, SUBSCRIBE_TOPIC_LEN) ;
        ESP_LOGI(TAG, "Subscribed to topic '%s'", subscribe_topic);
    }
    
    ESP_LOGI(TAG, "\n****************************************\n*  AWS client Id - %s  *\n****************************************\n\n",
             client_id);
    
    //ui_textarea_add("Attempting publish to: %s\n", base_publish_topic, BASE_PUBLISH_TOPIC_LEN) ;
    while((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)) {

        //Max time the yield function will wait for read messages
        rc = aws_iot_mqtt_yield(&client, 100);
        if(NETWORK_ATTEMPTING_RECONNECT == rc) {
            // If the client is attempting to reconnect we will skip the rest of the loop.
            continue;
        }

        ESP_LOGD(TAG, "Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
        
        //publisher(&client, base_publish_topic, BASE_PUBLISH_TOPIC_LEN);
    }

    ESP_LOGE(TAG, "An error occurred in the main loop.");
    abort();
}

static void tab_event_cb(lv_obj_t* slider, lv_event_t event){
    if(event == LV_EVENT_VALUE_CHANGED) {
        lv_tabview_ext_t* ext = (lv_tabview_ext_t*) lv_obj_get_ext_attr(tab_view);
        const char* tab_name = ext->tab_name_ptr[lv_tabview_get_tab_act(tab_view)];
        ESP_LOGI(TAG, "Current Active Tab: %s\n", tab_name);

        //vTaskSuspend(MPU_handle);
        
        //if(strcmp(tab_name, MPU_TAB_NAME) == 0)
            //vTaskResume(MPU_handle);
    }
}

static void ui_start(void){
    /* Displays the Powered by AWS logo */
    xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);   // Takes (blocks) the xGuiSemaphore mutex from being read/written by another task.
    lv_obj_t* opener_scr = lv_scr_act();   // Create a new LVGL "screen". Screens can be though of as a window.
    lv_obj_t* aws_img_obj = lv_img_create(opener_scr, NULL);   // Creates an LVGL image object and assigns it as a child of the opener_scr parent screen.
    lv_img_set_src(aws_img_obj, &powered_by_aws_logo);  // Sets the image object with the image data from the powered_by_aws_logo file which contains hex pixel matrix of the image.
    lv_obj_align(aws_img_obj, NULL, LV_ALIGN_CENTER, 0, 0); // Aligns the image object to the center of the parent screen.
    lv_obj_set_style_local_bg_color(opener_scr, LV_OBJ_PART_MAIN, 0, LV_COLOR_WHITE);   // Sets the background color of the screen to white.
    xSemaphoreGive(xGuiSemaphore);  // Frees the xGuiSemaphore so that another task can use it. In this case, the higher priority guiTask will take it and then read the values to then display.

    vTaskDelay(pdMS_TO_TICKS(10000)); // FreeRTOS scheduler block execution for 1.5 seconds to keep showing the Powered by AWS logo.
    lv_obj_clean(opener_scr);   // Clear the aws_img_obj and remove from memory space. Currently no objects exist on the screen.

    xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);   // Takes (blocks) the xGuiSemaphore mutex from being read/written by another task.
    lv_obj_t* core2forAWS_obj = lv_obj_create(NULL, NULL); // Create a object to draw all with no parent 
    lv_scr_load_anim(core2forAWS_obj, LV_SCR_LOAD_ANIM_MOVE_LEFT, 400, 0, false);   // Animates the loading of core2forAWS_obj as a slide into view from the left
    tab_view = lv_tabview_create(core2forAWS_obj, NULL); // Creates the tab view to display different tabs with different hardware features
    lv_obj_set_event_cb(tab_view, tab_event_cb); // Add a callback for whenever there is an event triggered on the tab_view object (e.g. a left-to-right swipe)    
    lv_tabview_set_btns_pos(tab_view, LV_TABVIEW_TAB_POS_NONE);  // Hide the tab buttons so it looks like a clean screen
    
    xSemaphoreGive(xGuiSemaphore);  // Frees the xGuiSemaphore so that another task can use it. In this case, the higher priority guiTask will take it and then read the values to then display.

    /*
    Below creates all the display layers for the various peripheral tabs. Some of the tabs also starts the concurrent FreeRTOS tasks 
    that read/write to the peripheral registers and displays the data from that peripheral.
    */
    display_home_tab(tab_view);
    display_power_tab(tab_view, core2forAWS_obj);    
}

void app_main()
{
    Core2ForAWS_Init();
    Core2ForAWS_Display_SetBrightness(50);
    
    ui_start();

    //ui_init();
    initialise_wifi();

    xTaskCreatePinnedToCore(&aws_iot_task, "aws_iot_task", 4096 * 2, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(&blink_task, "blink_task", 4096 * 1, NULL, 2, &xBlink, 1);
}