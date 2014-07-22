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

#include "knet.h"

LOGSYS_DECLARE_SUBSYS ("KNET");

/*
 * global to see if knet is configured or not
 */
static int knet_enabled = 0;
static unsigned int node_id = 0;
static unsigned int node_pos = -1;

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

	return 0;
}

int knet_fini(const char **error_string)
{
	return 0;
}
