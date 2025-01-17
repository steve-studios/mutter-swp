/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-crtc.h"
#include "backends/native/meta-crtc-native.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-update.h"

#define META_TYPE_CRTC_KMS (meta_crtc_kms_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaCrtcKms, meta_crtc_kms,
                      META, CRTC_KMS,
                      MetaCrtcNative)

void meta_crtc_kms_apply_transform (MetaCrtcKms            *crtc_kms,
                                    MetaKmsPlaneAssignment *kms_plane_assignment);

MetaKmsPlaneAssignment * meta_crtc_kms_assign_primary_plane (MetaCrtcKms            *crtc_kms,
                                                             MetaDrmBuffer          *buffer,
                                                             MetaKmsUpdate          *kms_update,
                                                             MetaKmsAssignPlaneFlag  flags);

MetaKmsPlane * meta_crtc_kms_get_assigned_cursor_plane (MetaCrtcKms *crtc_kms);

void meta_crtc_kms_set_mode (MetaCrtcKms   *crtc_kms,
                             MetaKmsUpdate *kms_update);

void meta_crtc_kms_set_is_underscanning (MetaCrtcKms *crtc_kms,
                                         gboolean     is_underscanning);

META_EXPORT_TEST
MetaKmsCrtc * meta_crtc_kms_get_kms_crtc (MetaCrtcKms *crtc_kms);

GArray * meta_crtc_kms_get_modifiers (MetaCrtcKms *crtc_kms,
                                      uint32_t     format);

GArray *
meta_crtc_kms_copy_drm_format_list (MetaCrtcKms *crtc_kms);

gboolean
meta_crtc_kms_supports_format (MetaCrtcKms *crtc_kms,
                               uint32_t     drm_format);

const MetaGammaLut * meta_crtc_kms_peek_gamma_lut (MetaCrtcKms *crtc_kms);

MetaCrtcKms * meta_crtc_kms_from_kms_crtc (MetaKmsCrtc *kms_crtc);

MetaCrtcKms * meta_crtc_kms_new (MetaGpuKms  *gpu_kms,
                                 MetaKmsCrtc *kms_crtc);
