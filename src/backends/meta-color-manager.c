/*
 * Copyright (C) 2021 Jeremy Cline
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:meta-color-manager
 * @title: MetaColorManager
 * @short_description: Interfaces for managing color-related properties like
 *   color look-up tables and color spaces.
 *
 * Each MetaBackend has a MetaColorManager which includes interfaces for querying
 * and altering the color-related properties for displays associated with that
 * backend.
 *
 * These tasks include configuring the hardware's lookup tables (LUTs) used to
 * apply or remove transfer functions (traditionally called "gamma"), set up
 * color space conversions (CSCs), and for determining or setting the output
 * color space and transfer function.
 *
 * Mutter itself does not store and manage device ICC profiles; this task is
 * handled by [colord](https://www.freedesktop.org/software/colord/). Colord
 * maintains a database of devices (displays, printers, etc) and color profiles,
 * including the default output profile for a device. Users configure colord
 * with their preferred color profile for a device via an external application
 * like GNOME Control Center or the colormgr CLI.
 *
 * Colord defines [a specification for device and profile names](
 * https://github.com/hughsie/colord/blob/1.4.5/doc/device-and-profile-naming-spec.txt)
 * which is used to map Colord's devices to Mutter's #MetaMonitor.
 */

#include "config.h"

#include "backends/meta-color-manager-private.h"

#include "backends/meta-backend-types.h"
#include "backends/meta-color-device.h"
#include "backends/meta-monitor.h"

#include "meta-dbus-gsd-color.h"

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaColorManagerPrivate
{
  MetaBackend *backend;

  CdClient *cd_client;
  GCancellable *cancellable;

  GHashTable *devices;

  MetaDbusSettingsDaemonColor *gsd_color;
} MetaColorManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaColorManager, meta_color_manager, G_TYPE_OBJECT)

static char *
generate_monitor_id (MetaMonitor *monitor)
{
  const char *vendor;
  const char *product;
  const char *serial;
  GString *id;

  vendor = meta_monitor_get_vendor (monitor);
  product = meta_monitor_get_product (monitor);
  serial = meta_monitor_get_serial (monitor);
  if (!vendor && !product && !serial)
    return g_strdup (meta_monitor_get_connector (monitor));

  id = g_string_new ("");

  if (vendor)
    g_string_append_printf (id, "v:%s", vendor);
  if (product)
    g_string_append_printf (id, "%sp:%s", id->len > 0 ? ";" : "", product);
  if (serial)
    g_string_append_printf (id, "%sp:%s", id->len > 0 ? ";" : "", serial);

  return g_string_free (id, FALSE);
}

static void
update_devices (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (priv->backend);
  GList *l;
  GHashTable *devices;

  devices = g_hash_table_new_full (g_str_hash,
                                   g_str_equal,
                                   g_free,
                                   (GDestroyNotify) meta_color_device_destroy);
  for (l = meta_monitor_manager_get_monitors (monitor_manager); l; l = l->next)
    {
      MetaMonitor *monitor = META_MONITOR (l->data);
      g_autofree char *monitor_id = NULL;
      g_autofree char *stolen_monitor_id = NULL;
      MetaColorDevice *color_device;

      monitor_id = generate_monitor_id (monitor);

      if (priv->devices &&
          g_hash_table_steal_extended (priv->devices,
                                       monitor_id,
                                       (gpointer *) &stolen_monitor_id,
                                       (gpointer *) &color_device))
        {
          meta_topic (META_DEBUG_COLOR,
                      "Updating color device '%s' monitor instance",
                      meta_color_device_get_id (color_device));
          meta_color_device_update_monitor (color_device, monitor);
          g_hash_table_insert (devices,
                               g_steal_pointer (&monitor_id),
                               color_device);
        }
      else
        {
          color_device = meta_color_device_new (color_manager, monitor);
          meta_topic (META_DEBUG_COLOR,
                      "Created new color device '%s' for monitor %s",
                      meta_color_device_get_id (color_device),
                      meta_monitor_get_connector (monitor));
          g_hash_table_insert (devices,
                               g_steal_pointer (&monitor_id),
                               color_device);
        }
    }

  if (priv->devices)
    {
      if (g_hash_table_size (priv->devices) > 0)
        {
          meta_topic (META_DEBUG_COLOR, "Removing %u color devices",
                      g_hash_table_size (priv->devices));
        }
      g_clear_pointer (&priv->devices, g_hash_table_unref);
    }
  priv->devices = devices;
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaColorManager   *color_manager)
{
  update_devices (color_manager);
}

static void
cd_client_connect_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CdClient *client = CD_CLIENT (source_object);
  MetaColorManager *color_manager = META_COLOR_MANAGER (user_data);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (priv->backend);
  g_autoptr (GError) error = NULL;

  if (!cd_client_connect_finish (client, res, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to connect to colord daemon: %s", error->message);
      return;
    }

  if (!cd_client_get_has_server (client))
    {
      g_warning ("There is no colord server available");
      return;
    }

  update_devices (color_manager);
  g_signal_connect (monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (on_monitors_changed),
                    color_manager);
}

static void
on_gsd_color_ready (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (user_data);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);
  MetaDbusSettingsDaemonColor *gsd_color;
  g_autoptr (GError) error = NULL;

  gsd_color =
    meta_dbus_settings_daemon_color_proxy_new_for_bus_finish (res, &error);
  if (!gsd_color)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create gsd-color D-Bus proxy: %s", error->message);
      return;
    }

  meta_topic (META_DEBUG_COLOR,
              "Connection to org.gnome.SettingsDaemon.Color established");
  priv->gsd_color = gsd_color;
}

static void
meta_color_manager_constructed (GObject *object)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  priv->cancellable = g_cancellable_new ();

  priv->cd_client = cd_client_new ();
  cd_client_connect (priv->cd_client, priv->cancellable, cd_client_connect_cb,
                     color_manager);

  meta_dbus_settings_daemon_color_proxy_new_for_bus (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.SettingsDaemon.Color",
    "/org/gnome/SettingsDaemon/Color",
    priv->cancellable,
    on_gsd_color_ready,
    color_manager);
}

static void
meta_color_manager_finalize (GObject *object)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);
  g_clear_pointer (&priv->devices, g_hash_table_unref);
  g_clear_object (&priv->gsd_color);

  G_OBJECT_CLASS (meta_color_manager_parent_class)->finalize (object);
}

static void
meta_color_manager_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_color_manager_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_color_manager_class_init (MetaColorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_color_manager_constructed;
  object_class->finalize = meta_color_manager_finalize;
  object_class->set_property = meta_color_manager_set_property;
  object_class->get_property = meta_color_manager_get_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         "backend",
                         "MetaBackend",
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_color_manager_init (MetaColorManager *color_manager)
{
}

MetaBackend *
meta_color_manager_get_backend (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->backend;
}

CdClient *
meta_color_manager_get_cd_client (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->cd_client;
}