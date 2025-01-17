/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Matthew Allum
 * Copyright (C) 2007 Iain Holmes
 * Based on xcompmgr - (c) 2003 Keith Packard
 *          xfwm4    - (c) 2005-2007 Olivier Fourdan
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

#pragma once

#include "clutter/clutter.h"
#include "meta/compositor.h"
#include "meta/meta-window-actor.h"
#include "meta/types.h"

/* Public compositor API */
META_EXPORT
ClutterActor *meta_get_stage_for_display            (MetaDisplay *display);

META_EXPORT
GList        *meta_get_window_actors                (MetaDisplay *display);

META_EXPORT
ClutterActor *meta_get_window_group_for_display     (MetaDisplay *display);

META_EXPORT
ClutterActor *meta_get_top_window_group_for_display (MetaDisplay *display);

META_EXPORT
G_DEPRECATED_FOR (meta_compositor_get_feedback_group)
ClutterActor *meta_get_feedback_group_for_display   (MetaDisplay *display);

META_EXPORT
void meta_disable_unredirect_for_display (MetaDisplay *display);

META_EXPORT
void meta_enable_unredirect_for_display  (MetaDisplay *display);
