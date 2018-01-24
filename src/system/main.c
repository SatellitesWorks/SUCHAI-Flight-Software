/*                                 SUCHAI
 *                      NANOSATELLITE FLIGHT SOFTWARE
 *
 *      Copyright 2017, Carlos Gonzalez Cortes, carlgonz@ug.uchile.cl
 *      Copyright 2017, Tomas Opazo Toro, tomas.opazo.t@gmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "main.h"

static void on_reset(void);
static void on_close(int signal);
const char *tag = "main";
#ifdef AVR32
volatile avr32_rtc_t *rtc;
#endif

__attribute__((__interrupt__)) void rtc_irq(void)
{
    //Checking the interrupt execution
    LED_Toggle(LED2);
    // Increment the minutes counter
    dat_update_time();
    // clear the interrupt flag
    rtc_clear_interrupt(&AVR32_RTC);

    // specify that an interrupt has been raised
    //print_sec = 1;
}


#ifdef ESP32
void app_main()
#else
int main(void)
#endif
{
    /* On reset */
    on_reset();

    LOGI(tag, "Creating tasks...");

    /* Initializing shared Queues */
    dispatcher_queue = osQueueCreate(25,sizeof(cmd_t *));
    executer_cmd_queue = osQueueCreate(1,sizeof(cmd_t *));
    executer_stat_queue = osQueueCreate(1,sizeof(int));

    int n_threads = 7;

    os_thread threads_id[n_threads];

    /* Crating system task (the others are created inside taskDeployment) */
    osCreateTask(taskDispatcher,"dispatcher", 15*256, NULL, 3, &threads_id[0]);
    osCreateTask(taskExecuter, "executer", 15*256, NULL, 4, &threads_id[1]);

#if SCH_RUN_TESTS
    osCreateTask(taskTest, "test", 15*256, "TEST1", 2, &threads_id[2]);
#endif

    /* Creating monitors tasks */
    osCreateTask(taskConsole, "console", 15*256, NULL, 2, &threads_id[3]);

#if SCH_HK_ENABLED
    osCreateTask(taskHousekeeping, "housekeeping", 15*256, NULL, 2, &threads_id[4]);
#endif

#if SCH_COMM_ENABLE
    osCreateTask(taskCommunications, "comm", 15*256, NULL,2, &threads_id[5]);
#endif

#if SCH_FP_ENABLED
    osCreateTask(taskFlightPlan,"flightplan",15*256,NULL,2,&threads_id[6]);
#endif

#ifndef ESP32
    /* Start the scheduler. Should never return */
    osScheduler(threads_id, n_threads);
    return 0;
#endif

}

#ifdef FREERTOS
/**
 * Task idle handle function. Performs operations inside the idle task
 * configUSE_IDLE_HOOK must be set to 1
 */
void vApplicationIdleHook(void)
{
    //Add hook code here
}

/**
 * Task idle handle function. Performs operations inside the idle task
 * configUSE_TICK_HOOK must be set to 1
 */
void vApplicationTickHook(void)
{
#ifdef AVR32
    LED_Toggle(LED0);
#endif
}

/**
 * Stack overflow handle function.
 * configCHECK_FOR_STACK_OVERFLOW must be set to 1 or 2
 *
 * @param pxTask Task handle
 * @param pcTaskName Task name
 */
void vApplicationStackOverflowHook(xTaskHandle* pxTask, signed char* pcTaskName)
{
    printf("[ERROR][-1][%s] Stak overflow!", (char *)pcTaskName);

    /* Stack overflow handle */
    while(1);
}
#endif

/**
 * Performs initialization actions
 */
void on_reset(void)
{
#ifdef LINUX
    /* Register INT/TERM signals */
    struct sigaction act;
    act.sa_handler = on_close;
    sigaction(SIGINT, &act, NULL);  // Register CTR+C signal handler
    sigaction(SIGTERM, &act, NULL);
#endif

#ifdef AVR32
    /* LED's init */
    serial_init();
    LED_On(LED0);
    LED_On(LED1);
    LED_On(LED2);
    LED_On(LED3);

    /* Interrupts init */
    // Disable all interrupts. */
    Disable_global_interrupt();

    // The INTC driver has to be used only for GNU GCC for AVR32.

    // Initialize interrupt vectors.
    INTC_init_interrupts();

    // Register the RTC interrupt handler to the interrupt controller.
    INTC_register_interrupt(&rtc_irq, AVR32_RTC_IRQ, AVR32_INTC_INT0);


    /* RTC init */
    rtc_init(&AVR32_RTC, RTC_OSC_32KHZ, RTC_PSEL_32KHZ_1HZ);
    // Set top value to 0 to generate an interrupt every seconds */
    rtc_set_top_value(&AVR32_RTC, 0);
    // Enable the interrupts
    rtc_enable_interrupt(&AVR32_RTC);
    // Enable the RTC
    rtc_enable(&AVR32_RTC);

    // Enable global interrupts
    Enable_global_interrupt();
#endif

    /* Init subsystems */
    log_init();      // Logging system
    cmd_repo_init(); //Command repository initialization
    dat_repo_init(); //Update status repository

#ifdef LINUX
    /* Init communications */
    LOGI(tag, "Initialising CSP...");
    /* Init buffer system with 5 packets of maximum 300 bytes each */
    csp_buffer_init(5, 300);
    /* Init CSP with address MY_ADDRESS */
    csp_init(SCH_COMM_ADDRESS);
    /* Start router task with 500 word stack, OS task priority 1 */
    csp_route_start_task(500, 1);
    /* Set ZMQ interface */
    csp_zmqhub_init_w_endpoints(255, SCH_COMM_ZMQ_OUT, SCH_COMM_ZMQ_IN);
    csp_route_set(CSP_DEFAULT_ROUTE, &csp_if_zmqhub, CSP_NODE_MAC);

    #if LOG_LEVEL >= LOG_LVL_DEBUG<s
        /*Debug output from CSP */
        printf("Debug enabed\r\n");
        csp_debug_set_level(1, 1);
        csp_debug_toggle_level(2);
        csp_debug_toggle_level(3);
        csp_debug_toggle_level(4);
        csp_debug_toggle_level(5);
        csp_debug_toggle_level(6);
        LOGD(tag, "Conn table");
        csp_conn_print_table();
        LOGD(tag, "Route table");
        csp_route_print_table();
        LOGD(tag, "Interfaces");
        csp_route_print_interfaces();
    #endif
#endif
}

/**
 * Performs a clean exit
 *
 * @param signal Int. Signal number
 */
void on_close(int signal)
{
    dat_repo_close();

    LOGI(tag, "Exit system!");
    exit(signal);
}


