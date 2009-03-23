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

#include "gstomx_aacdec.h"
#include "gstomx_base_filter.h"
#include "gstomx.h"

#include <stdlib.h>             /* For calloc, free */
/* open omx debug category */
GST_DEBUG_CATEGORY_EXTERN (gstomx_debug);
#define GST_OMX_CAT gstomx_debug

#ifdef BUILD_WITH_ANDROID
#define OMX_COMPONENT_NAME "OMX.PV.aacdec"
#else
#define OMX_COMPONENT_NAME "OMX.st.audio_decoder.aac"
#endif

static GstOmxBaseFilterClass *parent_class = NULL;

static GstFlowReturn gst_omx_aacdec_pad_chain (GstPad * pad, GstBuffer * buf);
static void gst_omx_aacdec_dispose (GObject * obj);

static GstCaps *
generate_src_template (void)
{
  GstCaps *caps;

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "rate", GST_TYPE_INT_RANGE, 8000, 96000,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "channels", GST_TYPE_INT_RANGE, 1, 6, NULL);

  return caps;
}

static GstCaps *
generate_sink_template (void)
{
  GstCaps *caps;
  GstStructure *struc;

  caps = gst_caps_new_empty ();

  struc = gst_structure_new ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "rate", GST_TYPE_INT_RANGE, 8000, 96000,
      "channels", GST_TYPE_INT_RANGE, 1, 6, NULL);

  {
    GValue list;
    GValue val;

    list.g_type = val.g_type = 0;

    g_value_init (&list, GST_TYPE_LIST);
    g_value_init (&val, G_TYPE_INT);

    g_value_set_int (&val, 2);
    gst_value_list_append_value (&list, &val);

    g_value_set_int (&val, 4);
    gst_value_list_append_value (&list, &val);

    gst_structure_set_value (struc, "mpegversion", &list);

    g_value_unset (&val);
    g_value_unset (&list);
  }

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

    details.longname = "OpenMAX IL AAC audio decoder";
    details.klass = "Codec/Decoder/Audio";
    details.description = "Decodes audio in AAC format with OpenMAX IL";
    details.author = "Felipe Contreras";

    gst_element_class_set_details (element_class, &details);
  }

  {
    GstPadTemplate *template;

    template = gst_pad_template_new ("src", GST_PAD_SRC,
        GST_PAD_ALWAYS, generate_src_template ());

    gst_element_class_add_pad_template (element_class, template);
  }

  {
    GstPadTemplate *template;

    template = gst_pad_template_new ("sink", GST_PAD_SINK,
        GST_PAD_ALWAYS, generate_sink_template ());

    gst_element_class_add_pad_template (element_class, template);
  }
}

static void
type_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) g_class;
  gstelement_class = (GstElementClass *) g_class;
  parent_class = g_type_class_ref (GST_OMX_BASE_FILTER_TYPE);
  gobject_class->dispose = gst_omx_aacdec_dispose;
}

static void
settings_changed_cb (GOmxCore * core)
{
  GstOmxBaseFilter *omx_base;
  guint rate;
  guint channels;

  omx_base = core->client_data;

  GST_DEBUG_OBJECT (omx_base, "settings changed");

  {
    OMX_AUDIO_PARAM_PCMMODETYPE *param;

    param = calloc (1, sizeof (OMX_AUDIO_PARAM_PCMMODETYPE));
    param->nSize = sizeof (OMX_AUDIO_PARAM_PCMMODETYPE);
    param->nVersion.s.nVersionMajor = 1;
    param->nVersion.s.nVersionMinor = 1;

    param->nPortIndex = 1;
    OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamAudioPcm,
        param);

    rate = param->nSamplingRate;
    channels = param->nChannels;
    free (param);
  }
  GST_DEBUG_OBJECT (omx_base, "After OMX_GetParameter, rate=%d, channels=%d",
      rate, channels);

  {
    GstCaps *new_caps;

    new_caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, 16,
        "depth", G_TYPE_INT, 16,
        "rate", G_TYPE_INT, rate,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "channels", G_TYPE_INT, channels, NULL);

    GST_INFO_OBJECT (omx_base, "caps are: %" GST_PTR_FORMAT, new_caps);
    gst_pad_set_caps (omx_base->srcpad, new_caps);
  }
  GST_DEBUG_OBJECT (omx_base, "Leave");
}

static gboolean
sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstOmxBaseFilter *omx_base;
  GOmxCore *gomx;
  GstStructure *s;
  GstOmxAacDec *omx_aacdec;
  const GValue *v = NULL;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  gomx = (GOmxCore *) omx_base->gomx;
  omx_aacdec = GST_OMX_AACDEC (gst_pad_get_parent (pad));

  GST_INFO_OBJECT (omx_aacdec, "Enter");

  /* get codec_data to work with PV OpenMax in Android */
  s = gst_caps_get_structure (caps, 0);
  if (omx_aacdec->codec_data != NULL) {
    gst_buffer_unref (omx_aacdec->codec_data);
    omx_aacdec->codec_data = NULL;
  }

  if ((v = gst_structure_get_value (s, "codec_data"))) {
    omx_aacdec->codec_data = gst_buffer_ref (gst_value_get_buffer (v));
    GST_INFO_OBJECT (omx_aacdec,
        "codec_data_length=%d", GST_BUFFER_SIZE (omx_aacdec->codec_data));
  }

  GST_INFO_OBJECT (omx_aacdec, "setcaps (sink): %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  {
    const GValue *codec_data;
    GstBuffer *buffer;

    codec_data = gst_structure_get_value (structure, "codec_data");
    if (codec_data) {
      buffer = gst_value_get_buffer (codec_data);
      omx_base->codec_data = buffer;
      gst_buffer_ref (buffer);
    }
  }

  return gst_pad_set_caps (pad, caps);
}

static GstFlowReturn
gst_omx_aacdec_pad_chain (GstPad * pad, GstBuffer * buf)
{
  GstOmxBaseFilter *omx_base;
  GstOmxAacDec *omx_aacdec;
  GstFlowReturn result = GST_FLOW_ERROR;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  omx_aacdec = GST_OMX_AACDEC (gst_pad_get_parent (pad));

  GST_INFO_OBJECT (omx_aacdec, "Enter");

#ifdef BUILD_WITH_ANDROID
  /* 
   * put codec_data before the first frame to work with PV OpenMax in android 
   */
  if (omx_aacdec->codec_data != NULL && buf != NULL) {
    GstBuffer *newbuf = NULL;
    int new_buf_size =
        GST_BUFFER_SIZE (buf) + GST_BUFFER_SIZE (omx_aacdec->codec_data);

    GST_INFO_OBJECT (omx_aacdec,
        "Put codec_data before the first frame, buf_size=%d, codec_data_size=%d",
        GST_BUFFER_SIZE (buf), GST_BUFFER_SIZE (omx_aacdec->codec_data));

    /* create a new buffer */
    newbuf = gst_buffer_new_and_alloc (new_buf_size);

    /* copy meta data */
    gst_buffer_copy_metadata (newbuf, buf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS |
        GST_BUFFER_COPY_CAPS);

    /* copy codec data */
    memcpy (GST_BUFFER_DATA (newbuf),
        GST_BUFFER_DATA (omx_aacdec->codec_data),
        GST_BUFFER_SIZE (omx_aacdec->codec_data));

    /* copy the first frame */
    memcpy (GST_BUFFER_DATA (newbuf) + GST_BUFFER_SIZE (omx_aacdec->codec_data),
        GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

    /* release buf and codec_data */
    gst_buffer_unref (buf);
    gst_buffer_unref (omx_aacdec->codec_data);
    omx_aacdec->codec_data = NULL;

    buf = newbuf;
  }
#endif /* BUILD_WITH_ANDROID */

  if (omx_aacdec->base_chain_func)
    result = omx_aacdec->base_chain_func (pad, buf);

  GST_INFO_OBJECT (omx_aacdec, "Leave, result: %s", gst_flow_get_name (result));

  return result;
}


static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base;
  GstOmxAacDec *omx_aacdec;

  omx_base = GST_OMX_BASE_FILTER (instance);
  omx_aacdec = GST_OMX_AACDEC (instance);
  GST_INFO_OBJECT (omx_aacdec, "Enter");

  omx_base->omx_component = g_strdup (OMX_COMPONENT_NAME);

  omx_base->gomx->settings_changed_cb = settings_changed_cb;

  gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);

  /* initialize aac decoder specific data */
  omx_aacdec->codec_data = NULL;
  omx_aacdec->base_chain_func = NULL;

  /* replace base chain func */
  omx_aacdec->base_chain_func = GST_PAD_CHAINFUNC (omx_base->sinkpad);
  gst_pad_set_chain_function (omx_base->sinkpad, gst_omx_aacdec_pad_chain);
  GST_INFO_OBJECT (omx_aacdec, "Leave");
}

static void
gst_omx_aacdec_dispose (GObject * obj)
{
  GstOmxAacDec *omx_aacdec;

  omx_aacdec = GST_OMX_AACDEC (obj);

  GST_INFO_OBJECT (omx_aacdec, "Enter");

  if (omx_aacdec->codec_data) {
    gst_buffer_unref (omx_aacdec->codec_data);
    omx_aacdec->codec_data = NULL;
  }
  omx_aacdec->base_chain_func = NULL;
}


GType
gst_omx_aacdec_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0)) {
    GTypeInfo *type_info;

    type_info = g_new0 (GTypeInfo, 1);
    type_info->class_size = sizeof (GstOmxAacDecClass);
    type_info->base_init = type_base_init;
    type_info->class_init = type_class_init;
    type_info->instance_size = sizeof (GstOmxAacDec);
    type_info->instance_init = type_instance_init;

    type =
        g_type_register_static (GST_OMX_BASE_FILTER_TYPE, "GstOmxAacDec",
        type_info, 0);

    g_free (type_info);
  }

  return type;
}
