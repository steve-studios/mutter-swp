/*
 * Copyright 2013, 2018 Red Hat, Inc.
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
 *
 */

#include "config.h"

#include "backends/meta-cursor-sprite-xcursor.h"

#include "backends/meta-cursor.h"
#include "backends/meta-cursor-renderer.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "meta/prefs.h"
#include "meta/util.h"

struct _MetaCursorSpriteXcursor
{
  MetaCursorSprite parent;

  MetaCursor cursor;

  int current_frame;
  XcursorImages *xcursor_images;

  int theme_scale;
  gboolean theme_dirty;
  gboolean invalidated;
};

G_DEFINE_TYPE (MetaCursorSpriteXcursor, meta_cursor_sprite_xcursor,
               META_TYPE_CURSOR_SPRITE)

static const char *
translate_meta_cursor (MetaCursor cursor)
{
  switch (cursor)
    {
    case META_CURSOR_DEFAULT:
      return "default";
    case META_CURSOR_NORTH_RESIZE:
      return "n-resize";
    case META_CURSOR_SOUTH_RESIZE:
      return "s-resize";
    case META_CURSOR_WEST_RESIZE:
      return "w-resize";
    case META_CURSOR_EAST_RESIZE:
      return "e-resize";
    case META_CURSOR_SE_RESIZE:
      return "se-resize";
    case META_CURSOR_SW_RESIZE:
      return "sw-resize";
    case META_CURSOR_NE_RESIZE:
      return "ne-resize";
    case META_CURSOR_NW_RESIZE:
      return "nw-resize";
    case META_CURSOR_MOVE_OR_RESIZE_WINDOW:
      return "move";
    case META_CURSOR_BUSY:
      return "wait";
    case META_CURSOR_DND_IN_DRAG:
      return "no-drop";
    case META_CURSOR_DND_MOVE:
      return "grabbing";
    case META_CURSOR_DND_COPY:
      return "copy";
    case META_CURSOR_DND_UNSUPPORTED_TARGET:
      return "no-drop";
    case META_CURSOR_POINTING_HAND:
      return "pointer";
    case META_CURSOR_CROSSHAIR:
      return "crosshair";
    case META_CURSOR_IBEAM:
      return "text";
    case META_CURSOR_BLANK:
    case META_CURSOR_NONE:
    case META_CURSOR_LAST:
      break;
    }

  g_assert_not_reached ();
  return NULL;
}

static Cursor
create_blank_cursor (Display *xdisplay)
{
  Pixmap pixmap;
  XColor color;
  Cursor cursor;
  XGCValues gc_values;
  GC gc;

  pixmap = XCreatePixmap (xdisplay, DefaultRootWindow (xdisplay), 1, 1, 1);

  gc_values.foreground = BlackPixel (xdisplay, DefaultScreen (xdisplay));
  gc = XCreateGC (xdisplay, pixmap, GCForeground, &gc_values);

  XFillRectangle (xdisplay, pixmap, gc, 0, 0, 1, 1);

  color.pixel = 0;
  color.red = color.blue = color.green = 0;

  cursor = XCreatePixmapCursor (xdisplay, pixmap, pixmap, &color, &color, 1, 1);

  XFreeGC (xdisplay, gc);
  XFreePixmap (xdisplay, pixmap);

  return cursor;
}

static XcursorImages *
create_blank_cursor_images (void)
{
  XcursorImages *images;

  images = XcursorImagesCreate (1);
  images->images[0] = XcursorImageCreate (1, 1);

  images->images[0]->xhot = 0;
  images->images[0]->yhot = 0;
  memset (images->images[0]->pixels, 0, sizeof(int32_t));

  return images;
}

MetaCursor
meta_cursor_sprite_xcursor_get_cursor (MetaCursorSpriteXcursor *sprite_xcursor)
{
  return sprite_xcursor->cursor;
}

Cursor
meta_create_x_cursor (Display    *xdisplay,
                      MetaCursor  cursor)
{
  if (cursor == META_CURSOR_BLANK)
    return create_blank_cursor (xdisplay);

  return XcursorLibraryLoadCursor (xdisplay, translate_meta_cursor (cursor));
}

static XcursorImages *
load_cursor_on_client (MetaCursor cursor, int scale)
{
  XcursorImages *xcursor_images;
  int fallback_size, i;
  /* Set a 'default' fallback */
  MetaCursor cursors[] = { cursor, META_CURSOR_DEFAULT };

  if (cursor == META_CURSOR_BLANK)
    return create_blank_cursor_images ();

  for (i = 0; i < G_N_ELEMENTS (cursors); i++)
    {
      xcursor_images =
        XcursorLibraryLoadImages (translate_meta_cursor (cursors[i]),
                                  meta_prefs_get_cursor_theme (),
                                  meta_prefs_get_cursor_size () * scale);
      if (xcursor_images)
        return xcursor_images;
    }

  g_warning_once ("No cursor theme available, please install a cursor theme");

  fallback_size = 24 * scale;
  xcursor_images = XcursorImagesCreate (1);
  xcursor_images->images[0] = XcursorImageCreate (fallback_size, fallback_size);
  xcursor_images->images[0]->xhot = 0;
  xcursor_images->images[0]->yhot = 0;
  memset (xcursor_images->images[0]->pixels, 0xc0,
          fallback_size * fallback_size * sizeof (int32_t));
  return xcursor_images;
}

static void
load_from_current_xcursor_image (MetaCursorSpriteXcursor *sprite_xcursor)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (sprite_xcursor);
  XcursorImage *xc_image;
  int width, height, rowstride;
  CoglPixelFormat cogl_format;
  ClutterBackend *clutter_backend;
  CoglContext *cogl_context;
  CoglTexture *texture;
  GError *error = NULL;
  int hotspot_x, hotspot_y;

  g_assert (!meta_cursor_sprite_get_cogl_texture (sprite));

  xc_image = meta_cursor_sprite_xcursor_get_current_image (sprite_xcursor);
  width = (int) xc_image->width;
  height = (int) xc_image->height;
  rowstride = width * 4;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  cogl_format = COGL_PIXEL_FORMAT_BGRA_8888;
#else
  cogl_format = COGL_PIXEL_FORMAT_ARGB_8888;
#endif

  clutter_backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           width, height,
                                           cogl_format,
                                           rowstride,
                                           (uint8_t *) xc_image->pixels,
                                           &error);
  if (!texture)
    {
      g_warning ("Failed to allocate cursor texture: %s", error->message);
      g_error_free (error);
    }

  if (meta_is_wayland_compositor ())
    {
      hotspot_x = ((int) (xc_image->xhot / sprite_xcursor->theme_scale) *
                   sprite_xcursor->theme_scale);
      hotspot_y = ((int) (xc_image->yhot / sprite_xcursor->theme_scale) *
                   sprite_xcursor->theme_scale);
    }
  else
    {
      hotspot_x = xc_image->xhot;
      hotspot_y = xc_image->yhot;
    }
  meta_cursor_sprite_set_texture (sprite,
                                  texture,
                                  hotspot_x, hotspot_y);

  g_clear_object (&texture);
}

void
meta_cursor_sprite_xcursor_set_theme_scale (MetaCursorSpriteXcursor *sprite_xcursor,
                                            int                      theme_scale)
{
  if (sprite_xcursor->theme_scale != theme_scale)
    sprite_xcursor->theme_dirty = TRUE;
  sprite_xcursor->theme_scale = theme_scale;
}

static gboolean
meta_cursor_sprite_xcursor_is_animated (MetaCursorSprite *sprite)
{
  MetaCursorSpriteXcursor *sprite_xcursor = META_CURSOR_SPRITE_XCURSOR (sprite);

  return (sprite_xcursor->xcursor_images &&
          sprite_xcursor->xcursor_images->nimage > 1);
}

XcursorImage *
meta_cursor_sprite_xcursor_get_current_image (MetaCursorSpriteXcursor *sprite_xcursor)
{
  return sprite_xcursor->xcursor_images->images[sprite_xcursor->current_frame];
}

static void
meta_cursor_sprite_xcursor_tick_frame (MetaCursorSprite *sprite)
{
  MetaCursorSpriteXcursor *sprite_xcursor = META_CURSOR_SPRITE_XCURSOR (sprite);

  if (!meta_cursor_sprite_is_animated (sprite))
    return;

  sprite_xcursor->current_frame++;

  if (sprite_xcursor->current_frame >= sprite_xcursor->xcursor_images->nimage)
    sprite_xcursor->current_frame = 0;

  meta_cursor_sprite_clear_texture (sprite);
  load_from_current_xcursor_image (sprite_xcursor);
}

static unsigned int
meta_cursor_sprite_xcursor_get_current_frame_time (MetaCursorSprite *sprite)
{
  MetaCursorSpriteXcursor *sprite_xcursor = META_CURSOR_SPRITE_XCURSOR (sprite);
  XcursorImages *xcursor_images;

  g_return_val_if_fail (meta_cursor_sprite_is_animated (sprite), 0);

  xcursor_images = sprite_xcursor->xcursor_images;
  return xcursor_images->images[sprite_xcursor->current_frame]->delay;
}

static void
load_cursor_from_theme (MetaCursorSprite *sprite)
{
  MetaCursorSpriteXcursor *sprite_xcursor = META_CURSOR_SPRITE_XCURSOR (sprite);

  g_assert (sprite_xcursor->cursor != META_CURSOR_NONE);

  sprite_xcursor->theme_dirty = FALSE;

  /* We might be reloading with a different scale. If so clear the old data. */
  if (sprite_xcursor->xcursor_images)
    {
      meta_cursor_sprite_clear_texture (sprite);
      XcursorImagesDestroy (sprite_xcursor->xcursor_images);
    }

  sprite_xcursor->current_frame = 0;
  sprite_xcursor->xcursor_images =
    load_cursor_on_client (sprite_xcursor->cursor,
                           sprite_xcursor->theme_scale);

  load_from_current_xcursor_image (sprite_xcursor);
}

static gboolean
meta_cursor_sprite_xcursor_realize_texture (MetaCursorSprite *sprite)
{
  MetaCursorSpriteXcursor *sprite_xcursor = META_CURSOR_SPRITE_XCURSOR (sprite);
  gboolean retval = sprite_xcursor->invalidated;

  if (sprite_xcursor->theme_dirty)
    {
      load_cursor_from_theme (sprite);
      retval = TRUE;
    }

  sprite_xcursor->invalidated = FALSE;

  return retval;
}

static void
meta_cursor_sprite_xcursor_invalidate (MetaCursorSprite *sprite)
{
  MetaCursorSpriteXcursor *sprite_xcursor = META_CURSOR_SPRITE_XCURSOR (sprite);

  sprite_xcursor->invalidated = TRUE;
}

MetaCursorSpriteXcursor *
meta_cursor_sprite_xcursor_new (MetaCursor         cursor,
                                MetaCursorTracker *cursor_tracker)
{
  MetaCursorSpriteXcursor *sprite_xcursor;

  sprite_xcursor = g_object_new (META_TYPE_CURSOR_SPRITE_XCURSOR,
                                 "cursor-tracker", cursor_tracker,
                                 NULL);
  sprite_xcursor->cursor = cursor;

  return sprite_xcursor;
}

static void
meta_cursor_sprite_xcursor_finalize (GObject *object)
{
  MetaCursorSpriteXcursor *sprite_xcursor = META_CURSOR_SPRITE_XCURSOR (object);

  g_clear_pointer (&sprite_xcursor->xcursor_images,
                   XcursorImagesDestroy);

  G_OBJECT_CLASS (meta_cursor_sprite_xcursor_parent_class)->finalize (object);
}

static void
meta_cursor_sprite_xcursor_init (MetaCursorSpriteXcursor *sprite_xcursor)
{
  sprite_xcursor->theme_scale = 1;
  sprite_xcursor->theme_dirty = TRUE;
}

static void
meta_cursor_sprite_xcursor_class_init (MetaCursorSpriteXcursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCursorSpriteClass *cursor_sprite_class = META_CURSOR_SPRITE_CLASS (klass);

  object_class->finalize = meta_cursor_sprite_xcursor_finalize;

  cursor_sprite_class->realize_texture =
    meta_cursor_sprite_xcursor_realize_texture;
  cursor_sprite_class->invalidate =
    meta_cursor_sprite_xcursor_invalidate;
  cursor_sprite_class->is_animated = meta_cursor_sprite_xcursor_is_animated;
  cursor_sprite_class->tick_frame = meta_cursor_sprite_xcursor_tick_frame;
  cursor_sprite_class->get_current_frame_time =
    meta_cursor_sprite_xcursor_get_current_frame_time;
}
