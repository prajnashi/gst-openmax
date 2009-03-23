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

#include "gstomx_mpeg4dec.h"
#include "gstomx.h"

/* open omx debug category */
GST_DEBUG_CATEGORY_EXTERN (gstomx_debug);
#define GST_OMX_CAT gstomx_debug

#ifdef BUILD_WITH_ANDROID
#define OMX_COMPONENT_NAME "OMX.PV.mpeg4dec"
#else
#define OMX_COMPONENT_NAME "OMX.st.video_decoder.mpeg4"
#endif

static GstOmxBaseVideoDecClass *parent_class = NULL;

static GstFlowReturn gst_omx_mpeg4dec_pad_chain (GstPad * pad, GstBuffer * buf);
static void gst_omx_mpeg4dec_dispose (GObject * obj);

static GstCaps *
generate_sink_template (void)
{
  GstCaps *caps;
  GstStructure *struc;

  caps = gst_caps_new_empty ();

  struc = gst_structure_new ("video/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1, NULL);

  gst_caps_append_structure (caps, struc);

  struc = gst_structure_new ("video/x-divx",
      "divxversion", GST_TYPE_INT_RANGE, 4, 5,
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1, NULL);

  gst_caps_append_structure (caps, struc);

  struc = gst_structure_new ("video/x-xvid",
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1, NULL);

  gst_caps_append_structure (caps, struc);

  struc = gst_structure_new ("video/x-3ivx",
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 100, 1, NULL);

  gst_caps_append_structure (caps, struc);

  return caps;
}

static void
type_base_init (gpointer g_class)
{
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  {
    GstElementDetails details;

    details.longname = "OpenMAX IL MPEG-4 video decoder";
    details.klass = "Codec/Decoder/Video";
    details.description = "Decodes video in MPEG-4 format with OpenMAX IL";
    details.author = "Felipe Contreras";

    gst_element_class_set_details (element_class, &details);
  }

  {
    GstPadTemplate *template;

    template = gst_pad_template_new ("sink", GST_PAD_SINK,
        GST_PAD_ALWAYS, generate_sink_template ());

    gst_element_class_add_pad_template (element_class, template);
  }
}

static gboolean
sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  GstStructure *s;
  GstOmxMpeg4Dec *omx_mpeg4dec;
  const GValue *v = NULL;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  gomx = (GOmxCore *) omx_base->gomx;
  omx_mpeg4dec = GST_OMX_MPEG4DEC (gst_pad_get_parent (pad));

  GST_INFO_OBJECT (omx_mpeg4dec, "Enter");

  /* get codec_data */
  s = gst_caps_get_structure (caps, 0);
  if (omx_mpeg4dec->codec_data != NULL) {
    gst_buffer_unref (omx_mpeg4dec->codec_data);
    omx_mpeg4dec->codec_data = NULL;
  }

  if ((v = gst_structure_get_value (s, "codec_data"))) {
    omx_mpeg4dec->codec_data = gst_buffer_ref (gst_value_get_buffer (v));
    GST_INFO_OBJECT (omx_mpeg4dec,
        "codec_data_length=%d", GST_BUFFER_SIZE (omx_mpeg4dec->codec_data));
  }

  GST_INFO_OBJECT (omx_mpeg4dec, "setcaps (sink): %" GST_PTR_FORMAT, caps);

  return gst_pad_set_caps (pad, caps);
}

static void
type_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) g_class;
  gstelement_class = (GstElementClass *) g_class;

  parent_class = g_type_class_ref (GST_OMX_BASE_VIDEODEC_TYPE);

  gobject_class->dispose = gst_omx_mpeg4dec_dispose;
}

static GstFlowReturn
gst_omx_mpeg4dec_pad_chain (GstPad * pad, GstBuffer * buf)
{
  GstOmxBaseFilter *omx_base;
  GstOmxMpeg4Dec *omx_mpeg4dec;
  GstFlowReturn result = GST_FLOW_ERROR;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  omx_mpeg4dec = GST_OMX_MPEG4DEC (gst_pad_get_parent (pad));

  GST_INFO_OBJECT (omx_mpeg4dec, "Enter");

  if (omx_mpeg4dec->base_chain_func) {
#ifdef BUILD_WITH_ANDROID
    /* send codec_date as the first frame in PV OpenMAX */
    if (omx_mpeg4dec->codec_data != NULL) {
      result = omx_mpeg4dec->base_chain_func (pad, omx_mpeg4dec->codec_data);
      GST_INFO_OBJECT (omx_mpeg4dec, "result: %s", gst_flow_get_name (result));
      gst_buffer_unref (omx_mpeg4dec->codec_data);
      omx_mpeg4dec->codec_data = NULL;
    }
#endif /* BUILD_WITH_ANDROID */
    if (buf != NULL) {
      result = omx_mpeg4dec->base_chain_func (pad, buf);
      GST_INFO_OBJECT (omx_mpeg4dec, "result: %s", gst_flow_get_name (result));
    }
  }

  return result;
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base_filter;
  GstOmxMpeg4Dec *omx_mpeg4dec;
  GstOmxBaseVideoDec *omx_base;

  omx_base_filter = GST_OMX_BASE_FILTER (instance);
  omx_base = GST_OMX_BASE_VIDEODEC (instance);

  omx_mpeg4dec = GST_OMX_MPEG4DEC (instance);
  GST_INFO_OBJECT (omx_mpeg4dec, "Enter");

  omx_base_filter->omx_component = g_strdup (OMX_COMPONENT_NAME);
  omx_base->compression_format = OMX_VIDEO_CodingMPEG4;

  gst_pad_set_setcaps_function (omx_base_filter->sinkpad, sink_setcaps);

  /* initialize mpeg4 decoder specific data */
  /* omx_mpeg4dec->is_first_frame = true; */
  omx_mpeg4dec->codec_data = NULL;
  omx_mpeg4dec->base_chain_func = NULL;

  /* replace base chain func */
  omx_mpeg4dec->base_chain_func = GST_PAD_CHAINFUNC (omx_base_filter->sinkpad);
  gst_pad_set_chain_function (omx_base_filter->sinkpad,
      gst_omx_mpeg4dec_pad_chain);
  GST_INFO_OBJECT (omx_mpeg4dec, "Leave");
}

static void
gst_omx_mpeg4dec_dispose (GObject * obj)
{
  GstOmxMpeg4Dec *omx_mpeg4dec;

  omx_mpeg4dec = GST_OMX_MPEG4DEC (obj);

  GST_INFO_OBJECT (omx_mpeg4dec, "Enter");

  if (omx_mpeg4dec->codec_data) {
    gst_buffer_unref (omx_mpeg4dec->codec_data);
    omx_mpeg4dec->codec_data = NULL;
  }
  omx_mpeg4dec->base_chain_func = NULL;
}

GType
gst_omx_mpeg4dec_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0)) {
    GTypeInfo *type_info;

    type_info = g_new0 (GTypeInfo, 1);
    type_info->class_size = sizeof (GstOmxMpeg4DecClass);
    type_info->base_init = type_base_init;
    type_info->class_init = type_class_init;
    type_info->instance_size = sizeof (GstOmxMpeg4Dec);
    type_info->instance_init = type_instance_init;

    type =
        g_type_register_static (GST_OMX_BASE_VIDEODEC_TYPE, "GstOmxMpeg4Dec",
        type_info, 0);

    g_free (type_info);
  }

  return type;
}
