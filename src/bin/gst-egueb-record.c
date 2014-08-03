/* Generate an iamge for non animatable egueb files or infinite duration;
 * and videos for finite duration
 */
#include <gst/gst.h>

static GMainLoop *main_loop;
static GstElement *pipeline;
static int encoder = -1;
static char *outname = NULL;

typedef enum _Gst_Egueb_Record_Encoder_Type
{
	ENCODER_TYPE_IMAGE,
	ENCODER_TYPE_VIDEO,
	ENCODER_TYPE_INTERACTIVE,
} Gst_Egueb_Record_Encoder_Type;

typedef struct _Gst_Egueb_Record_Encoder
{
	Gst_Egueb_Record_Encoder_Type type;
	const char *name;
	const char *sink;
} Gst_Egueb_Record_Encoder;

Gst_Egueb_Record_Encoder encoders[] = {
	{ ENCODER_TYPE_IMAGE, "png", "ffmpegcolorspace ! pngenc snapshot=true ! filesink location=%s.png" },
	{ ENCODER_TYPE_VIDEO, "theora", "ffmpegcolorspace ! theoraenc ! oggmux ! filesink location=%s.ogg" },
};

static void pipeline_eos_cb(GstBus *bus, GstMessage *msg, gpointer *data)
{
	printf("eos received\n");
	gst_element_set_state(pipeline, GST_STATE_READY);
	g_main_loop_quit(main_loop);
}

static void pipeline_error_cb(GstBus *bus, GstMessage *msg, gpointer *data)
{
	GError *err;
	gchar *debug_info;

	/* Print error details on the screen */
	gst_message_parse_error (msg, &err, &debug_info);
	g_printerr("ERROR: Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
	g_printerr("ERROR: Debugging information: %s\n", debug_info ? debug_info : "none");
	g_clear_error(&err);
	g_free(debug_info);

	gst_element_set_state(pipeline, GST_STATE_READY);
	g_main_loop_quit(main_loop);
}

static gboolean eguebdemux_buffer_probe_cb(GstPad * pad, GstBuffer * buffer, gpointer user_data)
{
	GstElement *sink;
	GstPad *sinkpad;
	GError *err = NULL;
	gchar *sink_str;
	static first_buffer = TRUE;

	if (!first_buffer) return TRUE;

	/* create the sink branch */
	encoder = 1;
	sink_str = g_strdup_printf(encoders[encoder].sink, outname);
	sink = gst_parse_bin_from_description(sink_str, TRUE, &err);
	if (!sink)
	{
		g_printerr("ERROR: Impossible to instantiate the sink '%s'\n", sink_str);
		g_free(sink_str);
		g_main_loop_quit(main_loop);
		return FALSE;
	}
	g_free(sink_str);

	gst_bin_add(GST_BIN(pipeline), gst_object_ref(sink));

	/* link the sink branch */
	sinkpad = gst_element_get_static_pad(sink, "sink");
	gst_pad_link(pad, sinkpad);

	gst_object_unref(sinkpad);
	gst_element_sync_state_with_parent(sink);
	gst_object_unref(sink);
	first_buffer = FALSE;

	return TRUE;
}

static void uridecodebin_pad_added_cb(GstElement *src, GstPad *pad, gpointer *data)
{
	GstElement *demuxer;
	GstPad *sinkpad, *srcpad;

	demuxer = gst_element_factory_make("eguebdemux", NULL);
	sinkpad = gst_element_get_static_pad(demuxer, "sink");
	srcpad = gst_element_get_static_pad(demuxer, "video");

	gst_bin_add(GST_BIN(pipeline), gst_object_ref(demuxer));
	/* link the demuxer */ 
	gst_pad_link(pad, sinkpad);
	gst_object_unref(sinkpad);

	/* TODO on the first buffer sent out from the demuxer, check the duration, and create the best
	 * sink based on it
	 */
	gst_pad_add_buffer_probe(srcpad, G_CALLBACK (eguebdemux_buffer_probe_cb), NULL);
	srcpad = gst_element_get_static_pad(demuxer, "video");
	gst_object_unref(srcpad);
	gst_element_sync_state_with_parent(demuxer);
	gst_object_unref(demuxer);

}

int main(int argc, char **argv)
{
	/* options */
	gchar *uri = NULL;
	GstElement *element;
	GstCaps *caps;
	GstBus *bus;
	GOptionContext *ctx;
	GOptionEntry options[] = {
		{"encoder", 'e', 0, G_OPTION_ARG_INT, &encoder,
				"The encoder to use", "encoder"},
		{NULL}
	};
	GError *err = NULL;

	ctx = g_option_context_new("URI FILE");
	g_option_context_add_main_entries(ctx, options, NULL);
	g_option_context_add_group(ctx, gst_init_get_option_group ());
	if (!g_option_context_parse(ctx, &argc, &argv, &err))
	{
		if (err)
			g_printerr("ERROR: Initializing: %s\n", GST_STR_NULL(err->message));
		else
			g_printerr("ERROR: Initializing: Unknown error!\n");
		return 1;
	}
	g_option_context_free(ctx);

	if (argc < 3)
	{
		g_printerr("ERROR: No URI specified\n");
		return 1;
	}
	uri = argv[1];
	outname = argv[2];

	gst_init (&argc, &argv);
	main_loop = g_main_loop_new(NULL, FALSE);
	pipeline = gst_pipeline_new(NULL);
	element = gst_element_factory_make("uridecodebin", NULL);

	/* support only svg for now, we should get every dom feature registered and get the mime */
	caps = gst_caps_new_simple("image/svg+xml", NULL);
	gst_caps_append(caps, gst_caps_new_simple("image/svg", NULL));

	/* setup uridecodebin */ 
	g_object_set(element, "uri", uri, "caps", caps, NULL);
	/* whenever we have a new pad, connect our demuxer and one of our sinks */
	g_signal_connect (G_OBJECT (element), "pad-added",
			G_CALLBACK (uridecodebin_pad_added_cb), NULL);

	/* setup the pipeline */
	gst_bin_add(GST_BIN(pipeline), element);
	bus = gst_element_get_bus(pipeline);
	gst_bus_add_signal_watch(bus);
	g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)pipeline_error_cb, NULL);
	g_signal_connect(G_OBJECT(bus), "message::eos", (GCallback)pipeline_eos_cb, NULL);
	gst_object_unref (bus);

	/* let's rock */
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	g_main_loop_run(main_loop);

	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);

	return 0;
}
