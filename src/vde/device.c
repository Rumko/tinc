/*
    device.c -- VDE plug
    Copyright (C) 2011 Guus Sliepen <guus@tinc-vpn.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "system.h"

#include <libvdeplug_dyn.h>

#include "conf.h"
#include "net.h"
#include "logger.h"
#include "utils.h"
#include "route.h"
#include "xalloc.h"

int device_fd = -1;
static struct vdepluglib plug;
static struct vdeconn *conn = NULL;
static int port = 0;
static char *group = NULL;
char *device = NULL;
char *iface = NULL;
static char *device_info;

extern char *identname;
extern bool running;

static uint64_t device_total_in = 0;
static uint64_t device_total_out = 0;

bool setup_device(void) {
	libvdeplug_dynopen(plug);

	if(!plug.dl_handle) {
		logger(LOG_ERR, "Could not open libvdeplug library!");
		return false;
	}

	if(!get_config_string(lookup_config(config_tree, "Device"), &device))
		xasprintf(&device, LOCALSTATEDIR "/run/vde.ctl");

	get_config_string(lookup_config(config_tree, "Interface"), &iface);

	get_config_int(lookup_config(config_tree, "VDEPort"), &port);

	get_config_string(lookup_config(config_tree, "VDEGroup"), &group);

	device_info = "VDE socket";

	struct vde_open_args args = {
		.port = port,
		.group = group,
		.mode = 0700,
	};

	conn = plug.vde_open(device, identname, &args);
	if(!conn) {
		logger(LOG_ERR, "Could not open VDE socket %s", device);
		return false;
	}

	device_fd = plug.vde_datafd(conn);

	logger(LOG_INFO, "%s is a %s", device, device_info);

	if(routing_mode == RMODE_ROUTER)
		overwrite_mac = true;

	return true;
}

void close_device(void) {
	if(conn)
		plug.vde_close(conn);

	if(plug.dl_handle)
		libvdeplug_dynclose(plug);

	free(device);

	free(iface);
}

bool read_packet(vpn_packet_t *packet) {
	int lenin = plug.vde_recv(conn, packet->data, MTU, 0);
	if(lenin <= 0) {
		logger(LOG_ERR, "Error while reading from %s %s: %s", device_info, device, strerror(errno));
		running = false;
		return false;
	}

	packet->len = lenin;
	device_total_in += packet->len;
	ifdebug(TRAFFIC) logger(LOG_DEBUG, "Read packet of %d bytes from %s", packet->len, device_info);

	return true;
}

bool write_packet(vpn_packet_t *packet) {
	if(plug.vde_send(conn, packet->data, packet->len, 0) < 0) {
		if(errno != EINTR && errno != EAGAIN) {
			logger(LOG_ERR, "Can't write to %s %s: %s", device_info, device, strerror(errno));
			running = false;
		}

		return false;
	}

	device_total_out += packet->len;

	return true;
}

void dump_device_stats(void) {
	logger(LOG_DEBUG, "Statistics for %s %s:", device_info, device);
	logger(LOG_DEBUG, " total bytes in:  %10"PRIu64, device_total_in);
	logger(LOG_DEBUG, " total bytes out: %10"PRIu64, device_total_out);
}
