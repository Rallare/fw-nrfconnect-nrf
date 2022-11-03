/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <dk_buttons_and_leds.h>
#include "wifi_provisioning.h"
#ifdef CONFIG_NET_SHELL
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

const struct shell *shell_backend;
static bool ping_cmd_recv;
#endif

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	uint32_t button = button_state & has_changed;
	static uint8_t nrf_wifi_ps_state = 0;
	if (button & DK_BTN1_MSK) {
	#ifdef CONFIG_NRF_WIFI_LOW_POWER
		nrf_wifi_ps_state = nrf_wifi_ps_state ? 0 : 1;
		wifi_set_power_state(nrf_wifi_ps_state);
		printk("Low Power mode %s\n", nrf_wifi_ps_state ? "On" : "Off");
		dk_set_led(DK_LED1, nrf_wifi_ps_state);
	#endif /* CONFIG_NRF_WIFI_LOW_POWER */			
	}
	if (button & DK_BTN2_MSK) {
		/* Cannot call it directly from dk-buttons-and-leds
		 *  workq item, looks like priority is equal to shell subsys
		 */
		dk_set_led(DK_LED2, 1);
		ping_cmd_recv = true;
	}
}

void main(void)
{
	int rc;
	struct wifi_config config;
	struct net_if *iface = net_if_get_default();
	struct wifi_connect_req_params cnx_params = { 0 };

	int err;
	#ifdef CONFIG_NET_SHELL
	shell_backend = shell_backend_uart_get_ptr();
	#endif
	err = dk_buttons_init(button_handler);
	if (err) {
		printk("Cannot init buttons (err: %d)", err);
	}

	err = dk_leds_init();
	if (err) {
		printk("Cannot init LEDs (err: %d)", err);
	}

	/* Sleep 1 seconds to allow initialization of wifi driver. */
	k_sleep(K_SECONDS(1));

	rc = wifi_config_init();
	if (rc != 0) {
		printk("Initializing config module failed, err = %d.\n", rc);
		return;
	}

	rc = wifi_prov_init();
	if (rc == 0) {
		printk("Wi-Fi provisioning service starts successfully.\n");
	} else {
		printk("Error occurs when initializing Wi-Fi provisioning service.\n");
		return;
	}

	if (wifi_has_config()) {
		rc = wifi_get_config(&config);
		if (rc == 0) {
			printk("Configuration found. Try to apply.\n");

			cnx_params.ssid = config.ssid;
			cnx_params.ssid_length = config.ssid_len;
			cnx_params.security = config.auth_type;

			cnx_params.psk = NULL;
			cnx_params.psk_length = 0;
			cnx_params.sae_password = NULL;
			cnx_params.sae_password_length = 0;

			if (config.auth_type != WIFI_SECURITY_TYPE_NONE) {
				cnx_params.psk = config.password;
				cnx_params.psk_length = config.password_len;
			}

			cnx_params.channel = config.channel;
			cnx_params.band = config.band;
			cnx_params.mfp = WIFI_MFP_OPTIONAL;
			rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
				&cnx_params, sizeof(struct wifi_connect_req_params));
			if (rc < 0) {
				printk("Cannot apply saved Wi-Fi configuration, err = %d.\n", rc);
			} else {
				printk("Configuration applied.\n");
			}
		}
	}
	while (1) {
		k_sleep(K_MSEC(1000));
		/* Scheduling the shell ping to main thread
		 * ie, a lower prio than workq
		 */
		if (ping_cmd_recv) {
			#ifdef CONFIG_NET_SHELL		
			char ping_cmd[64] = "net ping 8.8.8.8";
			int ret = shell_execute_cmd(shell_backend, ping_cmd);
			if (ret) {
				printk("shell error: %d\n", ret);
			}
			ping_cmd_recv = false;
			dk_set_led(DK_LED2, 0);
			#endif			
		}
	}
}
