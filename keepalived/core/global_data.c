/*
 * Soft:        Keepalived is a failover program for the LVS project
 *              <www.linuxvirtualserver.org>. It monitor & manipulate
 *              a loadbalanced server pool using multi-layer checks.
 *
 * Part:        Dynamic data structure definition.
 *
 * Author:      Alexandre Cassen, <acassen@linux-vs.org>
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Copyright (C) 2001-2017 Alexandre Cassen, <acassen@gmail.com>
 */

#include "config.h"

#include <unistd.h>
#include <pwd.h>
#include <sched.h>

#include "global_data.h"
#include "list_head.h"
#include "logger.h"
#include "parser.h"
#include "utils.h"
#include "main.h"
#include "memory.h"
#ifdef _WITH_VRRP_
#include "vrrp.h"
#include "vrrp_ipaddress.h"
#endif
#include "process.h"
#ifdef _WITH_FIREWALL_
#include "vrrp_firewall.h"
#endif
#include "align.h"
#include "pidfile.h"

/* global vars */
data_t *global_data = NULL;
data_t *old_global_data = NULL;

/* Default settings */
static void
set_default_router_id(data_t *data, const char *new_id)
{
	if (!new_id || !new_id[0])
		return;

	data->router_id = STRDUP(new_id);
}

static void
set_default_email_from(data_t * data, const char *hostname)
{
	struct passwd *pwd = NULL;
	size_t len;
	char *str;
	if (!hostname || !hostname[0])
		return;

	pwd = getpwuid(getuid());
	if (!pwd)
		return;

	len = strlen(hostname) + strlen(pwd->pw_name) + 2;
	data->email_from = str = MALLOC(len);
	if (!data->email_from)
		return;

	snprintf(str, len, "%s@%s", pwd->pw_name, hostname);
}

static void
set_default_smtp_connection_timeout(data_t * data)
{
	data->smtp_connection_to = DEFAULT_SMTP_CONNECTION_TIMEOUT;
}

#ifdef _WITH_VRRP_
static void
set_default_mcast_group(data_t * data)
{
	/* coverity[check_return] */
	inet_stosockaddr(INADDR_VRRP_GROUP, 0, PTR_CAST(struct sockaddr_storage, &data->vrrp_mcast_group4));
	/* coverity[check_return] */
	inet_stosockaddr(INADDR6_VRRP_GROUP, 0, PTR_CAST(struct sockaddr_storage, &data->vrrp_mcast_group6));
}

static void
set_vrrp_defaults(data_t * data)
{
	data->vrrp_garp_rep = VRRP_GARP_REP;
	data->vrrp_garp_refresh.tv_sec = VRRP_GARP_REFRESH;
	data->vrrp_garp_refresh_rep = VRRP_GARP_REFRESH_REP;
	data->vrrp_garp_delay = VRRP_GARP_DELAY;
	data->vrrp_garp_lower_prio_delay = PARAMETER_UNSET;
	data->vrrp_garp_lower_prio_rep = PARAMETER_UNSET;
#ifdef _HAVE_VRRP_VMAC_
	data->vrrp_vmac_garp_intvl = 0;
#endif
	data->vrrp_lower_prio_no_advert = false;
	data->vrrp_higher_prio_send_advert = false;
	data->vrrp_version = VRRP_VERSION_2;
#ifdef _HAVE_LIBIPSET_
	data->using_ipsets = PARAMETER_UNSET;
#endif
	data->vrrp_check_unicast_src = false;
	data->vrrp_skip_check_adv_addr = false;
	data->vrrp_strict = false;
#ifdef _WITH_NFTABLES_
	data->vrrp_nf_chain_priority = -1;
#endif
}
#endif

/* email facility functions */
static void
free_email_list(list_head_t *l)
{
	email_t *email, *email_tmp;

	list_for_each_entry_safe(email, email_tmp, l, e_list) {
		FREE(email->addr);
		FREE(email);
	}
}
static void
dump_email_list(FILE *fp, const list_head_t *l)
{
	email_t *email;

	list_for_each_entry(email, l, e_list)
		conf_write(fp, "   %s", email->addr);
}

void
alloc_email(const char *addr)
{
	email_t *email;

	PMALLOC(email);
	INIT_LIST_HEAD(&email->e_list);
	email->addr = STRDUP(addr);

	list_add_tail(&email->e_list, &global_data->email);
}

/* data facility functions */
data_t *
alloc_global_data(void)
{
	data_t *new;

	if (global_data)
		return global_data;

	PMALLOC(new);
	INIT_LIST_HEAD(&new->email);
	new->smtp_alert = -1;
#ifdef _WITH_VRRP_
	new->smtp_alert_vrrp = -1;
#endif
#ifdef _WITH_LVS_
	new->smtp_alert_checker = -1;
#endif

#ifdef _WITH_VRRP_
	set_default_mcast_group(new);
	set_vrrp_defaults(new);
#endif
	new->notify_fifo.fd = -1;
	new->max_auto_priority = 0;
	new->min_auto_priority_delay = 1000000;	/* 1 second */
#ifdef _WITH_VRRP_
	new->vrrp_notify_fifo.fd = -1;
	new->vrrp_rlimit_rt = RT_RLIMIT_DEFAULT;
	new->vrrp_rx_bufs_multiples = 3;
#endif
#ifdef _WITH_LVS_
	new->lvs_notify_fifo.fd = -1;
	new->checker_rlimit_rt = RT_RLIMIT_DEFAULT;
#ifdef _WITH_BFD_
	new->bfd_rlimit_rt = RT_RLIMIT_DEFAULT;
#endif
#endif

#ifdef _WITH_SNMP_
	if (snmp_option) {
#ifdef _WITH_SNMP_VRRP_
		new->enable_snmp_vrrp = true;
#endif
#ifdef _WITH_SNMP_RFCV2_
		new->enable_snmp_rfcv2 = true;
#endif
#ifdef _WITH_SNMP_RFCV3_
		new->enable_snmp_rfcv3 = true;
#endif
#ifdef _WITH_SNMP_CHECKER_
		new->enable_snmp_checker = true;
#endif
	}

	if (snmp_socket)
		new->snmp_socket = STRDUP(snmp_socket);
#endif

#ifdef _WITH_LVS_
#ifdef _WITH_VRRP_
	new->lvs_syncd.syncid = PARAMETER_UNSET;
#ifdef _HAVE_IPVS_SYNCD_ATTRIBUTES_
	new->lvs_syncd.mcast_group.ss_family = AF_UNSPEC;
#endif
#endif
#endif

	return new;
}

void
init_global_data(data_t * data, data_t *prev_global_data, bool copy_unchangeable_config)
{
	/* If this is a reload and we are running in a network namespace,
	 * we may not be able to get local_name, so preserve it */
	const char unknown_name[] = "[unknown]";

	/* If we are running in a network namespace, we may not be
	 * able to get our local name now, so re-use original */
	if (prev_global_data) {
		data->local_name = prev_global_data->local_name;
		prev_global_data->local_name = NULL;

		if (copy_unchangeable_config) {
			FREE_CONST_PTR(data->network_namespace);
			data->network_namespace = prev_global_data->network_namespace;
			prev_global_data->network_namespace = NULL;

			FREE_CONST_PTR(data->network_namespace_ipvs);
			data->network_namespace_ipvs = prev_global_data->network_namespace_ipvs;
			prev_global_data->network_namespace_ipvs = NULL;

			FREE_CONST_PTR(data->instance_name);
			data->instance_name = prev_global_data->instance_name;
			prev_global_data->instance_name = NULL;
		}
	}

#ifndef _ONE_PROCESS_DEBUG_
	if (data->reload_file == DEFAULT_RELOAD_FILE) {
		if (data->instance_name)
			data->reload_file = make_pidfile_name(KEEPALIVED_PID_DIR KEEPALIVED_PID_FILE, data->instance_name, RELOAD_EXTENSION);
		else if (use_pid_dir)
			data->reload_file = STRDUP(KEEPALIVED_PID_DIR KEEPALIVED_PID_FILE RELOAD_EXTENSION);
		else
			data->reload_file = STRDUP(RUN_DIR KEEPALIVED_PID_FILE RELOAD_EXTENSION);
	}
#endif

	if (!data->local_name &&
	    (!data->router_id ||
	     (data->smtp_server.ss_family &&
	      (!data->smtp_helo_name ||
	       !data->email_from)))) {
		data->local_name = get_local_name();

		/* If for some reason get_local_name() fails, we need to have
		 * some string in local_name, otherwise keepalived can segfault */
		if (!data->local_name)
			data->local_name = STRDUP(unknown_name);
	}

	if (!data->router_id)
		set_default_router_id(data, data->local_name);

	if (data->smtp_server.ss_family) {
		if (!data->smtp_connection_to)
			set_default_smtp_connection_timeout(data);

		if (data->local_name && strcmp(data->local_name, unknown_name)) {
			if (!data->email_from)
				set_default_email_from(data, data->local_name);

			if (!data->smtp_helo_name)
				data->smtp_helo_name = STRDUP(data->local_name);
		}
	}

	/* Check that there aren't conflicts with the notify FIFOs */
#ifdef _WITH_VRRP_
	/* If the global and vrrp notify FIFOs are the same, then data will be
	 * duplicated on the FIFO */
	if (
#ifndef _ONE_PROCESS_DEBUG_
	    prog_type == PROG_TYPE_VRRP &&
#endif
	    data->notify_fifo.name && data->vrrp_notify_fifo.name &&
	    !strcmp(data->notify_fifo.name, data->vrrp_notify_fifo.name)) {
		log_message(LOG_INFO, "notify FIFO %s has been specified for global and vrrp FIFO - ignoring vrrp FIFO", data->vrrp_notify_fifo.name);
		FREE_CONST_PTR(data->vrrp_notify_fifo.name);
		data->vrrp_notify_fifo.name = NULL;
		free_notify_script(&data->vrrp_notify_fifo.script);
	}
#endif
#ifdef _WITH_LVS_
	/* If the global and LVS notify FIFOs are the same, then data will be
	 * duplicated on the FIFO */
#ifndef _ONE_PROCESS_DEBUG_
	if (prog_type == PROG_TYPE_CHECKER)
#endif
	{
		if (data->notify_fifo.name && data->lvs_notify_fifo.name &&
		    !strcmp(data->notify_fifo.name, data->lvs_notify_fifo.name)) {
			log_message(LOG_INFO, "notify FIFO %s has been specified for global and LVS FIFO - ignoring LVS FIFO", data->lvs_notify_fifo.name);
			FREE_CONST_PTR(data->lvs_notify_fifo.name);
			data->lvs_notify_fifo.name = NULL;
			free_notify_script(&data->lvs_notify_fifo.script);
		}

#ifdef _WITH_VRRP_
		/* If LVS and VRRP use the same FIFO, they cannot both have a script for the FIFO.
		 * Use the VRRP script and ignore the LVS script */
		if (data->lvs_notify_fifo.name && data->vrrp_notify_fifo.name &&
		    !strcmp(data->lvs_notify_fifo.name, data->vrrp_notify_fifo.name) &&
		    data->lvs_notify_fifo.script &&
		    data->vrrp_notify_fifo.script) {
			log_message(LOG_INFO, "LVS notify FIFO and vrrp FIFO are the same both with scripts - ignoring LVS FIFO script");
			free_notify_script(&data->lvs_notify_fifo.script);
		}
#endif
	}
#endif
}

void
free_global_data(data_t * data)
{
	if (!data)
		return;

	free_email_list(&data->email);
	FREE_CONST_PTR(data->network_namespace);
	FREE_CONST_PTR(data->network_namespace_ipvs);
	FREE_CONST_PTR(data->instance_name);
	FREE_CONST_PTR(data->process_name);
#ifdef _WITH_VRRP_
	FREE_CONST_PTR(data->vrrp_process_name);
#endif
#ifdef _WITH_LVS_
	FREE_CONST_PTR(data->lvs_process_name);
#endif
#ifdef _WITH_BFD_
	FREE_CONST_PTR(data->bfd_process_name);
#endif
	FREE_CONST_PTR(data->router_id);
	FREE_CONST_PTR(data->email_from);
	FREE_CONST_PTR(data->smtp_helo_name);
	FREE_CONST_PTR(data->local_name);
#ifdef _WITH_SNMP_
	FREE_CONST_PTR(data->snmp_socket);
#endif
	free_notify_script(&data->startup_script);
	free_notify_script(&data->shutdown_script);
#if defined _WITH_LVS_ && defined _WITH_VRRP_
	FREE_CONST_PTR(data->lvs_syncd.ifname);
	FREE_CONST_PTR(data->lvs_syncd.vrrp_name);
#endif
	FREE_CONST_PTR(data->notify_fifo.name);
	free_notify_script(&data->notify_fifo.script);
#ifdef _WITH_VRRP_
	FREE_CONST_PTR(data->default_ifname);
	FREE_CONST_PTR(data->vrrp_notify_fifo.name);
	free_notify_script(&data->vrrp_notify_fifo.script);
#ifdef _HAVE_VRRP_VMAC_
	FREE_CONST_PTR(data->vmac_prefix);
	FREE_CONST_PTR(data->vmac_addr_prefix);
#endif
#ifdef _WITH_IPTABLES_
	FREE_CONST_PTR(data->vrrp_iptables_inchain);
	FREE_CONST_PTR(data->vrrp_iptables_outchain);
#ifdef _HAVE_LIBIPSET_
	FREE_CONST_PTR(data->vrrp_ipset_address);
	FREE_CONST_PTR(data->vrrp_ipset_address6);
	FREE_CONST_PTR(data->vrrp_ipset_address_iface6);
	FREE_CONST_PTR(data->vrrp_ipset_igmp);
	FREE_CONST_PTR(data->vrrp_ipset_mld);
#endif
#endif
#ifdef _WITH_NFTABLES_
	FREE_CONST_PTR(data->vrrp_nf_table_name);
#endif
#endif
#ifdef _WITH_LVS_
	FREE_CONST_PTR(data->lvs_notify_fifo.name);
	free_notify_script(&data->lvs_notify_fifo.script);
#ifdef _WITH_NFTABLES_
	FREE_CONST_PTR(data->ipvs_nf_table_name);
#endif
#endif
#ifdef _WITH_DBUS_
	FREE_CONST_PTR(data->dbus_service_name);
#endif
#ifndef _ONE_PROCESS_DEBUG_
	FREE_CONST_PTR(data->reload_check_config);
	FREE_CONST_PTR(data->reload_file);
	FREE_CONST_PTR(data->reload_time_file);
#endif
	FREE_CONST_PTR(data->config_directory);
	FREE(data);
}

FILE * __attribute__((malloc))
open_dump_file(const char *file_name)
{
	FILE *fp;
	char *file_name_tmp = NULL;
	const char *dot;
	int len;

	if (global_data->data_use_instance &&
	    (global_data->instance_name || global_data->network_namespace)) {
		len = strlen(file_name) + 1;
		if (global_data->instance_name)
			len += strlen(global_data->instance_name) + 1;
		if (global_data->network_namespace)
			len += strlen(global_data->network_namespace) + 1;

		file_name_tmp = MALLOC(len);

		dot = strrchr(file_name, '.');
		sprintf(file_name_tmp, "%.*s.%s%s%s%s",
				(int)(dot - file_name), file_name,
				global_data->network_namespace ? global_data->network_namespace : "",
				global_data->instance_name && global_data->network_namespace ? "_" : "",
				global_data->instance_name ? global_data->instance_name : "",
				dot);
		file_name = file_name_tmp;
	}

	fp = fopen_safe(file_name, "w");

	if (!fp)
		log_message(LOG_INFO, "Can't open dump file %s (%d: %s)",
			file_name, errno, strerror(errno));

	if (file_name_tmp)
		FREE(file_name_tmp);

	return fp;
}

void
dump_global_data(FILE *fp, data_t * data)
{
	char cpu_str[64];
#ifdef _WITH_VRRP_
	char buf[64];
#endif
#ifndef _ONE_PROCESS_DEBUG_
	char date_time_str[20];
	struct tm tm;
#endif
	unsigned val;

	if (!data)
		return;

	conf_write(fp, "------< Global definitions >------");

#ifndef _ONE_PROCESS_DEBUG_
	if (config_save_dir)
		conf_write(fp, " Config save dir = %s", config_save_dir);
#endif

	conf_write(fp, " Network namespace = %s", data->network_namespace ? data->network_namespace : "(default)");
	conf_write(fp, " Network namespace ipvs = %s", data->network_namespace_ipvs ? data->network_namespace_ipvs[0] ? data->network_namespace_ipvs : "(default)" : "(main namespace)");
	if (data->instance_name)
		conf_write(fp, " Instance name = %s", data->instance_name);
	if (data->process_name)
		conf_write(fp, " Parent process name = %s", data->process_name);
#ifdef _WITH_VRRP_
	if (data->vrrp_process_name)
		conf_write(fp, " VRRP process name = %s", data->vrrp_process_name);
#endif
#ifdef _WITH_LVS_
	if (data->lvs_process_name)
		conf_write(fp, " LVS process name = %s", data->lvs_process_name);
#endif
#ifdef _WITH_BFD_
	if (data->bfd_process_name)
		conf_write(fp, " BFD process name = %s", data->bfd_process_name);
#endif
	if (data->router_id)
		conf_write(fp, " Router ID = %s", data->router_id);
	if (data->smtp_server.ss_family) {
		conf_write(fp, " Smtp server = %s", inet_sockaddrtos(&data->smtp_server));
		conf_write(fp, " Smtp server port = %u", ntohs(inet_sockaddrport(&data->smtp_server)));
	}
	if (data->smtp_helo_name)
		conf_write(fp, " Smtp HELO name = %s" , data->smtp_helo_name);
	if (data->smtp_connection_to)
		conf_write(fp, " Smtp server connection timeout = %lu"
			     , data->smtp_connection_to / TIMER_HZ);
	if (data->email_from) {
		conf_write(fp, " Email notification from = %s"
				    , data->email_from);
		conf_write(fp, " Email notification to:");
		dump_email_list(fp, &data->email);
	}
	conf_write(fp, " Default smtp_alert = %s",
			data->smtp_alert == -1 ? "unset" : data->smtp_alert ? "on" : "off");
#ifdef _WITH_VRRP_
	conf_write(fp, " Default smtp_alert_vrrp = %s",
			data->smtp_alert_vrrp == -1 ? "unset" : data->smtp_alert_vrrp ? "on" : "off");
#endif
#ifdef _WITH_LVS_
	conf_write(fp, " Default smtp_alert_checker = %s",
			data->smtp_alert_checker == -1 ? "unset" : data->smtp_alert_checker ? "on" : "off");
	conf_write(fp, " Checkers log all failures = %s", data->checker_log_all_failures ? "true" : "false");
#endif
#ifndef _ONE_PROCESS_DEBUG_
	if (data->reload_check_config)
		conf_write(fp, " Test config before reload, log to %s", data->reload_check_config);
	else
		conf_write(fp, " No test config before reload");
	if (data->reload_time_file) {
		conf_write(fp, " Reload time file = %s%s", data->reload_time_file, data->reload_repeat ? " (repeat)" : "");
		if (data->reload_time) {
			localtime_r(&data->reload_time, &tm);
			strftime(date_time_str, sizeof(date_time_str), "%Y-%m-%d %H:%M:%S", &tm);
			conf_write(fp, " Reload scheduled for %s%s", date_time_str, global_data->reload_date_specified ? " (date specified" : "");
		} else
			conf_write(fp, " No reload scheduled");
	}
	if (data->reload_file)
		conf_write(fp, " Reload_file = %s", data->reload_file);
#endif
	if (data->config_directory)
		conf_write(fp, " config save directory = %s", data->config_directory);
	if (data->data_use_instance)
		conf_write(fp, " Use instance name in data dumps");
	if (data->startup_script)
		conf_write(fp, " Startup script = %s, uid:gid %u:%u, timeout %u",
			    cmd_str(data->startup_script),
			    data->startup_script->uid,
			    data->startup_script->gid,
			    data->startup_script_timeout);
	if (data->shutdown_script)
		conf_write(fp, " Shutdown script = %s, uid:gid %u:%u timeout %u",
			    cmd_str(data->shutdown_script),
			    data->shutdown_script->uid,
			    data->shutdown_script->gid,
			    data->shutdown_script_timeout);
#ifdef _WITH_VRRP_
	conf_write(fp, " Dynamic interfaces = %s", data->dynamic_interfaces ? "true" : "false");
	if (data->dynamic_interfaces)
		conf_write(fp, " Allow interface changes = %s", data->allow_if_changes ? "true" : "false");
	if (data->no_email_faults)
		conf_write(fp, " Send emails for fault transitions = off");
#endif
#ifdef _WITH_LVS_
	if (data->lvs_timeouts.tcp_timeout)
		conf_write(fp, " LVS TCP timeout = %d", data->lvs_timeouts.tcp_timeout);
	if (data->lvs_timeouts.tcp_fin_timeout)
		conf_write(fp, " LVS TCP FIN timeout = %d", data->lvs_timeouts.tcp_fin_timeout);
	if (data->lvs_timeouts.udp_timeout)
		conf_write(fp, " LVS TCP timeout = %d", data->lvs_timeouts.udp_timeout);
#ifdef _WITH_VRRP_
#ifndef _ONE_PROCESS_DEBUG_
	if (prog_type == PROG_TYPE_VRRP)
#endif
		conf_write(fp, " Default interface = %s", data->default_ifp ? data->default_ifp->ifname : DFLT_INT);
	conf_write(fp, " Disable local IGMP = %s", data->disable_local_igmp ? "yes" : "no");
	if (data->lvs_syncd.ifname) {
		if (data->lvs_syncd.vrrp)
			conf_write(fp, " LVS syncd vrrp instance = %s"
				     , data->lvs_syncd.vrrp->iname);
		else if (data->lvs_syncd.vrrp_name)
			conf_write(fp, " LVS syncd vrrp name = %s"
				     , data->lvs_syncd.vrrp_name);
		conf_write(fp, " LVS syncd interface = %s"
			     , data->lvs_syncd.ifname);
		conf_write(fp, " LVS syncd syncid = %u"
				    , data->lvs_syncd.syncid);
#ifdef _HAVE_IPVS_SYNCD_ATTRIBUTES_
		if (data->lvs_syncd.sync_maxlen)
			conf_write(fp, " LVS syncd maxlen = %u", data->lvs_syncd.sync_maxlen);
		if (data->lvs_syncd.mcast_group.ss_family != AF_UNSPEC)
			conf_write(fp, " LVS mcast group %s", inet_sockaddrtos(&data->lvs_syncd.mcast_group));
		if (data->lvs_syncd.mcast_port)
			conf_write(fp, " LVS syncd mcast port = %d", data->lvs_syncd.mcast_port);
		if (data->lvs_syncd.mcast_ttl)
			conf_write(fp, " LVS syncd mcast ttl = %u", data->lvs_syncd.mcast_ttl);
#endif
	}
#endif
	conf_write(fp, " LVS flush = %s", data->lvs_flush ? "true" : "false");
	conf_write(fp, " LVS flush on stop = %s", data->lvs_flush_on_stop == LVS_FLUSH_FULL ? "full" :
						  data->lvs_flush_on_stop == LVS_FLUSH_VS ? "VS" : "disabled");
#endif
	if (data->notify_fifo.name) {
		conf_write(fp, " Global notify fifo = %s, uid:gid %u:%u", data->notify_fifo.name, data->notify_fifo.uid, data->notify_fifo.gid);
		if (data->notify_fifo.script)
			conf_write(fp, " Global notify fifo script = %s, uid:gid %u:%u",
				    cmd_str(data->notify_fifo.script),
				    data->notify_fifo.script->uid,
				    data->notify_fifo.script->gid);
	}
#ifdef _WITH_VRRP_
	if (data->vrrp_notify_fifo.name) {
		conf_write(fp, " VRRP notify fifo = %s, uid:gid %u:%u", data->vrrp_notify_fifo.name, data->vrrp_notify_fifo.uid, data->vrrp_notify_fifo.gid);
		if (data->vrrp_notify_fifo.script)
			conf_write(fp, " VRRP notify fifo script = %s, uid:gid %u:%u",
				    cmd_str(data->vrrp_notify_fifo.script),
				    data->vrrp_notify_fifo.script->uid,
				    data->vrrp_notify_fifo.script->gid);
	}
#endif
#ifdef _WITH_LVS_
	if (data->lvs_notify_fifo.name) {
		conf_write(fp, " LVS notify fifo = %s, uid:gid %u:%u", data->lvs_notify_fifo.name, data->lvs_notify_fifo.uid, data->lvs_notify_fifo.gid);
		if (data->lvs_notify_fifo.script)
			conf_write(fp, " LVS notify fifo script = %s, uid:gid %u:%u",
				    cmd_str(data->lvs_notify_fifo.script),
				    data->lvs_notify_fifo.script->uid,
				    data->lvs_notify_fifo.script->gid);
	}
#endif
#ifdef _WITH_VRRP_
	conf_write(fp, " VRRP notify priority changes = %s", data->vrrp_notify_priority_changes ? "true" : "false");
	if (data->vrrp_mcast_group4.sin_family) {
		conf_write(fp, " VRRP IPv4 mcast group = %s"
				    , inet_sockaddrtos(PTR_CAST(struct sockaddr_storage, &data->vrrp_mcast_group4)));
	}
	if (data->vrrp_mcast_group6.sin6_family) {
		conf_write(fp, " VRRP IPv6 mcast group = %s"
				    , inet_sockaddrtos(PTR_CAST(struct sockaddr_storage, &data->vrrp_mcast_group6)));
	}
	conf_write(fp, " Gratuitous ARP delay = %u",
		       data->vrrp_garp_delay/TIMER_HZ);
	conf_write(fp, " Gratuitous ARP repeat = %u", data->vrrp_garp_rep);
	conf_write(fp, " Gratuitous ARP refresh timer = %ld", data->vrrp_garp_refresh.tv_sec);
	conf_write(fp, " Gratuitous ARP refresh repeat = %u", data->vrrp_garp_refresh_rep);
	conf_write(fp, " Gratuitous ARP lower priority delay = %u", data->vrrp_garp_lower_prio_delay == PARAMETER_UNSET ? PARAMETER_UNSET : data->vrrp_garp_lower_prio_delay / TIMER_HZ);
	conf_write(fp, " Gratuitous ARP lower priority repeat = %u", data->vrrp_garp_lower_prio_rep);
#ifdef _HAVE_VRRP_VMAC_
	if (data->vrrp_vmac_garp_intvl != PARAMETER_UNSET)
		conf_write(fp, " Gratuitous ARP for each secondary %s = %us", data->vrrp_vmac_garp_all_if ? "i/f" : "VMAC", data->vrrp_vmac_garp_intvl);
#endif
	conf_write(fp, " Send advert after receive lower priority advert = %s", data->vrrp_lower_prio_no_advert ? "false" : "true");
	conf_write(fp, " Send advert after receive higher priority advert = %s", data->vrrp_higher_prio_send_advert ? "true" : "false");
	conf_write(fp, " Gratuitous ARP interval = %f", data->vrrp_garp_interval / TIMER_HZ_DOUBLE);
	conf_write(fp, " Gratuitous NA interval = %f", data->vrrp_gna_interval / TIMER_HZ_DOUBLE);
	conf_write(fp, " VRRP default protocol version = %d", data->vrrp_version);
#ifdef _WITH_IPTABLES_
	if (data->vrrp_iptables_inchain) {
		conf_write(fp," Iptables input chain = %s", data->vrrp_iptables_inchain);
		if (data->vrrp_iptables_outchain)
			conf_write(fp," Iptables output chain = %s", data->vrrp_iptables_outchain);
#ifdef _HAVE_LIBIPSET_
		conf_write(fp, " Using ipsets = %s", data->using_ipsets ? "true" : "false");
		if (data->using_ipsets) {
			if (data->vrrp_ipset_address)
				conf_write(fp," ipset IPv4 address set = %s", data->vrrp_ipset_address);
			if (data->vrrp_ipset_address6)
				conf_write(fp," ipset IPv6 address set = %s", data->vrrp_ipset_address6);
			if (data->vrrp_ipset_address_iface6)
				conf_write(fp," ipset IPv6 address,iface set = %s", data->vrrp_ipset_address_iface6);
			if (data->vrrp_ipset_igmp)
				conf_write(fp," ipset IGMP set = %s", data->vrrp_ipset_igmp);
			if (data->vrrp_ipset_mld)
				conf_write(fp," ipset MLD set = %s", data->vrrp_ipset_mld);
		}
#endif
	}
#endif
#ifdef _WITH_NFTABLES_
#ifdef _WITH_VRRP_
	if (data->vrrp_nf_table_name) {
		conf_write(fp," nftables table name = %s", data->vrrp_nf_table_name);
		conf_write(fp," nftables base chain priority = %d", data->vrrp_nf_chain_priority);
		conf_write(fp," nftables %sforce use ifindex for link local IPv6", data->vrrp_nf_ifindex ? "" : "don't ");
	}
#endif

#ifdef _WITH_LVS_
	if (data->ipvs_nf_table_name) {
		conf_write(fp," ipvs nftables table name = %s", data->ipvs_nf_table_name);
		conf_write(fp," ipvs nftables base chain priority = %d", data->ipvs_nf_chain_priority);
		conf_write(fp," ipvs nftables start fwmark = %u", data->ipvs_nftables_start_fwmark);
	}
#endif
	conf_write(fp," nftables with%s counters", data->nf_counters ? "" : "out");
	conf_write(fp," libnftnl version %u.%u.%u", LIBNFTNL_VERSION >> 16,
		       (LIBNFTNL_VERSION >> 8) & 0xff, LIBNFTNL_VERSION & 0xff);
#endif

	conf_write(fp, " VRRP check unicast_src = %s", data->vrrp_check_unicast_src ? "true" : "false");
	conf_write(fp, " VRRP skip check advert addresses = %s", data->vrrp_skip_check_adv_addr ? "true" : "false");
	conf_write(fp, " VRRP strict mode = %s", data->vrrp_strict ? "true" : "false");
	if (data->max_auto_priority == -1)
		conf_write(fp, " Max auto priority = Disabled");
	else
		conf_write(fp, " Max auto priority = %d", data->max_auto_priority);
	conf_write(fp, " Min auto priority delay = %ld usecs", data->min_auto_priority_delay);
	conf_write(fp, " VRRP process priority = %d", data->vrrp_process_priority);
	conf_write(fp, " VRRP don't swap = %s", data->vrrp_no_swap ? "true" : "false");
	conf_write(fp, " VRRP realtime priority = %u", data->vrrp_realtime_priority);
	if (CPU_COUNT(&data->vrrp_cpu_mask)) {
		get_process_cpu_affinity_string(&data->vrrp_cpu_mask, cpu_str, 63);
		conf_write(fp, " VRRP CPU Affinity = %s", cpu_str);
	}
	conf_write(fp, " VRRP realtime limit = %" PRI_rlim_t, data->vrrp_rlimit_rt);
#endif
#ifdef _WITH_LVS_
	conf_write(fp, " Checker process priority = %d", data->checker_process_priority);
	conf_write(fp, " Checker don't swap = %s", data->checker_no_swap ? "true" : "false");
	conf_write(fp, " Checker realtime priority = %u", data->checker_realtime_priority);
	if (CPU_COUNT(&data->checker_cpu_mask)) {
		get_process_cpu_affinity_string(&data->checker_cpu_mask, cpu_str, 63);
		conf_write(fp, " Checker CPU Affinity = %s", cpu_str);
	}
	conf_write(fp, " Checker realtime limit = %" PRI_rlim_t, data->checker_rlimit_rt);
#endif
#ifdef _WITH_BFD_
	conf_write(fp, " BFD process priority = %d", data->bfd_process_priority);
	conf_write(fp, " BFD don't swap = %s", data->bfd_no_swap ? "true" : "false");
	conf_write(fp, " BFD realtime priority = %u", data->bfd_realtime_priority);
	if (CPU_COUNT(&data->bfd_cpu_mask)) {
		get_process_cpu_affinity_string(&data->bfd_cpu_mask, cpu_str, 63);
		conf_write(fp, " BFD CPU Affinity = %s", cpu_str);
	}
	conf_write(fp, " BFD realtime limit = %" PRI_rlim_t, data->bfd_rlimit_rt);
#endif
#ifdef _WITH_SNMP_VRRP_
	conf_write(fp, " SNMP vrrp %s", data->enable_snmp_vrrp ? "enabled" : "disabled");
#endif
#ifdef _WITH_SNMP_CHECKER_
	conf_write(fp, " SNMP checker %s", data->enable_snmp_checker ? "enabled" : "disabled");
#endif
#ifdef _WITH_SNMP_RFCV2_
	conf_write(fp, " SNMP RFCv2 %s", data->enable_snmp_rfcv2 ? "enabled" : "disabled");
#endif
#ifdef _WITH_SNMP_RFCV3_
	conf_write(fp, " SNMP RFCv3 %s", data->enable_snmp_rfcv3 ? "enabled" : "disabled");
#endif
#ifdef _WITH_SNMP_
	conf_write(fp, " SNMP traps %s", data->enable_traps ? "enabled" : "disabled");
	conf_write(fp, " SNMP socket = %s", data->snmp_socket ? data->snmp_socket : "default (unix:/var/agentx/master)");
#endif
#ifdef _WITH_DBUS_
	conf_write(fp, " DBus %s", data->enable_dbus ? "enabled" : "disabled");
	conf_write(fp, " DBus service name = %s", data->dbus_service_name ? data->dbus_service_name : "");
#endif
	conf_write(fp, " Script security %s", script_security ? "enabled" : "disabled");
	conf_write(fp, " Default script uid:gid %u:%u", default_script_uid, default_script_gid);
#ifdef _WITH_VRRP_
	conf_write(fp, " vrrp_netlink_cmd_rcv_bufs = %u", global_data->vrrp_netlink_cmd_rcv_bufs);
	conf_write(fp, " vrrp_netlink_cmd_rcv_bufs_force = %d", global_data->vrrp_netlink_cmd_rcv_bufs_force);
	conf_write(fp, " vrrp_netlink_monitor_rcv_bufs = %u", global_data->vrrp_netlink_monitor_rcv_bufs);
	conf_write(fp, " vrrp_netlink_monitor_rcv_bufs_force = %d", global_data->vrrp_netlink_monitor_rcv_bufs_force);
#ifdef _WITH_TRACK_PROCESS_
	conf_write(fp, " process_monitor_rcv_bufs = %u", global_data->process_monitor_rcv_bufs);
	conf_write(fp, " process_monitor_rcv_bufs_force = %d", global_data->process_monitor_rcv_bufs_force);
#endif
#endif
#ifdef _WITH_LVS_
	conf_write(fp, " lvs_netlink_cmd_rcv_bufs = %u", global_data->lvs_netlink_cmd_rcv_bufs);
	conf_write(fp, " lvs_netlink_cmd_rcv_bufs_force = %d", global_data->lvs_netlink_cmd_rcv_bufs_force);
	conf_write(fp, " lvs_netlink_monitor_rcv_bufs = %u", global_data->lvs_netlink_monitor_rcv_bufs);
	conf_write(fp, " lvs_netlink_monitor_rcv_bufs_force = %d", global_data->lvs_netlink_monitor_rcv_bufs_force);
	conf_write(fp, " rs_init_notifies = %d", global_data->rs_init_notifies);
	conf_write(fp, " no_checker_emails = %d", global_data->no_checker_emails);
#endif
#ifdef _WITH_VRRP_
	buf[0] = '\0';
	if (global_data->vrrp_rx_bufs_policy & RX_BUFS_POLICY_MTU)
		strcpy(buf, " rx_bufs_policy = MTU");
	else if (global_data->vrrp_rx_bufs_policy & RX_BUFS_POLICY_ADVERT)
		strcpy(buf, " rx_bufs_policy = ADVERT");
	else if (global_data->vrrp_rx_bufs_policy & RX_BUFS_SIZE)
		sprintf(buf, " rx_bufs_size = %zu", global_data->vrrp_rx_bufs_size);
	if (buf[0])
		conf_write(fp, "%s", buf);
	conf_write(fp, " rx_bufs_multiples = %d", global_data->vrrp_rx_bufs_multiples);
	conf_write(fp, " umask = 0%o", umask_val);
	if (global_data->vrrp_startup_delay)
		conf_write(fp, " vrrp_startup_delay = %g", global_data->vrrp_startup_delay / TIMER_HZ_DOUBLE);
	if (global_data->log_unknown_vrids)
		conf_write(fp, " log_unknown_vrids");
#ifdef _HAVE_VRRP_VMAC_
	if (global_data->vmac_prefix)
		conf_write(fp, " VMAC prefix = %s", global_data->vmac_prefix);
	if (global_data->vmac_addr_prefix)
		conf_write(fp, " VMAC address prefix = %s", global_data->vmac_addr_prefix);
#endif
#endif
	if ((val = get_cur_priority()))
		conf_write(fp, " current realtime priority = %u", val);
	if ((val = get_cur_rlimit_rttime()))
		conf_write(fp, " current realtime time limit = %u", val);
}
