/*
 * Copyright (c) 2012 Rustam Tsurik <rustam.tsurik@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <audacious/plugin.h>
#include <gtk/gtk.h>

#include <audacious/debug.h>
#include <audacious/drct.h>
#include <audacious/gtk-compat.h>
#include <audacious/misc.h>
#include <audacious/playlist.h>

#include <audacious/plugins.h>
#include <audacious/plugin.h>

#include <libaudcore/hook.h>
#include <libaudgui/libaudgui.h>
#include <libaudgui/libaudgui-gtk.h>

#include <math.h>

static void calc_entire_playlist_dr(void);
static void dr_save_to_file(void);

GtkListStore *dr_tree_model;
GtkWidget *main_progress_bar;
GtkWidget *status_bar;

gint cur_track_no = 1;
char *cur_track_title;
char *cur_track_artist;

gint au_format;
gint au_channels;
gint au_rate;
gint bytes_per_sample;
gint frames_per_three_sec;

gint frames_counter;

#define CHANNELS_MAX 8
double tmp_peak[CHANNELS_MAX];
double tmp_rms_sum[CHANNELS_MAX];

#define TRACK_PBACK_MAX 8
#define TRACK_PBACK_MAX_SEC ( TRACK_PBACK_MAX * 3600 )
double rms_h[CHANNELS_MAX][TRACK_PBACK_MAX_SEC];
double peaks_h[CHANNELS_MAX][TRACK_PBACK_MAX_SEC];
gint fragments_counter = 0;

static void playback_start (gpointer data, GtkWidget *area)
{
    gtk_widget_queue_draw(area);
}

static gpointer dr_meter_get_widget (void)
{
	
	GtkWidget *main_grid = gtk_grid_new();

	status_bar = gtk_statusbar_new();	
	gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "status message");

	main_progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(main_progress_bar), 0);

	GtkWidget *toolbar = gtk_toolbar_new();

	GtkToolItem *button_exec =  gtk_tool_button_new_from_stock(GTK_STOCK_EXECUTE);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_exec, -1); 
	g_signal_connect(button_exec, "clicked", (GCallback) calc_entire_playlist_dr, FALSE);

	GtkToolItem *button_save =  gtk_tool_button_new_from_stock(GTK_STOCK_SAVE);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_save, -1); 
	g_signal_connect(button_save, "clicked", (GCallback) dr_save_to_file, FALSE);

	GtkToolItem *button_properties =  gtk_tool_button_new_from_stock(GTK_STOCK_PROPERTIES);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_properties, -1); 

	GtkWidget *dr_tree_view = gtk_tree_view_new();

	GtkCellRenderer *dr_cell_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes (
		GTK_TREE_VIEW (dr_tree_view), -1, "#", dr_cell_renderer, "text", 0, NULL );
	gtk_tree_view_insert_column_with_attributes (
		GTK_TREE_VIEW (dr_tree_view), -1, "Artist", dr_cell_renderer, "text", 1, NULL );
	gtk_tree_view_insert_column_with_attributes (
		GTK_TREE_VIEW (dr_tree_view), -1, "Title", dr_cell_renderer, "text", 2, NULL );
	gtk_tree_view_insert_column_with_attributes (
		GTK_TREE_VIEW (dr_tree_view), -1, "DR", dr_cell_renderer, "text", 3, NULL );
	gtk_tree_view_insert_column_with_attributes (
		GTK_TREE_VIEW (dr_tree_view), -1, "Peak", dr_cell_renderer, "text", 4, NULL );
	gtk_tree_view_insert_column_with_attributes (
		GTK_TREE_VIEW (dr_tree_view), -1, "RMS", dr_cell_renderer, "text", 5, NULL );

	dr_tree_model = gtk_list_store_new (6, 
		G_TYPE_INT, 
		G_TYPE_STRING, 
		G_TYPE_STRING, 
		G_TYPE_STRING, 
		G_TYPE_STRING, 
		G_TYPE_STRING );

	gtk_tree_view_set_model (GTK_TREE_VIEW(dr_tree_view), GTK_TREE_MODEL(dr_tree_model) );

	GtkTreeViewColumn *this_column = gtk_tree_view_get_column (GTK_TREE_VIEW(dr_tree_view), 1);
	g_object_set (G_OBJECT (this_column), "resizable", TRUE, "min-width", 120, NULL);

	this_column = gtk_tree_view_get_column (GTK_TREE_VIEW(dr_tree_view), 2);
	g_object_set (G_OBJECT (this_column), "resizable", TRUE, "min-width", 120, NULL);

	GtkWidget *scrolledTreeContainer = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_add_with_viewport( GTK_SCROLLED_WINDOW(scrolledTreeContainer), dr_tree_view);

	gtk_grid_attach(GTK_GRID(main_grid), toolbar, 0, 0, 1, 1);

	gtk_widget_set_vexpand (dr_tree_view, TRUE);
	gtk_widget_set_valign (dr_tree_view, GTK_ALIGN_FILL);

	gtk_grid_attach_next_to(GTK_GRID(main_grid), scrolledTreeContainer, toolbar, GTK_POS_BOTTOM, 1, 1);
	gtk_grid_attach_next_to(GTK_GRID(main_grid), main_progress_bar, scrolledTreeContainer, GTK_POS_BOTTOM, 1, 1);
	gtk_grid_attach_next_to(GTK_GRID(main_grid), status_bar, main_progress_bar, GTK_POS_BOTTOM, 1, 1);

	gtk_grid_set_row_spacing(GTK_GRID(main_grid), 5);
	gtk_grid_set_column_homogeneous(GTK_GRID(main_grid), TRUE);

    return main_grid;
}

static void dr_meter_cleanup(void) {
	hook_dissociate("playback begin", (HookFunction) playback_start);
}

static void dr_meter_about(void) {
}

gint output_open_audio (gint format, gint rate, gint channels) {

	au_format = format;
	au_channels = channels;
	au_rate = rate;
	bytes_per_sample = FMT_SIZEOF(au_format);
	frames_per_three_sec = rate * 3;

	frames_counter = 0;
	fragments_counter = 0;

	gint ichn;
	for (ichn = 0; ichn < au_channels; ichn++){
		tmp_rms_sum[ichn] = 0.0; tmp_peak[ichn] = 0.0;
	}
	
	return 1;
}

void output_set_replaygain_info (ReplayGainInfo * info) {
}

void output_write_audio (void * data, gint length) {

	int samples = length / FMT_SIZEOF (au_format);
	int frames = samples / au_channels;

	float *new = g_malloc(sizeof(float) * samples);

	if (au_format != FMT_FLOAT)
	{
		audio_from_int (data, au_format, new, samples);

	} else {
		memcpy(new, data, sizeof (float) * samples);
	}

	static int i, ichn;
	static double tmp_value;

	for (i = 0; i < frames; i++){

		for (ichn = 0; ichn < au_channels; ichn++){
			tmp_value = fabs(new[i*au_channels+ichn]);
			tmp_rms_sum[ichn] +=  tmp_value * tmp_value; 
			if ( tmp_peak[ichn] < tmp_value ) {
				tmp_peak[ichn] = tmp_value;
			}
		}

		frames_counter++;
		if (frames_counter >= frames_per_three_sec){
			frames_counter = 0;

			for (ichn = 0; ichn < au_channels; ichn++){
				rms_h[ichn][fragments_counter] = sqrt(2.0 * tmp_rms_sum[ichn] / frames_per_three_sec);
				peaks_h[ichn][fragments_counter] = tmp_peak[ichn];

				tmp_rms_sum[ichn] = 0.0; tmp_peak[ichn] = 0.0;
			}
			fragments_counter++;
		}
	}
	g_free (new);

}

int compare_doubles (const void *a, const void *b) {

	const double *da = (const double *) a;
	const double *db = (const double *) b;

	return (*da > *db) - (*da < *db);
}

double to_db (double x) {

	return (20 * log10(x));
}

void output_close_audio (void) {

	static gint ichn, i, upper_starts, upper_qty;
	static double tmp_rms_sum_upper[CHANNELS_MAX];
	static double dr_per_channel[CHANNELS_MAX];
	static double dr, peak, rms;
	
	upper_starts = fragments_counter - (fragments_counter / 5); 
	upper_qty = (fragments_counter / 5) + 1;

	dr = 0.0; rms = 0.0; peak = 0.0;

	for (ichn = 0; ichn < au_channels; ichn++ ){

		qsort(peaks_h[ichn], fragments_counter + 1, sizeof(double), compare_doubles);
		qsort(rms_h[ichn], fragments_counter + 1, sizeof(double), compare_doubles);

		tmp_rms_sum_upper[ichn] = 0.0;

		for (i = upper_starts; i <= fragments_counter ; i++) {
			tmp_rms_sum_upper[ichn] += rms_h[ichn][i] * rms_h[ichn][i];
		}
		tmp_rms_sum[ichn] = tmp_rms_sum_upper[ichn];
		for (i = 0 ; i < upper_starts; i++){
			tmp_rms_sum[ichn] += rms_h[ichn][i] * rms_h[ichn][i];
		}
		dr_per_channel[ichn] = to_db(peaks_h[ichn][fragments_counter - 1] 
			/ sqrt(tmp_rms_sum_upper[ichn] / upper_qty));
		dr += dr_per_channel[ichn];

		if (peaks_h[ichn][fragments_counter] > peak) {
			peak = peaks_h[ichn][fragments_counter];
		}
		tmp_rms_sum[ichn] = sqrt(tmp_rms_sum[ichn] / fragments_counter);
		rms += tmp_rms_sum[ichn];
	}

	dr = round( dr / au_channels);
	peak = to_db(peak);
	rms = to_db(rms / au_channels);

	static char dr_txt[5];
	static char peak_txt[10];
	static char rms_txt[10];

	sprintf (dr_txt, "DR%.0f", dr);
	sprintf (peak_txt, "%.2f", peak);
	sprintf (rms_txt, "%.2f", rms);

	GtkTreeIter newItem;

	gtk_list_store_append (dr_tree_model, &newItem );
	gtk_list_store_set (
		dr_tree_model, &newItem, 
		0, cur_track_no, 
		1, cur_track_artist, 
		2, cur_track_title,
		3, dr_txt, 
		4, peak_txt,
		5, rms_txt,
		-1 );
}

void output_pause (gboolean pause) {
}

void output_flush (gint time) {
}

gint output_written_time (void) {
	return 0;
}

gboolean output_buffer_playing (void) {
	return FALSE;
}

void output_abort_write (void) {
}

void ip_set_data (InputPlayback * p, void * data){
}

void * ip_get_data (InputPlayback * p) {
	return NULL;
}

void ip_set_pb_ready (InputPlayback * p) {
}

void ip_set_params (InputPlayback * p, gint bitrate, gint samplerate, gint channels) {
}

void ip_set_tuple (InputPlayback * playback, Tuple * tuple) {
}

void ip_set_gain_from_playlist (InputPlayback * playback) {
}


static void calc_entire_playlist_dr( void ) {      
    gint playlist_num;
	gint playlist_entry_count;
	
	playlist_num = aud_playlist_get_active();
	playlist_entry_count = aud_playlist_entry_count(playlist_num);
	
	int i;
	gboolean dec_status;
	PluginHandle * decoder;
	InputPlugin * decoder_cur;
	
	static struct OutputAPI output_api = {
		.open_audio = output_open_audio,
		.set_replaygain_info = output_set_replaygain_info,
		.write_audio = output_write_audio,
		.close_audio = output_close_audio,

		.pause = output_pause,
		.flush = output_flush,
		.written_time = output_written_time,
		.buffer_playing = output_buffer_playing,
		.abort_write = output_abort_write,
	};

	static InputPlayback playback_api = {
		.output = & output_api,
		.set_data = ip_set_data,
		.get_data = ip_get_data,
		.set_pb_ready = ip_set_pb_ready,
		.set_params = ip_set_params,
		.set_tuple = ip_set_tuple,
		.set_gain_from_playlist = ip_set_gain_from_playlist,
	};

	gtk_list_store_clear (dr_tree_model);

	char status_message[50];

	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(main_progress_bar), 0);
	while( gtk_events_pending() ) gtk_main_iteration();
	
	for ( i=0; i<playlist_entry_count; i++ ) {

		sprintf(status_message, "Processing track %i of %i", i+1, playlist_entry_count);
		gtk_statusbar_remove_all(GTK_STATUSBAR(status_bar),0);
		gtk_statusbar_push(GTK_STATUSBAR(status_bar),0, status_message);
		while( gtk_events_pending() ) gtk_main_iteration();

		char * entry_filename = aud_playlist_entry_get_filename(playlist_num, i);

		Tuple *tuple = NULL;
		tuple = aud_playlist_entry_get_tuple (playlist_num, i, FALSE);
		cur_track_artist = tuple_get_str (tuple, FIELD_ARTIST, NULL);
		cur_track_title = tuple_get_str (tuple, FIELD_TITLE, NULL);
		cur_track_no = i + 1;

		tuple_unref(tuple);

		decoder = aud_playlist_entry_get_decoder(playlist_num, i, FALSE);
		decoder_cur = aud_plugin_get_header( decoder );
		VFSFile * file = vfs_fopen (entry_filename, "r");		
		dec_status = decoder_cur->play (& playback_api, entry_filename, file, -1, -1, FALSE);
		
		str_unref(entry_filename);

		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(main_progress_bar), ((double)i+1)/(double)playlist_entry_count);
		while( gtk_events_pending() ) gtk_main_iteration();
	}

	gtk_statusbar_remove_all(GTK_STATUSBAR(status_bar), 0);
	gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Done");
	while( gtk_events_pending() ) gtk_main_iteration();
}

static void dr_save_to_file(void) {
	
	GtkWidget *dialog;

	dialog = gtk_file_chooser_dialog_new ("Save File",
		NULL,
		GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), g_get_home_dir());
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), "dr_meter_data.txt");

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		// save data to a file here

		g_free (filename);
	}

	gtk_widget_destroy (dialog);
}

gboolean dr_meter_init(void) {

	return TRUE;
}

static void dr_meter_configure(void) {

}

AUD_GENERAL_PLUGIN
(
	.name = "DR Meter",
	.cleanup = dr_meter_cleanup,
	.get_widget = dr_meter_get_widget,
	.init = dr_meter_init,
	.about = dr_meter_about,
	.configure = dr_meter_configure
)

