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

#include "gstomx_h264dec.h"
#include "gstomx.h"
#include <string.h>

/* open omx debug category */
GST_DEBUG_CATEGORY_EXTERN (gstomx_debug);
#define GST_OMX_CAT gstomx_debug

#ifdef BUILD_WITH_ANDROID
#define OMX_COMPONENT_NAME "OMX.PV.avcdec"
#else
#define OMX_COMPONENT_NAME "OMX.st.video_decoder.avc"
#endif

static GstOmxBaseVideoDecClass *parent_class = NULL;

static GstFlowReturn gst_omx_h264dec_pad_chain (GstPad * pad, GstBuffer * buf);
static void gst_omx_h264dec_dispose (GObject * obj);

static GstCaps *
generate_sink_template (void)
{
  GstCaps *caps;
  GstStructure *struc;

  caps = gst_caps_new_empty ();

  struc = gst_structure_new ("video/x-h264",
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

    details.longname = "OpenMAX IL H.264/AVC video decoder";
    details.klass = "Codec/Decoder/Video";
    details.description = "Decodes video in H.264/AVC format with OpenMAX IL";
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
  GstOmxH264Dec *omx_h264dec;
  const GValue *v = NULL;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  gomx = (GOmxCore *) omx_base->gomx;
  omx_h264dec = GST_OMX_H264DEC (gst_pad_get_parent (pad));

  GST_INFO_OBJECT (omx_h264dec, "Enter");

  /* get codec_data */
  s = gst_caps_get_structure (caps, 0);
  if (omx_h264dec->codec_data != NULL) {
    gst_buffer_unref (omx_h264dec->codec_data);
    omx_h264dec->codec_data = NULL;
  }

  if ((v = gst_structure_get_value (s, "codec_data"))) {
    omx_h264dec->codec_data = gst_buffer_ref (gst_value_get_buffer (v));
    GST_INFO_OBJECT (omx_h264dec,
        "codec_data_length=%d", GST_BUFFER_SIZE (omx_h264dec->codec_data));
  }

  GST_INFO_OBJECT (omx_h264dec, "setcaps (sink): %" GST_PTR_FORMAT, caps);

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

  gobject_class->dispose = gst_omx_h264dec_dispose;
}

static GstFlowReturn
gst_omx_h264dec_pad_chain (GstPad * pad, GstBuffer * buf)
{
  GstOmxBaseFilter *omx_base;
  GstOmxH264Dec *omx_h264dec;
  GstFlowReturn result = GST_FLOW_ERROR;
  int bufsize = 0;
  int index = 0;
  guint size;
  guint8 *data;

  omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
  omx_h264dec = GST_OMX_H264DEC (gst_pad_get_parent (pad));

  GST_INFO_OBJECT (omx_h264dec, "Enter");

#ifdef BUILD_WITH_ANDROID
  /* split sequence parameter set and picture parameter set and other NALU */
  if (omx_h264dec->base_chain_func) {
    /* parse AVCDecoderConfigurationRecord */
    if (omx_h264dec->codec_data != NULL) {
      GstBuffer *SeqParabuf = NULL;
      GstBuffer *PicParabuf = NULL;
      int NumSeqPara = 0;
      int NumPicPara = 0;
      int LenSeqPara = 0;
      int LenPicPara = 0;
      int i;

      size = GST_BUFFER_SIZE (omx_h264dec->codec_data);
      data = GST_BUFFER_DATA (omx_h264dec->codec_data);

      /* Get sequence parameters from AVCDecoderConfigurationRecord 
       * and send them to OMX */
      index = 5;
      NumSeqPara = data[index] & 0x1f;
      GST_INFO_OBJECT (omx_h264dec, "index=%d, NumSeqPara=%d", index,
          NumSeqPara);
      index++;
      for (i = 0; i < NumSeqPara; i++) {
        LenSeqPara = (data[index] << 8) + data[index + 1];
        GST_INFO_OBJECT (omx_h264dec, "LenSeqPara=%d", LenSeqPara);
        index += 2;

        /* create a new buffer */
        SeqParabuf = gst_buffer_new_and_alloc (LenSeqPara + 4);

        /* To keep consistent with others buf, first 4 bytes are for length */
        memcpy (GST_BUFFER_DATA (SeqParabuf) + 4, data + index, LenSeqPara);

        result = omx_h264dec->base_chain_func (pad, SeqParabuf);
        GST_INFO_OBJECT (omx_h264dec, "result=%s, LenSeqPara=%d",
            gst_flow_get_name (result), LenSeqPara);

        /* SeqParabuf shall be released in chain func */
        index += LenSeqPara;
      }

      /* Get picture parameters from AVCDecoderConfigurationRecord 
       * and send them to OMX */
      NumPicPara = data[index];
      GST_INFO_OBJECT (omx_h264dec, "NumPicPara=%d", NumPicPara);
      index++;
      for (i = 0; i < NumPicPara; i++) {
        LenPicPara = (data[index] << 8) + data[index + 1];
        GST_INFO_OBJECT (omx_h264dec, "index=%d, LenPicPara=%d", index,
            LenPicPara);
        index += 2;

        /* create a new buffer */
        PicParabuf = gst_buffer_new_and_alloc (LenPicPara + 4);
        /* To keep consistent with others buf, first 4 bytes are for length */
        memcpy (GST_BUFFER_DATA (PicParabuf) + 4, data + index, LenPicPara);
        GST_BUFFER_TIMESTAMP (PicParabuf) =
            GST_BUFFER_TIMESTAMP (omx_h264dec->codec_data);

        result = omx_h264dec->base_chain_func (pad, PicParabuf);
        GST_INFO_OBJECT (omx_h264dec, "result=%s, LenPicPara=%d",
            gst_flow_get_name (result), LenPicPara);

        /* PicParabuf shall be released in chain func */
        index += LenPicPara;
      }

      gst_buffer_unref (omx_h264dec->codec_data);
      omx_h264dec->codec_data = NULL;

    }

    /* send NALU one by one to OMX */
    if (buf != NULL) {
      int length = 0;
      GstBuffer *NalUnitbuf = NULL;
      size = GST_BUFFER_SIZE (buf);
      data = GST_BUFFER_DATA (buf);
      index = 0;
      while (index < size) {
        length = (data[index] << 24) + (data[index + 1] << 16)
            + (data[index + 2] << 8) + (data[index + 3]);
        GST_INFO_OBJECT (omx_h264dec, "index=0x%x, length=0x%x", index, length);

        /* create a new buffer */
        NalUnitbuf = gst_buffer_new_and_alloc (length + 4);
        memcpy (GST_BUFFER_DATA (NalUnitbuf), data + index, length + 4);
        GST_BUFFER_TIMESTAMP (NalUnitbuf) = GST_BUFFER_TIMESTAMP (buf);
        result = omx_h264dec->base_chain_func (pad, NalUnitbuf);
        GST_INFO_OBJECT (omx_h264dec,
            "index=0x%x, length=0x%x, result=%s, buf_time=%" GST_TIME_FORMAT,
            index, length, gst_flow_get_name (result),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (NalUnitbuf)));
        index = index + 4 + length;
      }
      gst_buffer_unref (buf);
      buf = NULL;
    }
  }
#else
  result = omx_h264dec->base_chain_func (pad, buf);
#endif /* BUILD_WITH_ANDROID */

  return result;
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *omx_base_filter;
  GstOmxBaseVideoDec *omx_base;
  GstOmxH264Dec *omx_h264dec;

  omx_base_filter = GST_OMX_BASE_FILTER (instance);
  omx_base = GST_OMX_BASE_VIDEODEC (instance);

  omx_h264dec = GST_OMX_H264DEC (instance);
  GST_INFO_OBJECT (omx_h264dec, "Enter");

  omx_base_filter->omx_component = g_strdup (OMX_COMPONENT_NAME);
  omx_base->compression_format = OMX_VIDEO_CodingAVC;


  gst_pad_set_setcaps_function (omx_base_filter->sinkpad, sink_setcaps);

  /* initialize h264 decoder specific data */
  /* omx_h264dec->is_first_frame = true; */
  omx_h264dec->codec_data = NULL;
  omx_h264dec->base_chain_func = NULL;

  /* replace base chain func */
  omx_h264dec->base_chain_func = GST_PAD_CHAINFUNC (omx_base_filter->sinkpad);
  gst_pad_set_chain_function (omx_base_filter->sinkpad,
      gst_omx_h264dec_pad_chain);
  GST_INFO_OBJECT (omx_h264dec, "Leave");
}

static void
gst_omx_h264dec_dispose (GObject * obj)
{
  GstOmxH264Dec *omx_h264dec;

  omx_h264dec = GST_OMX_H264DEC (obj);

  GST_INFO_OBJECT (omx_h264dec, "Enter");

  if (omx_h264dec->codec_data) {
    gst_buffer_unref (omx_h264dec->codec_data);
    omx_h264dec->codec_data = NULL;
  }
  omx_h264dec->base_chain_func = NULL;
}


GType
gst_omx_h264dec_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0)) {
    GTypeInfo *type_info;

    type_info = g_new0 (GTypeInfo, 1);
    type_info->class_size = sizeof (GstOmxH264DecClass);
    type_info->base_init = type_base_init;
    type_info->class_init = type_class_init;
    type_info->instance_size = sizeof (GstOmxH264Dec);
    type_info->instance_init = type_instance_init;

    type =
        g_type_register_static (GST_OMX_BASE_VIDEODEC_TYPE, "GstOmxH264Dec",
        type_info, 0);

    g_free (type_info);
  }

  return type;
}
