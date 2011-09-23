/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/**
 * \file plugin_usb.c
 * \brief USB interface source.
 *
 * Copyright (C) 2010 Signove Tecnologia Corporation.
 * All rights reserved.
 * Contact: Signove Tecnologia Corporation (contact@signove.com)
 *
 * $LICENSE_TEXT:BEGIN$
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation and appearing
 * in the file LICENSE included in the packaging of this file; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 * $LICENSE_TEXT:END$
 *
 * \author Elvis Pfutzenreuter
 * \date Mar 25, 2011
 */

/**
 * @addtogroup Communication
 */

/*
	TODO
	* after initial search, react to plugged devices
	* do not fail if initial search returns nothing
	* handle multiple USB devices
	* handle unplugging
	* specific TODOs throughout the code
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <glib.h>
#include "src/communication/plugin/plugin.h"
#include "src/communication/communication.h"
#include "src/util/log.h"
#include "plugin_usb.h"
#include "usb_phdc_drive.h"

static char *current_data = NULL;
static int data_len = 0;

usb_phdc_context *phdc_context = NULL;

typedef struct device_object {
	usb_phdc_device *impl;
	char *addr;
} device_object;

typedef struct channel_object {
	// TODO is there a real differentiation between channel and device?
	usb_phdc_device *impl;
	usb_phdc_device *device;
	guint64 handle;
	GSList *gios;
} channel_object;

static GSList *devices = NULL;
static GSList *channels = NULL;
guint64 last_handle = 0;

// static GSList *apps = NULL;
static PluginUsbListener *listener = NULL;

// static int send_data(guint64 handle, unsigned char *data, int len);

static int init();
static int finalize();
static ByteStreamReader *get_apdu(struct Context *ctx);
static int send_apdu_stream(struct Context *ctx, ByteStreamWriter *stream);

/**
 * Callback called from USB layer, when device connects (BT-wise)
 *
 * @param handle Connection handler
 * @param device Device connected
 */
void device_connected(guint64 handle, const char *device)
{
	if (listener) {
		listener->agent_connected(handle, device);
	}
	communication_transport_connect_indication(handle);
}

/**
 * Callback called from USB layer when device disconnects (BT-wise)
 *
 * @param handle
 * @param device
 */
static void device_disconnected(guint64 handle, const char *device)
{
	communication_transport_disconnect_indication(handle);
	if (listener) {
		listener->agent_disconnected(handle, device);
	}
}


void plugin_usb_setup(CommunicationPlugin *plugin)
{
	plugin->network_init = init;
	plugin->network_get_apdu_stream = get_apdu;
	plugin->network_send_apdu_stream = send_apdu_stream;
	plugin->network_finalize = finalize;
}

/**
 * Sets a listener to event of this plugin
 * @param plugin
 */
void plugin_usb_set_listener(PluginUsbListener *plugin)
{
	listener = plugin;
}

/**
 * Fetch device struct from proxy list
 */
static device_object *get_device_object(const usb_phdc_device *impl)
{
	GSList *i;
	device_object *m;

	for (i = devices; i; i = i->next) {
		m = i->data;

		if (m->impl == impl)
			return m;
	}

	return NULL;
}


/**
 * Remove device from device proxy list
 */
static void remove_device(const usb_phdc_device *impl)
{
	device_object *dev = get_device_object(impl);

	if (dev) {
		g_free(dev->addr);
		g_free(dev);
		devices = g_slist_remove(devices, dev);
	}
}


/**
 * Get device address
 * Returns unknonwn on failure
 */
static char *get_device_addr(const usb_phdc_device *impl)
{
	// TODO return real address
	return g_strdup("00:12:34:56:78:9A");
}


/**
 * Add device into device proxy list
 */
static void add_device(usb_phdc_device *impl)
{
	device_object *dev;

	if (get_device_object(impl)) {
		remove_device(impl);
	}

	dev = g_new(device_object, 1);
	dev->addr = get_device_addr(impl);
	dev->impl = impl;

	devices = g_slist_prepend(devices, dev);
}


/**
 * Fetch channel struct from list
 */
static channel_object *get_channel(const usb_phdc_device *impl)
{
	GSList *i;
	channel_object *m;

	for (i = channels; i; i = i->next) {
		m = i->data;

		if (m->impl == impl)
			return m;
	}

	return NULL;

}


/**
 * Fetch channel struct from list by handle
 */
static channel_object *get_channel_by_handle(guint64 handle)
{
	GSList *i;
	channel_object *m;

	for (i = channels; i; i = i->next) {
		m = i->data;

		if (m->handle == handle)
			return m;
	}

	return NULL;

}

/**
 * Remove channel id from channel proxy list
 */
static void remove_channel(const usb_phdc_device *impl)
{
	channel_object *c = get_channel(impl);

	if (c) {
		while (c->gios) {
			GIOChannel *gio = c->gios->data;
			g_io_channel_unref(gio);
			c->gios = g_slist_remove(c->gios, gio);
		}

		g_free(c);
		channels = g_slist_remove(channels, c);
	}
}

static void channel_closed(usb_phdc_device *impl);

/**
 * USB callback
 */
static gboolean usb_event_received(GIOChannel *gio, GIOCondition cond, gpointer dev)
{
	poll_phdc_device_post(dev);
	// reschedule next read (FIXME is this really necessary?)
	poll_phdc_device_pre(dev);

	return TRUE;
}

static void data_received(usb_phdc_device *dev, unsigned char *buf, int len)
{
	if (len <= 0) {
		ERROR("Data length <= 0");
		return;
	}

	DEBUG("Recv: %d bytes", len);

	if (current_data) {
		free(current_data);
		current_data = NULL;
	}

	data_len = len;
	current_data = malloc(len + 1);
	memcpy(current_data, buf, len);
	current_data[len] = '\0';

	channel_object *c = get_channel(dev);

	if (!c)	{
		ERROR("Unknown channel");
		return;
	}

	communication_read_input_stream(context_get((ContextId)c->handle));
}

static void data_error_received(usb_phdc_device *dev)
{
	channel_closed(dev);
}

/**
 * Add channel into channel proxy list
 */

static guint64 add_channel(usb_phdc_device *impl, usb_phdc_device *device)
{
	channel_object *c;
	GIOChannel *gio;
	GSList *gios = NULL;
	int i;

	if (get_channel(impl))
		remove_channel(impl);

	for (i = 0; i < device->fds_count; ++i) {
		gio = g_io_channel_unix_new(device->fds[i].fd);

		g_io_add_watch(gio,
			((device->fds[i].events & POLLIN) ? G_IO_IN : 0) |
			((device->fds[i].events & POLLOUT) ? G_IO_OUT : 0) |
			((device->fds[i].events & POLLPRI) ? G_IO_PRI : 0) |
			((device->fds[i].events & POLLERR) ? G_IO_ERR : 0) |
			((device->fds[i].events & POLLHUP) ? G_IO_HUP : 0) |
			((device->fds[i].events & POLLNVAL) ? G_IO_NVAL : 0),
			usb_event_received,
			device);

		gios = g_slist_prepend(gios, gio);
	}

	c = (channel_object *) g_new(channel_object, 1);
	c->impl = impl;
	c->device = device;
	c->handle = ++last_handle;
	c->gios = gios;

	channels = g_slist_prepend(channels, c);

	return c->handle;
}

static int disconnect_channel(guint64 handle);

/**
 * Takes care of channel closure
 */
static void channel_closed(struct usb_phdc_device *impl)
{
	channel_object *c = get_channel(impl);
	device_object *d;

	if (!c)
		return;

	d = get_device_object(c->device);

	// notifies higher layers
	if (d) {
		device_disconnected(c->handle, d->addr);
	} else {
		ERROR("Unknown device");
		device_disconnected(c->handle, "unknown");
	}

	disconnect_channel(c->handle);
}


/**
 * Forces closure of all open channels
 */
static void disconnect_all_channels()
{
	channel_object *chan;

	while (channels) {
		chan = channels->data;
		disconnect_channel(chan->handle);
	}
}

static void channel_connected(usb_phdc_device *dev, usb_phdc_device *impl);

/**
 * Starts Health link with USB
 *
 * @return success status
 */
static int init()
{
	DEBUG("Starting USB...");

	phdc_context = (usb_phdc_context *) calloc(1, sizeof(usb_phdc_context));

	init_phdc_usb_plugin(phdc_context);
	search_phdc_devices(phdc_context);

	if (phdc_context->number_of_devices > 0) {
		// Get the first device to read measurements
		usb_phdc_device *usbdev = &(phdc_context->device_list[0]);
		add_device(usbdev);

		usbdev->data_read_cb = data_received;
		usbdev->error_read_cb = data_error_received;

		print_phdc_info(usbdev);

		if (open_phdc_handle(usbdev) == 1) {
			channel_connected(usbdev, usbdev);
			poll_phdc_device_pre(usbdev);
			return NETWORK_ERROR_NONE;
		}
	}

	return NETWORK_ERROR;
}


/**
 * Does memory cleanup after USB link stopped.
 * This is made as a separate function because main loop must hava a chance
 * to handle stopping before objects are destroyed.
 */
static gboolean cleanup(gpointer data)
{
	// TODO stop USB listening
	disconnect_all_channels();

	g_free(current_data);
	current_data = NULL;

	release_phdc_resources(phdc_context);

	return FALSE;
}


/**
 * Stops USB link. Link may be restarted again afterwards.
 *
 * @return success status
 */
static int finalize()
{
	DEBUG("Stopping USB link...");

	g_idle_add(cleanup, NULL);

	return NETWORK_ERROR_NONE;
}


/**
 * Reads an APDU from buffer
 *
 * @return a byteStream with the read APDU.
 */
static ByteStreamReader *get_apdu(struct Context *ctx)
{
	guchar *buffer;
	DEBUG("\nUSB: get APDU stream");

	// Create bytestream
	buffer = malloc(data_len);
	memcpy(buffer, (unsigned char *) current_data, data_len);

	ByteStreamReader *stream = byte_stream_reader_instance(buffer, data_len);

	if (stream == NULL) {
		ERROR("\n network:usb Error creating bytelib");
		return NULL;
	}

	DEBUG(" network:usb APDU received ");

	return stream;
}

/**
 * Sends IEEE data to USB
 *
 * @return success status
 */
static int send_apdu_stream(struct Context *ctx, ByteStreamWriter *stream)
{
	DEBUG("\nSend APDU");

	channel_object *c = get_channel_by_handle(ctx->id);

	if (c) {
		return usb_send_apdu(c->impl, stream->buffer, stream->size);
	} else {
		return 0;
	}
}

/**
 * Forces closure of a channel
 */
static int disconnect_channel(guint64 handle)
{
	channel_object *c = get_channel_by_handle(handle);

	if (c) {
		DEBUG("removing channel");
		remove_channel(c->impl);
		return 1;
	} else {
		DEBUG("unknown handle/channel");
		return 0;
	}
}

static void channel_connected(usb_phdc_device *device, usb_phdc_device *impl)
{
	guint64 handle = add_channel(impl, device);
	device_object *dev = get_device_object(device);

	if (dev) {
		device_connected(handle, dev->addr);
	} else {
		ERROR("Channel from unknown device");
		device_connected(handle, "unknown");
	}
}
