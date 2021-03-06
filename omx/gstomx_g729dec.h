/*
 * Copyright (C) 2007-2009 Nokia Corporation.
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef GSTOMX_G729DEC_H
#define GSTOMX_G729DEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_OMX_G729DEC(obj) (GstOmxG729Dec *) (obj)
#define GST_OMX_G729DEC_TYPE (gst_omx_g729dec_get_type ())

typedef struct GstOmxG729Dec GstOmxG729Dec;
typedef struct GstOmxG729DecClass GstOmxG729DecClass;

#include "gstomx_base_filter.h"

struct GstOmxG729Dec
{
    GstOmxBaseFilter omx_base;
};

struct GstOmxG729DecClass
{
    GstOmxBaseFilterClass parent_class;
};

GType gst_omx_g729dec_get_type (void);

G_END_DECLS

#endif /* GSTOMX_G729DEC_H */
