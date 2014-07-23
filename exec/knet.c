/*
 * Copyright (c) 2014 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Fabio M. Di Nitto <fdinitto@redhat.com>
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>

#include <libtap.h>
#include <libknet.h>

#include "totemconfig.h"

#include <corosync/corotypes.h>
#include <corosync/corodefs.h>
#include <corosync/coroapi.h>
#include <corosync/logsys.h>
#include <corosync/icmap.h>

#include <net/if.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include <string.h>

#include "knet.h"

LOGSYS_DECLARE_SUBSYS ("KNET");

/*
 * global to see if knet is configured or not
 */
static int knet_enabled = 0;
static unsigned int node_id = 0;
static unsigned int node_pos = -1;

/*
 * global tap data
 */
static tap_t tap = NULL;
static char tap_name[2*IFNAMSIZ];
static size_t tap_name_size = IFNAMSIZ;
static int tap_fd = 0;

/*
 * global knet logging
 */
pthread_t knet_logging_thread;
static int knet_logfd[2];
static int knet_log_thread_started = 0;

/*
 * knet global bits
 */
static uint16_t knet_baseport = 60000; /* hardcode default? make it a define? */
static knet_handle_t knet_h = NULL;

/*
 * those are config values that CANNOT be changed at runtime
 * return:
 * 0 on success
 * -1 on error
 */
static int knet_read_config(void)
{
	char *value = NULL;

	if (icmap_get_string("knet.use_knet", &value) == CS_OK) {
		if ((strcmp (value, "on") == 0) || (strcmp (value, "yes") == 0)) {
			knet_enabled = 1;
		}
		free(value);
	}

	if (!knet_enabled) {
		return 0;
	}

	if (icmap_get_uint16("knet.baseport", &knet_baseport) != CS_OK) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "knet baseport has not been specified in corosync.conf");
		return -1;
	}
	/*
	 * store it in network format
	 */
	knet_baseport = htons(knet_baseport);

	if (icmap_get_string("knet.ifacename", &value) == CS_OK) {
		if (strlen(value) >= tap_name_size) {
			log_printf(LOGSYS_LEVEL_ERROR,
				   "knet ifacename too long. Max 15 chars allowed");
			free(value);
			return -1;
		}
		strncpy(tap_name, value, tap_name_size);
		free(value);
	} else {
		strncpy(tap_name, "cluster-net", tap_name_size);
	}

	return 0;
}

static int knet_find_nodeid(void)
{
	char tmp_key[ICMAP_KEYNAME_MAXLEN];

	if (totem_config_find_local_addr_in_nodelist("knet_ip", &node_pos) == 1) {
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", node_pos);
		if (icmap_get_uint32(tmp_key, &node_id) != CS_OK) {
			log_printf(LOGSYS_LEVEL_ERROR, "Unable to determine our node_id from nodelist");
			return -1;
		}
	} else {
		log_printf(LOGSYS_LEVEL_DEBUG, "Unable to find our node in nodelist");
		return -1;
	}

	return 0;
}

/*
 * return < 0 on error
 * tap fd > 0 on success
 */
static int tap_init(void)
{
	char tap_mac[18];
	char tap_ip[64];
	char *error_string = NULL;
	uint16_t net_node_id;
	uint8_t *nodeid = (uint8_t *)&net_node_id;
	uint8_t *bport = (uint8_t *)&knet_baseport;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char *value = NULL;

	log_printf(LOGSYS_LEVEL_DEBUG, "Opening tap device [%s]", tap_name);
	tap = tap_open(tap_name, tap_name_size, NULL);
	if (!tap) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to initialize tap device [%s], error: %s",
			   tap_name, strerror(errno));
		return -1;
	}

	/*
 	 * remember to give it a check re node_id 32bit vs knet node_id 16bit and mac
 	 * address generation here
 	 */ 
	net_node_id = htons(node_id);
	snprintf(tap_mac, sizeof(tap_mac) - 1, "54:54:%x:%x:%x:%x",
		 bport[0], bport[1], nodeid[0], nodeid[1]);
	log_printf(LOGSYS_LEVEL_DEBUG, "Setting mac [%s] on device [%s]",
		   tap_mac, tap_name);
	if (tap_set_mac(tap, tap_mac) < 0) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to set mac address [%s] on tap device [%s], error: %s",
			   tap_mac, tap_name, strerror(errno));
		return -1;
	}

	snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.ring0_addr", node_pos);
	if (icmap_get_string(tmp_key, &value) != CS_OK) {
		log_printf(LOGSYS_LEVEL_ERROR, "Unable to determine our ring0_addr from nodelist");
		return -1;
	}
	strncpy(tap_ip, value, sizeof(tap_ip) - 1);
	free(value);

	log_printf(LOGSYS_LEVEL_DEBUG, "Adding ip address [%s] on tap device [%s]",
		   tap_ip, tap_name);
	if (tap_add_ip(tap, tap_ip, "24", &error_string) < 0) {
		if (error_string) {
			log_printf(LOGSYS_LEVEL_ERROR,
				   "Unable to add ip address [%s] to tap device [%s], error: %s",
				   tap_ip, tap_name, error_string);
			free(error_string);
			error_string = NULL;
		} else {
			log_printf(LOGSYS_LEVEL_ERROR,
				   "Unable to add ip address [%s] to tap device [%s], error: %s",
				   tap_ip, tap_name, strerror(errno));
		}
		return -1;
	}

	return tap_get_fd(tap);
}

static int _fdset_cloexec(int fd)
{
	int fdflags;

	fdflags = fcntl(fd, F_GETFD, 0);
	if (fdflags < 0)
		return -1;

	fdflags |= FD_CLOEXEC;

	if (fcntl(fd, F_SETFD, fdflags) < 0)
		return -1;

	return 0;
}

static int _fdset_nonblock(int fd)
{
	int fdflags;

	fdflags = fcntl(fd, F_GETFL, 0);
	if (fdflags < 0)
		return -1;

	fdflags |= O_NONBLOCK;

	if (fcntl(fd, F_SETFL, fdflags) < 0)
		return -1;

	return 0;
}

static void *_handle_logging_thread(void *data)
{
	int logfd;
	int se_result = 0;
	fd_set rfds;
	struct timeval tv;

	memcpy(&logfd, data, sizeof(int));

	while (se_result >= 0){
		FD_ZERO (&rfds);
		FD_SET (logfd, &rfds);

		tv.tv_sec = 1;
		tv.tv_usec = 0;

		se_result = select(FD_SETSIZE, &rfds, 0, 0, &tv);

		if (se_result == -1)
			goto out;

		if (se_result == 0)
			continue;

		if (FD_ISSET(logfd, &rfds))  {
			struct knet_log_msg msg;
			size_t bytes_read = 0;
			size_t len;

			while (bytes_read < sizeof(struct knet_log_msg)) {
				len = read(logfd, &msg + bytes_read,
					   sizeof(struct knet_log_msg) - bytes_read);
				if (len <= 0) {
					break;
				}
				bytes_read += len;
			}

			if (bytes_read != sizeof(struct knet_log_msg))
				continue;

			switch(msg.msglevel) {
				case KNET_LOG_WARN:
					log_printf(LOGSYS_LEVEL_WARNING, "(%s) %s",
						   knet_log_get_subsystem_name(msg.subsystem), msg.msg);
					break;
				case KNET_LOG_INFO:
					log_printf(LOGSYS_LEVEL_INFO, "(%s) %s",
						   knet_log_get_subsystem_name(msg.subsystem), msg.msg);
					break;
				case KNET_LOG_DEBUG:
					log_printf(LOGSYS_LEVEL_DEBUG, "(%s) %s",
						   knet_log_get_subsystem_name(msg.subsystem), msg.msg);
					break;
				case KNET_LOG_ERR:
				default:
					log_printf(LOGSYS_LEVEL_ERROR, "(%s) %s",
						   knet_log_get_subsystem_name(msg.subsystem), msg.msg);
			}
		}
	}

out:
	return NULL;
}

static int knet_log_init(void)
{
	int res = 0;

	if (pipe(knet_logfd)) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to create knet logging pipe, error: %s",
			   strerror(errno));
		return -1;
	}

	if ((_fdset_cloexec(knet_logfd[0])) ||
	    (_fdset_nonblock(knet_logfd[0])) ||
	    (_fdset_cloexec(knet_logfd[1])) ||
	    (_fdset_nonblock(knet_logfd[1]))) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to set FD_CLOEXEX / O_NONBLOCK on knet logfd pipe, error %s",
			   strerror(errno));
		return -1;
	}

	res = pthread_create(&knet_logging_thread,
			     NULL, _handle_logging_thread,
			     (void *)&knet_logfd[0]);

	if (res) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to create knet logging thread, error: %s",
			   strerror(res));
		return -1;
	}

	knet_log_thread_started = 1;

	return 0;
}

/*
 * stole from linux kernel/include/linux/etherdevice.h
 */

static inline int is_zero_ether_addr(const uint8_t *addr)
{
	return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}

static inline int is_multicast_ether_addr(const uint8_t *addr)
{
	return 0x01 & addr[0];
}

static inline int is_broadcast_ether_addr(const uint8_t *addr)
{
	return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff;
}

static int ether_host_filter_fn (const unsigned char *outdata,
			  ssize_t outdata_len,
			  uint16_t src_host_id,
			  uint16_t *dst_host_ids,
			  size_t *dst_host_ids_entries)
{
	struct ether_header *eth_h = (struct ether_header *)outdata;
	uint8_t *dst_mac = (uint8_t *)eth_h->ether_dhost;
	uint16_t dst_host_id;

	if (is_zero_ether_addr(dst_mac))
		return -1;

	if (is_multicast_ether_addr(dst_mac) ||
	    is_broadcast_ether_addr(dst_mac)) {
		return 1;
	}

	memcpy(&dst_host_id, &dst_mac[4], 2);

	dst_host_ids[0] = ntohs(dst_host_id);
	*dst_host_ids_entries = 1;

	return 0;
}

static int knet_add_nodes_to_engine(unsigned int new_node_pos)
{
	int res = 0;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	char *value;
	uint32_t new_node_id = -1;
	char knet_host_name[KNET_MAX_HOST_LEN];
	uint16_t knet_host_id;

	snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", new_node_pos);
	if (icmap_get_uint32(tmp_key, &new_node_id) != CS_OK) {
		log_printf(LOGSYS_LEVEL_ERROR, "Unable to determine node_id from nodelist");
		return -1;
	}

	knet_host_id = new_node_id;

	res = knet_host_get_name_by_host_id(knet_h, knet_host_id, knet_host_name);
	if (res < 0) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to lookup knet host db, error: %s",
			   strerror(errno));
		return -1;
	}
	/*
	 * host not found, create one
	 */
	if (res == 0) {
		if (knet_host_add(knet_h, knet_host_id) < 0) {
			log_printf(LOGSYS_LEVEL_ERROR,
				   "Unable to add knet host [%u], error: %s",
				   knet_host_id, strerror(errno));
			return -1;
		}
		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.ring0_addr", new_node_pos);
		if (icmap_get_string(tmp_key, &value) != CS_OK) {
			log_printf(LOGSYS_LEVEL_ERROR,
				   "Unable to determine ring0_addr from nodelist");
			return -1;
		}
		if (knet_host_set_name(knet_h, knet_host_id, value) < 0) {
			free(value);
			log_printf(LOGSYS_LEVEL_ERROR,
				   "Unable to set knet hostname for nodeid [%u], error: %s",
				   knet_host_id, strerror(errno));
			return -1;
		}
		log_printf(LOGSYS_LEVEL_DEBUG,
			   "Added host [%s] with nodeid [%u] to knet host db",
			   value, knet_host_id);
		free(value);
	}
	/*
 	 * host is in db and with a name set to ring0_addr
 	 */

	/*
	 * TODO: add/update/delete links
	 */ 
	return 0;
}

static int knet_engine_remote_host_manage(void)
{
	int res = 0;
	icmap_iter_t iter;
	const char *iter_key;
	uint32_t tmp_node_pos = -1;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];

	iter = icmap_iter_init("nodelist.node.");

	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key, "nodelist.node.%u.%s", &tmp_node_pos, tmp_key);
		if (res != 2) {
			log_printf(LOGSYS_LEVEL_ERROR, "Error scanning nodelist");
			return -1;
		}

		if (tmp_node_pos == node_pos) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		if (knet_add_nodes_to_engine(tmp_node_pos) < 0) {
			log_printf(LOGSYS_LEVEL_ERROR, "Unable to add knet host");
			return -1;
		}
	}

	icmap_iter_finalize(iter);

	/*
	 * TODO, get knet host list and compare lists to remove
	 * nodes dynamically
	 */

	return 0;
}

static int knet_engine_init(void)
{
	uint16_t host_id = node_id;

	log_printf(LOGSYS_LEVEL_DEBUG, "Creating knet logging thread");
	if (knet_log_init() < 0) {
		log_printf(LOGSYS_LEVEL_ERROR, "Unable to create knet logging thread");
		return -1;
	}

	log_printf(LOGSYS_LEVEL_DEBUG, "Creating knet engine handle");
	knet_h = knet_handle_new(host_id, tap_fd, knet_logfd[1], KNET_LOG_DEBUG);
	if (!knet_h) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to create knet engine handle, error: %s",
			   strerror(errno));
		return -1;
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "knet engine handle created");

	log_printf(LOGSYS_LEVEL_DEBUG, "Installing knet ethernet packet filter");
	if (knet_handle_enable_filter(knet_h, ether_host_filter_fn) < 0) {
		log_printf(LOGSYS_LEVEL_ERROR,
			  "Unable to install knet ethernet packet filter, error: %s",
			  strerror(errno));
		return -1;
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "knet ethernet packet filter installed");

	log_printf(LOGSYS_LEVEL_DEBUG, "Initializing knet remote hosts");
	if (knet_engine_remote_host_manage() < 0) {
		log_printf(LOGSYS_LEVEL_ERROR,
			  "Unable to initialize knet remote hosts, error: %s",
			  strerror(errno));
		return -1;
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "knet remote hosts initialized");

	return 0;
}

static int knet_tap_bridge(void)
{
	log_printf(LOGSYS_LEVEL_DEBUG,
		   "Enabling knet engine traffic forwarding");
	if (knet_handle_setfwd(knet_h, 1) < 0) {
		log_printf(LOGSYS_LEVEL_ERROR,
			  "Unable to knet start traffic forwarding, error: %s",
			  strerror(errno));
		return -1;	
	}
	log_printf(LOGSYS_LEVEL_DEBUG,
		   "knet engine traffic forwarding enabled");

	log_printf(LOGSYS_LEVEL_DEBUG, "Setting tap device [%s] up", tap_name);
	if (tap_set_up(tap, NULL, NULL) < 0) {
		log_printf(LOGSYS_LEVEL_ERROR,
			   "Unable to set tap device [%s] up, error: %s",
			   tap_name, strerror(errno));
		return -1;
	}

	return 0;
}

int knet_init(const char **error_string)
{
	if (knet_read_config() < 0) {
		*error_string = "Unable to read knet config";
		return -1;
	}

	if (!knet_enabled) {
		log_printf(LOGSYS_LEVEL_DEBUG, "knet not enabled in config file");
		return 0;
	} else {
		log_printf(LOGSYS_LEVEL_INFO, "Initializing knet interface");
	}

	if (knet_find_nodeid() < 0) {
		*error_string = "Unable to find our node information";
		return -1;
	}

	log_printf(LOGSYS_LEVEL_DEBUG, "Initializing local tap interface");
	tap_fd = tap_init();
	if (tap_fd < 1) {
		*error_string = "Unable to init tap device";
		return -1;
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "local tap device initialization completed");

	log_printf(LOGSYS_LEVEL_DEBUG, "Initializing knet engine");
	if (knet_engine_init() < 0) {
		*error_string = "Unable to init knet engine";
		return -1;
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "knet engine initialization completed");

	log_printf(LOGSYS_LEVEL_DEBUG, "Start local tap/knet bridge");
	if (knet_tap_bridge() < 0) {
		*error_string = "Unable to start tap/knet bridge";
		return -1;
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "local tap/knet bridge started");

	log_printf(LOGSYS_LEVEL_INFO, "knet interface initialized");

	return 0;
}

/*
 * stop function can do better error report
 * not critical right now, since exit path is death of corosync
 */
static int knet_stop(void)
{
	uint16_t knet_host_ids[KNET_MAX_HOST];
	size_t knet_host_ids_entries = 0;

	if (knet_host_get_host_list(knet_h, knet_host_ids, &knet_host_ids_entries) == 0) {
		int i;

		for (i = 0; i < knet_host_ids_entries; i++) {
			knet_host_remove(knet_h, knet_host_ids[i]);
		}
	}

	knet_handle_setfwd(knet_h, 0);

	knet_handle_free(knet_h);
	return 0;
}

int knet_fini(const char **error_string)
{
	if (knet_h) {
		log_printf(LOGSYS_LEVEL_DEBUG, "Destroying knet engine");
		knet_stop();
		log_printf(LOGSYS_LEVEL_DEBUG, "knet engine destroyed");
	}

	if (knet_log_thread_started) {
		pthread_cancel(knet_logging_thread);
		close(knet_logfd[0]);
		close(knet_logfd[1]);
	}

	if (tap) {
		log_printf(LOGSYS_LEVEL_DEBUG, "Destroying local tap device");
		tap_close(tap);
		log_printf(LOGSYS_LEVEL_DEBUG, "local tap device destroyed");
	}

	return 0;
}
