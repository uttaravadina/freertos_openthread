/**
 * Copyright (c) 2017 - 2018, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/** @file
 *
 * @defgroup freertos_coap_server_example_main main.c
 * @{
 * @ingroup freertos_coap_server_example
 *
 * @brief Thread CoAP server example with FreeRTOS Application main file.
 *
 * This file contains the source code for a sample application using Thread CoAP server and FreeRTOS.
 *
 */


/* button interrupt */
#include "nrf.h"
#include "nrf_drv_gpiote.h"
#include "app_error.h"
#include "boards.h"

#ifdef BSP_BUTTON_0
    #define PIN_IN BSP_BUTTON_0
#endif
#ifndef PIN_IN
    #error "Please indicate input pin"
#endif

#ifdef BSP_LED_0
    #define PIN_OUT BSP_LED_0
#endif
#ifndef PIN_OUT
    #error "Please indicate output pin"
#endif


 #define NRF_LOG_ENABLED 1
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app_scheduler.h"

#include "FreeRTOS.h"
#include "nrf_drv_clock.h"
#include "task.h"

#define NRF_LOG_MODULE_NAME APP
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#include "mqttsn_client.h"

#include "app_timer.h"
#include "bsp_thread.h"
#include "thread_utils.h"

#include <openthread/instance.h>
#include <openthread/thread.h>

#define SCHED_QUEUE_SIZE       32                                           /**< Maximum number of events in the scheduler queue. */
#define SCHED_EVENT_DATA_SIZE  APP_TIMER_SCHED_EVENT_DATA_SIZE              /**< Maximum app_scheduler event size. */


#define THREAD_STACK_TASK_STACK_SIZE     (( 1024 * 8 ) / sizeof(StackType_t))   /**< FreeRTOS task stack size is determined in multiples of StackType_t. */
#define LOG_TASK_STACK_SIZE              ( 1024 / sizeof(StackType_t))          /**< FreeRTOS task stack size is determined in multiples of StackType_t. */
#define THREAD_STACK_TASK_PRIORITY       2
#define LOG_TASK_PRIORITY                1
#define LED1_TASK_PRIORITY               1
#define LED2_TASK_PRIORITY               1
#define LED1_BLINK_INTERVAL              427
#define LED2_BLINK_INTERVAL              472
#define SEARCH_GATEWAY_TIMEOUT 5                                            /**< MQTT-SN Gateway discovery procedure timeout in [s]. */


typedef struct
{
    TaskHandle_t     thread_stack_task;     /**< Thread stack task handle */
    TaskHandle_t     logger_task;           /**< Definition of Logger task. */
    TaskHandle_t     led1_task;             /**< LED1 task handle*/
    TaskHandle_t     led2_task;             /**< LED2 task handle*/
} application_t;

application_t m_app =
{
    .thread_stack_task = NULL,
    .logger_task       = NULL,
    .led1_task         = NULL,
    .led2_task         = NULL,
};

#if LEDS_NUMBER <= 2
#error "Board is not equipped with enough amount of LEDs"
#endif

#define TASK_DELAY        200           /**< Task delay. Delays a LED0 task for 200 ms */

TaskHandle_t  led_toggle_task_handle;   /**< Reference to LED0 toggling FreeRTOS task. */



#define MQTT_SUB "v1/sub"
#define MQTT_PUB "v1/pub"
#define MQTT_ID MQTT_SUB "-id"


static mqttsn_client_t      m_client;                          /**< An MQTT-SN client instance. */
static mqttsn_remote_t      m_gateway_addr;                    /**< A gateway address. */
static uint8_t              m_gateway_id;                      /**< A gateway ID. */
static mqttsn_connect_opt_t m_connect_opt;                     /**< Connect options for the MQTT-SN client. */
static uint16_t             m_msg_id_sub           = 0;        /**< Message ID thrown with MQTTSN_EVENT_TIMEOUT. */
static uint16_t             m_msg_id_pub           = 0;        /**< Message ID thrown with MQTTSN_EVENT_TIMEOUT. */

static bool                 m_subscribed       = 0;            /**< Current subscription state. */
static char                 m_client_id[]    =  MQTT_ID;      /**< The MQTT-SN Client's ID. */


#define MQTT_SUB "v1/sub"
#define MQTT_PUB "v1/pub"
#define MESSAGE_LENGTH 12                                      /**< Length of message. Max 255>*/

static char                 m_topic_pub_name[]   =  MQTT_PUB;  /**< Name of the topic to publish to. */
static mqttsn_topic_t       m_topic_pub            =           /**< Topic corresponding to publisher>*/
{
    .p_topic_name = (unsigned char *)m_topic_pub_name,
    .topic_id     = 0,
};

static char                 m_topic_sub_name[] = MQTT_SUB;     /**< Name of the topic to subscribe to. */
static mqttsn_topic_t       m_topic_sub        =               
{
    .p_topic_name = (unsigned char *)m_topic_sub_name,
    .topic_id     = 0,
};


/*
    Button interrupt
*/

 
uint8_t tx_message[MESSAGE_LENGTH] = "publish msg"; 
void in_pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{

    nrf_drv_gpiote_out_toggle(PIN_OUT);
    uint32_t ec = mqttsn_client_publish(&m_client, m_topic_pub.topic_id, tx_message, MESSAGE_LENGTH, &m_msg_id_pub);
    if (ec != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("PUBLISH message could not be sent. Error code: 0x%x\r\n", ec)
    }
}
/**
 * @brief Function for configuring: PIN_IN pin for input, PIN_OUT pin for output,
 * and configures GPIOTE to give an interrupt on pin change.
 */
static void gpio_init(void)
{
    ret_code_t err_code;

    err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);

    nrf_drv_gpiote_out_config_t out_config = GPIOTE_CONFIG_OUT_SIMPLE(false);

    err_code = nrf_drv_gpiote_out_init(PIN_OUT, &out_config);
    APP_ERROR_CHECK(err_code);

    nrf_drv_gpiote_in_config_t in_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
    in_config.pull = NRF_GPIO_PIN_PULLUP;

    err_code = nrf_drv_gpiote_in_init(PIN_IN, &in_config, in_pin_handler);
    APP_ERROR_CHECK(err_code);

    nrf_drv_gpiote_in_event_enable(PIN_IN, true);
}


/***************************************************************************************************
 * @section CoAP
 **************************************************************************************************/

static inline void light_on(void)
{
    vTaskResume(m_app.led1_task);
    vTaskResume(m_app.led2_task);
}

static inline void light_off(void)
{
    vTaskSuspend(m_app.led1_task);
    LEDS_OFF(BSP_LED_2_MASK);

    vTaskSuspend(m_app.led2_task);
    LEDS_OFF(BSP_LED_3_MASK);
}


/***************************************************************************************************
 * @section Signal handling
 **************************************************************************************************/

void otTaskletsSignalPending(otInstance * p_instance)
{
    BaseType_t var = xTaskNotifyGive(m_app.thread_stack_task);
    UNUSED_VARIABLE(var);
}

void otSysEventSignalPending(void)
{
    static BaseType_t xHigherPriorityTaskWoken;

    vTaskNotifyGiveFromISR(m_app.thread_stack_task, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}


/***************************************************************************************************
 * @section State change handling
 **************************************************************************************************/

static void state_changed_callback(uint32_t flags, void * p_context)
{
    NRF_LOG_INFO("State changed! Flags: 0x%08x Current role: %d\r\n",
                 flags, otThreadGetDeviceRole(p_context));
}


/**@brief Initializes MQTT-SN client's connection options. */
static void connect_opt_init(void)
{
    m_connect_opt.alive_duration = MQTTSN_DEFAULT_ALIVE_DURATION,
    m_connect_opt.clean_session  = MQTTSN_DEFAULT_CLEAN_SESSION_FLAG,
    m_connect_opt.will_flag      = MQTTSN_DEFAULT_WILL_FLAG,
    m_connect_opt.client_id_len  = strlen(m_client_id),

    memcpy(m_connect_opt.p_client_id,  (unsigned char *)m_client_id,  m_connect_opt.client_id_len);
}


/**@brief Processes GWINFO message from a gateway.
 *
 * @details This function updates MQTT-SN Gateway information.
 *
 * @param[in]    p_event  Pointer to MQTT-SN event.
 */
static void gateway_info_callback(mqttsn_event_t * p_event)
{
    m_gateway_addr  = *(p_event->event_data.connected.p_gateway_addr);
    m_gateway_id    = p_event->event_data.connected.gateway_id;
}


static void subscribe(void)
{
    uint8_t  topic_sub_name_len = strlen(m_topic_sub_name);
    uint32_t err_code       = NRF_SUCCESS;

    if (m_subscribed)
    {
        err_code = mqttsn_client_unsubscribe(&m_client, m_topic_sub.p_topic_name, topic_sub_name_len, &m_msg_id_sub);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("UNSUBSCRIBE message could not be sent.\r\n");
        }
        else
        {
            m_subscribed = false;
        }
    }
    else
    {
        err_code = mqttsn_client_subscribe(&m_client, m_topic_sub.p_topic_name, topic_sub_name_len, &m_msg_id_sub);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("SUBSCRIBE message could not be sent.\r\n");
        }
        else
        {
            m_subscribed = true;
        }
    }
}

/**@brief Processes CONNACK message from a gateway.
 *
 * @details This function launches the topic registration procedure if necessary.
 */
static void connected_callback(void)
{
    light_on();

    uint32_t err_code = mqttsn_client_topic_register(&m_client,
                                                     m_topic_pub.p_topic_name,
                                                     strlen(m_topic_pub_name),
                                                     &m_msg_id_pub);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("REGISTER message could not be sent. Error code: 0x%x\r\n", err_code);
    }

    subscribe();
}


/**@brief Processes DISCONNECT message from a gateway. */
static void disconnected_callback(void)
{
    light_off();
}


/**@brief Processes REGACK message from a gateway.
 *
 * @param[in] p_event Pointer to MQTT-SN event.
 */
static void regack_callback(mqttsn_event_t * p_event)
{
    m_topic_pub.topic_id = p_event->event_data.registered.packet.topic.topic_id;
    NRF_LOG_INFO("MQTT-SN event: Topic has been registered with ID: %d.\r\n",
                 p_event->event_data.registered.packet.topic.topic_id);
}


static void received_callback(mqttsn_event_t * p_event)
{
        char my_string[MESSAGE_LENGTH+1];
        NRF_LOG_INFO("MQTT-SN event: Content to subscribed topic received.\r\n");

        for(int i = 0; i < MESSAGE_LENGTH; i++)
        {
            my_string[i] = p_event->event_data.published.p_payload[i];
        }
        my_string[MESSAGE_LENGTH] = 0; 
        NRF_LOG_INFO("message: %s", my_string);
}


/**@brief Processes retransmission limit reached event. */
static void timeout_callback(mqttsn_event_t * p_event)
{
    NRF_LOG_INFO("MQTT-SN event: Timed-out message: %d. Message ID: %d.\r\n",
                  p_event->event_data.error.msg_type,
                  p_event->event_data.error.msg_id);
}


/**@brief Processes results of gateway discovery procedure. */
static void searchgw_timeout_callback(mqttsn_event_t * p_event)
{
    NRF_LOG_INFO("MQTT-SN event: Gateway discovery result: 0x%x.\r\n", p_event->event_data.discovery);
}


/**@brief Function for handling MQTT-SN events. */
void mqttsn_evt_handler(mqttsn_client_t * p_client, mqttsn_event_t * p_event)
{
    switch(p_event->event_id)
    {
        case MQTTSN_EVENT_GATEWAY_FOUND:
            NRF_LOG_INFO("MQTT-SN event: Client has found an active gateway.\r\n");
            gateway_info_callback(p_event);
            break;

        case MQTTSN_EVENT_CONNECTED:
            NRF_LOG_INFO("MQTT-SN event: Client connected.\r\n");
            connected_callback();
            break;

        case MQTTSN_EVENT_DISCONNECT_PERMIT:
            NRF_LOG_INFO("MQTT-SN event: Client disconnected.\r\n");
            disconnected_callback();
            break;

        case MQTTSN_EVENT_REGISTERED:
            NRF_LOG_INFO("MQTT-SN event: Client registered topic.\r\n");
            regack_callback(p_event);
            break;

        case MQTTSN_EVENT_PUBLISHED:
            NRF_LOG_INFO("MQTT-SN event: Client has successfully published content.\r\n");
            break;

        case MQTTSN_EVENT_SUBSCRIBED:
            NRF_LOG_INFO("MQTT-SN event: Client subscribed to topic.\r\n");
            LEDS_ON(BSP_LED_3_MASK);
            break;

        case MQTTSN_EVENT_UNSUBSCRIBED:
            NRF_LOG_INFO("MQTT-SN event: Client unsubscribed to topic.\r\n");
            break;

        case MQTTSN_EVENT_RECEIVED:
            NRF_LOG_INFO("MQTT-SN event: Client received content.\r\n");
            received_callback(p_event);
            break;

        case MQTTSN_EVENT_TIMEOUT:
            NRF_LOG_INFO("MQTT-SN event: Retransmission retries limit has been reached.\r\n");
            timeout_callback(p_event);
            break;

        case MQTTSN_EVENT_SEARCHGW_TIMEOUT:
            NRF_LOG_INFO("MQTT-SN event: Gateway discovery procedure has finished.\r\n");
            searchgw_timeout_callback(p_event);

            // topic registered

                uint32_t err_code;

            if (mqttsn_client_state_get(&m_client) == MQTTSN_CLIENT_CONNECTED)
            {
                err_code = mqttsn_client_disconnect(&m_client);
                if (err_code != NRF_SUCCESS)
                {
                    NRF_LOG_INFO("DISCONNECT message could not be sent. Error: 0x%x\r\n", err_code);
                }
            }
            else
            {
                err_code = mqttsn_client_connect(&m_client, &m_gateway_addr, m_gateway_id, &m_connect_opt);
                if (err_code != NRF_SUCCESS)
                {
                    NRF_LOG_ERROR("CONNECT message could not be sent. Error: 0x%x\r\n", err_code);
                }
            }

            break;

        default:
            break;
    }
}



/***************************************************************************************************
 * @section State
 **************************************************************************************************/




static void timer_init(void)
{
    uint32_t error_code = app_timer_init();
    APP_ERROR_CHECK(error_code);
}




/**@brief Function for initializing the Thread Stack
 */
static void thread_instance_init(void)
{
    thread_configuration_t thread_configuration =
    {
        .role              = RX_ON_WHEN_IDLE,
        .autocommissioning = true,
    };

    thread_init(&thread_configuration);
    thread_cli_init();
    thread_state_changed_callback_set(state_changed_callback);
}



/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}


/**@brief Function for initializing the clock.
 */
static void clock_init(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
}




#if NRF_LOG_ENABLED
/**@brief Task for handling the logger.
 *
 * @details This task is responsible for processing log entries if logs are deferred.
 *          Task flushes all log entries and suspends. It is resumed by idle task hook.
 *
 * @param[in]   arg   Pointer used for passing some arbitrary information (context) from the
 *                    osThreadCreate() call to the task.
 */
static void logger_task(void * arg)
{
    UNUSED_PARAMETER(arg);

    while (1)
    {
        NRF_LOG_FLUSH();

        // Suspend logger task.
        vTaskSuspend(NULL);
    }
}
#endif //NRF_LOG_ENABLED


/**@brief LED0 task entry function.
 *
 * @param[in] pvParameter   Pointer that will be used as the parameter for the task.
 */


void vApplicationIdleHook(void) {
#if NRF_LOG_ENABLED
    // vTaskResume(m_logger_thread);
#endif
}



/***************************************************************************************************
 * @section Main
 **************************************************************************************************/
/**@brief Function for initializing scheduler module.
 */ 


static void mqttsn_init(void)
{
    uint32_t err_code = mqttsn_client_init(&m_client,
                                           MQTTSN_DEFAULT_CLIENT_PORT,
                                           mqttsn_evt_handler,
                                           thread_ot_instance_get());
    APP_ERROR_CHECK(err_code);

    NRF_LOG_INFO("MQTTS inited, error code: %d", err_code);

    connect_opt_init();
}

static void scheduler_init(void)
{
    APP_SCHED_INIT(SCHED_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
}


/* Tasks */ 




static void thread_stack_task(void * arg)
{
    UNUSED_PARAMETER(arg);

    
    mqttsn_init();

    uint32_t err_code = mqttsn_client_search_gateway(&m_client, SEARCH_GATEWAY_TIMEOUT);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_INFO("SEARCH GATEWAY message could not be sent. Error: 0x%x\r\n", err_code);
    }else{
        NRF_LOG_INFO("Search gateway message sendt");
    }

    while (1)
    {   

        thread_process();
        app_sched_execute();
        if (NRF_LOG_PROCESS() == false)
        {
            thread_sleep();
        }

        
        UNUSED_VARIABLE(ulTaskNotifyTake(pdTRUE, portMAX_DELAY));
    }
}

void initialize_system(){
    log_init();
    scheduler_init();
    clock_init();
    timer_init();
    bsp_board_init(BSP_INIT_LEDS);
    gpio_init();
    thread_instance_init(); 

}

int main(void)
{
    initialize_system();
    
    // Start thread stack execution.
    if (pdPASS != xTaskCreate(thread_stack_task, "THR", THREAD_STACK_TASK_STACK_SIZE, NULL, 2, &m_app.thread_stack_task))
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
    
    #if NRF_LOG_ENABLED
      

    // Start execution.
    if (pdPASS != xTaskCreate(logger_task, "LOG", LOG_TASK_STACK_SIZE, NULL, 1, &m_app.logger_task))
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
    #endif //NRF_LOG_ENABLED

    /* Start FreeRTOS scheduler. */
    vTaskStartScheduler();

    


    while (true)
    {
        /* FreeRTOS should not be here... FreeRTOS goes back to the start of stack
         * in vTaskStartScheduler function. */
    }
}


/**
 *@}
 **/
