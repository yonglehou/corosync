/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
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
 * - Neither the name of the Red Hat, Inc. nor the names of its
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

#include <sys/types.h>
#include <sys/socket.h>

#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <netdb.h>

#include "qdevice-config.h"
#include "qdevice-cmap.h"
#include "qdevice-log.h"
#include "qdevice-log-debug.h"
#include "qdevice-model.h"
#include "utils.h"

static uint32_t
qdevice_cmap_autogenerate_node_id(const char *addr, int clear_node_high_byte)
{
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	int ret, i;

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	/*
	 * Hardcoded AF_INET because autogenerated nodeid is valid only for ipv4
	 */
	ahints.ai_family = AF_INET;

	ret = getaddrinfo(addr, NULL, &ahints, &ainfo);
	if (ret != 0)
		return (0);

	if (ainfo->ai_family != AF_INET) {

		freeaddrinfo(ainfo);

		return (0);
	}

	memcpy(&i, &((struct sockaddr_in *)ainfo->ai_addr)->sin_addr, sizeof(struct in_addr));
	freeaddrinfo(ainfo);

	ret = htonl(i);

	if (clear_node_high_byte) {
		ret &= 0x7FFFFFFF;
	}

	return (ret);
}

int
qdevice_cmap_get_nodelist(cmap_handle_t cmap_handle, struct node_list *list)
{
	cs_error_t cs_err;
	cmap_iter_handle_t iter_handle;
	char key_name[CMAP_KEYNAME_MAXLEN + 1];
	char tmp_key[CMAP_KEYNAME_MAXLEN + 1];
	int res;
	int ret_value;
	unsigned int node_pos;
	uint32_t node_id;
	uint32_t data_center_id;
	char *tmp_str;
	char *addr0_str;
	int clear_node_high_byte;

	ret_value = 0;

	node_list_init(list);

	cs_err = cmap_iter_init(cmap_handle, "nodelist.node.", &iter_handle);
	if (cs_err != CS_OK) {
		return (-1);
	}

	while ((cs_err = cmap_iter_next(cmap_handle, iter_handle, key_name, NULL, NULL)) == CS_OK) {
		res = sscanf(key_name, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", node_pos);
		cs_err = cmap_get_uint32(cmap_handle, tmp_key, &node_id);

		if (cs_err == CS_ERR_NOT_EXIST) {
			/*
			 * Nodeid doesn't exists -> autogenerate node id
			 */
			clear_node_high_byte = 0;

			if (cmap_get_string(cmap_handle, "totem.clear_node_high_bit",
			    &tmp_str) == CS_OK) {
				if (strcmp (tmp_str, "yes") == 0) {
					clear_node_high_byte = 1;
				}

				free(tmp_str);
			}

			if (cmap_get_string(cmap_handle, key_name, &addr0_str) != CS_OK) {
				return (-1);
			}

			node_id = qdevice_cmap_autogenerate_node_id(addr0_str,
			    clear_node_high_byte);

			free(addr0_str);
		} else if (cs_err != CS_OK) {
			ret_value = -1;

			goto iter_finalize;
		}

		snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, "nodelist.node.%u.datacenterid", node_pos);
		if (cmap_get_uint32(cmap_handle, tmp_key, &data_center_id) != CS_OK) {
			data_center_id = 0;
		}

		if (node_list_add(list, node_id, data_center_id, TLV_NODE_STATE_NOT_SET) == NULL) {
			ret_value = -1;

			goto iter_finalize;
		}
	}

iter_finalize:
	cmap_iter_finalize(cmap_handle, iter_handle);

	if (ret_value != 0) {
		node_list_free(list);
	}

	return (ret_value);
}

int
qdevice_cmap_get_config_version(cmap_handle_t cmap_handle, uint64_t *config_version)
{
	int res;

	if (cmap_get_uint64(cmap_handle, "totem.config_version", config_version) == CS_OK) {
		res = 0;
	} else {
		*config_version = 0;
		res = -1;
	}

	return (res);
}

int
qdevice_cmap_store_config_node_list(struct qdevice_instance *instance)
{
	int res;

	node_list_free(&instance->config_node_list);

	if (qdevice_cmap_get_nodelist(instance->cmap_handle, &instance->config_node_list) != 0) {
		qdevice_log(LOG_ERR, "Can't get configuration node list.");

		return (-1);
	}

	res = qdevice_cmap_get_config_version(instance->cmap_handle, &instance->config_node_list_version);
	instance->config_node_list_version_set = (res == 0);

	return (0);
}

void
qdevice_cmap_init(struct qdevice_instance *instance)
{
	cs_error_t res;
	int no_retries;

	no_retries = 0;

	while ((res = cmap_initialize(&instance->cmap_handle)) == CS_ERR_TRY_AGAIN &&
	    no_retries++ < QDEVICE_MAX_CS_TRY_AGAIN) {
		(void)poll(NULL, 0, 1000);
	}

        if (res != CS_OK) {
		errx(1, "Failed to initialize the cmap API. Error %s", cs_strerror(res));
	}

	if ((res = cmap_context_set(instance->cmap_handle, (void *)instance)) != CS_OK) {
		errx(1, "Can't set cmap context. Error %s", cs_strerror(res));
	}

	cmap_fd_get(instance->cmap_handle, &instance->cmap_poll_fd);
}

static void
qdevice_cmap_node_list_event(struct qdevice_instance *instance)
{
	struct node_list nlist;
	int config_version_set;
	uint64_t config_version;

	qdevice_log(LOG_DEBUG, "Node list configuration possibly changed");
	if (qdevice_cmap_get_nodelist(instance->cmap_handle, &nlist) != 0) {
		qdevice_log(LOG_ERR, "Can't get configuration node list.");

		if (qdevice_model_get_config_node_list_failed(instance) != 0) {
			qdevice_log(LOG_DEBUG, "qdevice_model_get_config_node_list_failed returned error -> exit");
			exit(2);
		}

		return ;
	}

	config_version_set = (qdevice_cmap_get_config_version(instance->cmap_handle,
	    &config_version) == 0);

	if (node_list_eq(&instance->config_node_list, &nlist)) {
		return ;
	}

	qdevice_log(LOG_DEBUG, "Node list changed");
	if (config_version_set) {
		qdevice_log(LOG_DEBUG, "  config_version = "UTILS_PRI_CONFIG_VERSION, config_version);
	}
	qdevice_log_debug_dump_node_list(&nlist);

	if (qdevice_model_config_node_list_changed(instance, &nlist,
	    config_version_set, config_version) != 0) {
		qdevice_log(LOG_DEBUG, "qdevice_model_config_node_list_changed returned error -> exit");
		exit(2);
	}

	node_list_free(&instance->config_node_list);
	if (node_list_clone(&instance->config_node_list, &nlist) != 0) {
		qdevice_log(LOG_ERR, "Can't allocate instance->config_node_list clone");

		node_list_free(&nlist);

		if (qdevice_model_get_config_node_list_failed(instance) != 0) {
			qdevice_log(LOG_DEBUG, "qdevice_model_get_config_node_list_failed returned error -> exit");
			exit(2);
		}

		return ;
	}

	instance->config_node_list_version_set = config_version_set;

	if (config_version_set) {
		instance->config_node_list_version = config_version;
	}
}

static void
qdevice_cmap_logging_event(struct qdevice_instance *instance)
{

	qdevice_log(LOG_DEBUG, "Logging configuration possibly changed");
	qdevice_log_configure(instance);
}

static void
qdevice_cmap_reload_cb(cmap_handle_t cmap_handle, cmap_track_handle_t cmap_track_handle,
    int32_t event, const char *key_name,
    struct cmap_notify_value new_value, struct cmap_notify_value old_value,
    void *user_data)
{
	cs_error_t cs_res;
	uint8_t reload;
	struct qdevice_instance *instance;
	int node_list_event;
	int logging_event;
	const char *node_list_prefix_str;
	const char *logging_prefix_str;

	node_list_event = 0;
	logging_event = 0;
	node_list_prefix_str = "nodelist.";
	logging_prefix_str = "logging.";

	if (cmap_context_get(cmap_handle, (const void **)&instance) != CS_OK) {
		qdevice_log(LOG_ERR, "Fatal error. Can't get cmap context");
		exit(1);
	}

	/*
	 * Wait for full reload
	 */
	if (strcmp(key_name, "config.totemconfig_reload_in_progress") == 0 &&
	    new_value.type == CMAP_VALUETYPE_UINT8 && new_value.len == sizeof(reload)) {
		reload = 1;
		if (memcmp(new_value.data, &reload, sizeof(reload)) == 0) {
			/*
			 * Ignore nodelist changes
			 */
			instance->cmap_reload_in_progress = 1;
			return ;
		} else {
			instance->cmap_reload_in_progress = 0;
			node_list_event = 1;
			logging_event = 1;
		}
	}

	if (instance->cmap_reload_in_progress) {
		return ;
	}

	if (((cs_res = cmap_get_uint8(cmap_handle, "config.totemconfig_reload_in_progress",
	    &reload)) == CS_OK) && reload == 1) {
		return ;
	}

	if (strncmp(key_name, node_list_prefix_str, strlen(node_list_prefix_str)) == 0) {
		node_list_event = 1;
	}

	if (strncmp(key_name, logging_prefix_str, strlen(logging_prefix_str)) == 0) {
		logging_event = 1;
	}

	if (logging_event) {
		qdevice_cmap_logging_event(instance);
	}

	if (node_list_event) {
		qdevice_cmap_node_list_event(instance);
	}
}

int
qdevice_cmap_add_track(struct qdevice_instance *instance)
{
	cs_error_t res;

	res = cmap_track_add(instance->cmap_handle, "config.totemconfig_reload_in_progress",
	    CMAP_TRACK_ADD | CMAP_TRACK_MODIFY, qdevice_cmap_reload_cb,
	    NULL, &instance->cmap_reload_track_handle);

	if (res != CS_OK) {
		qdevice_log(LOG_ERR, "Can't initialize cmap totemconfig_reload_in_progress tracking");
		return (-1);
	}

	res = cmap_track_add(instance->cmap_handle, "nodelist.",
	    CMAP_TRACK_ADD | CMAP_TRACK_DELETE | CMAP_TRACK_MODIFY | CMAP_TRACK_PREFIX,
	    qdevice_cmap_reload_cb,
	    NULL, &instance->cmap_nodelist_track_handle);

	if (res != CS_OK) {
		qdevice_log(LOG_ERR, "Can't initialize cmap nodelist tracking");
		return (-1);
	}

	res = cmap_track_add(instance->cmap_handle, "logging.",
	    CMAP_TRACK_ADD | CMAP_TRACK_DELETE | CMAP_TRACK_MODIFY | CMAP_TRACK_PREFIX,
	    qdevice_cmap_reload_cb,
	    NULL, &instance->cmap_logging_track_handle);

	if (res != CS_OK) {
		qdevice_log(LOG_ERR, "Can't initialize logging tracking");
		return (-1);
	}

	return (0);
}

int
qdevice_cmap_del_track(struct qdevice_instance *instance)
{
	cs_error_t res;

	res = cmap_track_delete(instance->cmap_handle, instance->cmap_reload_track_handle);
	if (res != CS_OK) {
		qdevice_log(LOG_WARNING, "Can't delete cmap totemconfig_reload_in_progress tracking");
	}

	res = cmap_track_delete(instance->cmap_handle, instance->cmap_nodelist_track_handle);
	if (res != CS_OK) {
		qdevice_log(LOG_WARNING, "Can't delete cmap nodelist tracking");
	}

	res = cmap_track_delete(instance->cmap_handle, instance->cmap_logging_track_handle);
	if (res != CS_OK) {
		qdevice_log(LOG_WARNING, "Can't delete cmap logging tracking");
	}

	return (0);
}

void
qdevice_cmap_destroy(struct qdevice_instance *instance)
{
	cs_error_t res;

	res = cmap_finalize(instance->cmap_handle);

        if (res != CS_OK) {
		qdevice_log(LOG_WARNING, "Can't finalize cmap. Error %s", cs_strerror(res));
	}
}

int
qdevice_cmap_dispatch(struct qdevice_instance *instance)
{
	cs_error_t res;

	res = cmap_dispatch(instance->cmap_handle, CS_DISPATCH_ALL);

	if (res != CS_OK && res != CS_ERR_TRY_AGAIN) {
		qdevice_log(LOG_ERR, "Can't dispatch cmap messages");

		return (-1);
	}

	return (0);
}
