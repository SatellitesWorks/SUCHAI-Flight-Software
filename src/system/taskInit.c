/*                                 SUCHAI
 *                      NANOSATELLITE FLIGHT SOFTWARE
 *
 *      Copyright 2018, Matias Ramirez Martinez, nicoram.mt@gmail.com
 *      Copyright 2018, Carlos Gonzalez Cortes, carlgonz@uchile.cl
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
#include "taskInit.h"

static const char *tag = "taskInit";

#if SCH_COMM_ENABLE
#ifdef LINUX
    static csp_iface_t csp_if_kiss;
    static csp_kiss_handle_t csp_kiss_driver;
    void my_usart_rx(uint8_t * buf, int len, void * pxTaskWoken) {
        csp_kiss_rx(&csp_if_kiss, buf, len, pxTaskWoken);
    }
#endif //LINUX
#endif //SCH_COMM_ENABLE

void taskInit(void *param)
{
#ifdef NANOMIND
    /**
     * Setting SPI devices
     */
    init_spi1();
    /* Init temperature sensors */
    lm70_init();
    /* Init spansion chip */
    spn_fl512s_init((unsigned int) 0);  // Creates a lock
    /* Init RTC and FRAM chip */
    fm33256b_init();  // Creates a lock
    init_rtc();

    /**
     * Setting I2C devices
     */
    twi_init();
    /* Init gyroscope */
    mpu3300_init(MPU3300_BW_5, MPU3300_FSR_225);
    /* Init magnetometer */
    hmc5843_init();
    /* Setup ADC channels for current measurements */
    adc_channels_init();  // Creates a lock
    /* Setup motherboard switches */
    //mb_switch_init();

    /**
     * Init CAN devices
     */
    init_can(0); // Init can, default disabled

    /**
    * Init PWM driver
    */
    gs_pwm_init();

    /* Latest reset source */
    int reset_source = reset_cause_get_causes();
    log_reset_cause(reset_source);
    dat_set_system_var(dat_obc_last_reset, reset_source);
#endif

    LOGD(tag, "Initialization commands ...");
    // Init LibCSP system
    init_communications();
    // Execute deployment activities if first boot
    int first_boot = dat_get_system_var(dat_dep_deployed) > 0 ? 0 : 1;
    if(first_boot)
    {
        LOGI(tag, "\tFirst boot! Execute init routines");
        init_routines();
    }

    LOGD(tag, "Creating client tasks ...");
    int t_ok;
    int n_threads = 4;
    os_thread thread_id[n_threads];

    /* Creating clients tasks */
    t_ok = osCreateTask(taskConsole, "console", SCH_TASK_CON_STACK, NULL, 2, &(thread_id[0]));
    if(t_ok != 0) LOGE(tag, "Task console not created!");

#if SCH_HK_ENABLED
    t_ok = osCreateTask(taskHousekeeping, "housekeeping", SCH_TASK_HKP_STACK, NULL, 2, &(thread_id[1]));
    if(t_ok != 0) LOGE(tag, "Task housekeeping not created!");
#endif
#if SCH_COMM_ENABLE
    t_ok = osCreateTask(taskCommunications, "comm", SCH_TASK_COM_STACK, NULL, 2, &(thread_id[2]));
    if(t_ok != 0) LOGE(tag, "Task communications not created!");
#endif
#if SCH_FP_ENABLED
    t_ok = osCreateTask(taskFlightPlan,"flightplan", SCH_TASK_FPL_STACK, NULL, 2, &(thread_id[3]));
    if(t_ok != 0) LOGE(tag, "Task flightplan not created!");
#endif

    osTaskDelete(NULL);
}

void init_communications(void)
{
#if SCH_COMM_ENABLE
    /* Init communications */
    LOGI(tag, "Initialising CSP...");

    if(LOG_LEVEL >= LOG_LVL_DEBUG)
    {
        csp_debug_set_level(CSP_ERROR, 1);
        csp_debug_set_level(CSP_WARN, 1);
        csp_debug_set_level(CSP_INFO, 1);
        csp_debug_set_level(CSP_BUFFER, 1);
        csp_debug_set_level(CSP_PACKET, 1);
        csp_debug_set_level(CSP_PROTOCOL, 1);
        csp_debug_set_level(CSP_LOCK, 0);
    }
    else
    {
        csp_debug_set_level(CSP_ERROR, 1);
        csp_debug_set_level(CSP_WARN, 1);
        csp_debug_set_level(CSP_INFO, 1);
        csp_debug_set_level(CSP_BUFFER, 0);
        csp_debug_set_level(CSP_PACKET, 0);
        csp_debug_set_level(CSP_PROTOCOL, 0);
        csp_debug_set_level(CSP_LOCK, 0);
    }

    /* Init buffer system */
    csp_conf_t sch_csp_conf;
    //csp_conf_get_defaults(&sch_csp_conf);
    sch_csp_conf.address = SCH_COMM_ADDRESS;
    sch_csp_conf.conn_max = 10;
    sch_csp_conf.conn_queue_length = 10;
    sch_csp_conf.conn_dfl_so = CSP_O_NONE;
    sch_csp_conf.fifo_length = 25;
    sch_csp_conf.port_max_bind = 24;
    sch_csp_conf.rdp_max_window = 20;

    int t_ok;
    t_ok = csp_buffer_init(SCH_BUFFERS_CSP, SCH_BUFF_MAX_LEN);
    if(t_ok != 0) LOGE(tag, "csp_buffer_init failed!");
    //csp_set_hostname("SUCHAI-OBC");
    csp_init(&sch_csp_conf); // Init CSP with address MY_ADDRESS

    /**
     * Set interfaces and routes
     *  Platform dependent
     */
#ifdef LINUX
    //csp_set_model("LINUX");

    struct usart_conf conf;
    conf.device = SCH_KISS_DEVICE;
    conf.baudrate = SCH_KISS_UART_BAUDRATE;
    usart_init(&conf);

    csp_kiss_init(&csp_if_kiss, &csp_kiss_driver, usart_putc, usart_insert, "KISS");

    /* Setup callback from USART RX to KISS RS */
    usart_set_callback(my_usart_rx);
    csp_route_set(SCH_TNC_ADDRESS, &csp_if_kiss, CSP_NODE_MAC);
    csp_route_set(CSP_DEFAULT_ROUTE, &csp_if_kiss, CSP_NODE_MAC);
    csp_rtable_set(0, 2, &csp_if_kiss, SCH_TNC_ADDRESS); // Traffic to GND (0-7) via KISS node TNC

    /* Set ZMQ interface */
    csp_zmqhub_init_w_endpoints(255, SCH_COMM_ZMQ_OUT, SCH_COMM_ZMQ_IN);
    csp_route_set(CSP_DEFAULT_ROUTE, &csp_if_zmqhub, CSP_NODE_MAC);
#endif //LINUX

#ifdef NANOMIND
    //csp_set_model("A3200");
    /* Init csp i2c interface with address 1 and 400 kHz clock */
    LOGI(tag, "csp_i2c_init...");
    t_ok = csp_i2c_init(SCH_COMM_ADDRESS, 0, 400);
    if(t_ok != CSP_ERR_NONE) LOGE(tag, "\tcsp_i2c_init failed!");

    /**
     * Setting route table
     * Build with options: --enable-if-i2c --with-rtable cidr
     *  csp_rtable_load("8/2 I2C 5");
     *  csp_rtable_load("0/0 I2C");
     */
    csp_rtable_set(8, 2, &csp_if_i2c, SCH_TRX_ADDRESS); // Traffic to GND (8-15) via I2C node TRX
    csp_route_set(CSP_DEFAULT_ROUTE, &csp_if_i2c, CSP_NODE_MAC); // All traffic to I2C using node as i2c address
#endif //NANOMIND

    /* Start router task with SCH_TASK_CSP_STACK word stack, OS task priority 1 */
    t_ok = csp_route_start_task(SCH_TASK_CSP_STACK, 1);
    if(t_ok != 0) LOGE(tag, "Task router not created!");

    LOGD(tag, "Route table");
    csp_route_print_table();
    LOGD(tag, "Interfaces");
    csp_route_print_interfaces();
#endif //SCH_COMM_ENABLE
}

void init_routines(void)
{
    LOGD(tag, "\tAntenna deployment...")
    //Update antenna deployment status
    cmd_t *cmd_dep;
    cmd_dep = cmd_get_str("istage_update_status");
    cmd_send(cmd_dep);

    //Try to deploy antennas if necessary
    //      istage 1. On: 2s, off: 1s, rep: 5
    cmd_dep = cmd_get_str("istage_antenna_release");
    cmd_add_params_var(cmd_dep, 16, 2, 1, 5);
    cmd_send(cmd_dep);
    //      istage 2. On: 2s, off: 1s, rep: 5
    cmd_dep = cmd_get_str("istage_antenna_release");
    cmd_add_params_var(cmd_dep, 17, 2, 1, 5);
    cmd_send(cmd_dep);
    //      istage 3. On: 2s, off: 1s, rep: 5
    cmd_dep = cmd_get_str("istage_antenna_release");
    cmd_add_params_var(cmd_dep, 18, 2, 1, 5);
    cmd_send(cmd_dep);
    //      istage 4. On: 2s, off: 1s, rep: 5
    cmd_dep = cmd_get_str("istage_antenna_release");
    cmd_add_params_var(cmd_dep, 19, 2, 1, 5);
    cmd_send(cmd_dep);

    //Update antenna deployment status
    cmd_dep = cmd_get_str("istage_update_status");
    cmd_send(cmd_dep);


    LOGD(tag, "\t Init TRX...");
    cmd_t *trx_cmd;
    // Set TX_INHIBIT to implement silent time
    trx_cmd = cmd_get_str("com_set_config");
    cmd_add_params_var(trx_cmd, "tx_inhibit", TOSTRING(SCH_TX_INHIBIT));
    cmd_send(trx_cmd);
    if(LOG_LEVEL >= LOG_LVL_DEBUG)
    {
        trx_cmd = cmd_get_str("com_get_config");
        cmd_add_params_str(trx_cmd, "tx_inhibit");
        cmd_send(trx_cmd);
    }
    // Set TX_PWR
    trx_cmd = cmd_get_str("com_set_config");
    cmd_add_params_var(trx_cmd, "tx_pwr", TOSTRING(SCH_TX_PWR));
    cmd_send(trx_cmd);
    if(LOG_LEVEL >= LOG_LVL_DEBUG)
    {
        trx_cmd = cmd_get_str("com_get_config");
        cmd_add_params_str(trx_cmd, "tx_pwr");
        cmd_send(trx_cmd);
    }
    // Set TX_BEACON_PERIOD
    trx_cmd = cmd_get_str("com_set_config");
    cmd_add_params_var(trx_cmd, "bcn_interval", TOSTRING(SCH_TX_BCN_PERIOD));
    cmd_send(trx_cmd);
    if(LOG_LEVEL >= LOG_LVL_DEBUG)
    {
        trx_cmd = cmd_get_str("com_get_config");
        cmd_add_params_str(trx_cmd, "bcn_interval");
        cmd_send(trx_cmd);
    }
    // Set TRX Freq
    trx_cmd = cmd_get_str("com_set_config");
    cmd_add_params_var(trx_cmd, "freq", TOSTRING(SCH_TX_FREQ));
    cmd_send(trx_cmd);
    if(LOG_LEVEL >= LOG_LVL_DEBUG)
    {
        trx_cmd = cmd_get_str("com_get_config");
        cmd_add_params_str(trx_cmd, "freq");
        cmd_send(trx_cmd);
    }
    // Set TRX Baud
    trx_cmd = cmd_get_str("com_set_config");
    cmd_add_params_var(trx_cmd, "baud", TOSTRING(SCH_TX_BAUD));
    cmd_send(trx_cmd);
    if(LOG_LEVEL >= LOG_LVL_DEBUG)
    {
        trx_cmd = cmd_get_str("com_get_config");
        cmd_add_params_str(trx_cmd, "baud");
        cmd_send(trx_cmd);
    }
}