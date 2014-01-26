/*
 * Copyright (c) 2012-2014 Rustam Tsurik <rustam.tsurik@gmail.com>
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

#include <audacious/misc.h>
#include <audacious/playlist.h>

#include <audacious/plugins.h>
#include <audacious/plugin.h>

#include <libaudcore/hook.h>
#include <libaudgui/libaudgui.h>
#include <libaudgui/libaudgui-gtk.h>

#include "dr_playlist.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DR_METER_VERSION "20140125-alpha"

static void calc_entire_playlist_dr(void);
static void dr_save_to_file(void);
static void dr_meter_configure(void);


tracks_list_t *playlist;
gint calc_thread_status = 0;

GtkListStore *dr_tree_model;
GtkWidget *main_progress_bar;

gint cur_track_no = 1;

char *cur_track_title;      // these cur_track_x vars need to be moved
char *cur_track_artist;     // under the calc_entire_playlist_dr() function
char *cur_track_album;      // as soon as we've switched to the playlist struct
long cur_track_duration;    

unsigned char track_processed;

gint au_format;
gint au_channels;
gint au_rate;
gint bytes_per_sample;
gint frames_per_three_sec;

gint frames_counter;

#define CHANNELS_MAX 8          // this needs to be changed to dynamic arrays
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

// AUD_GENERAL_PLUGIN->get_widget callback
static gpointer dr_meter_get_widget (void)
{
    
    // define elements

    // grid
    GtkWidget *main_grid = gtk_grid_new();

    // progress bar
    main_progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(main_progress_bar), 0);
    gtk_widget_set_hexpand (main_progress_bar, TRUE);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(main_progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(main_progress_bar), "Press the Exec button");

    // tool bar with buttons
    GtkWidget *toolbar = gtk_toolbar_new();

    gtk_widget_set_hexpand (toolbar, FALSE);
    g_object_set (G_OBJECT (toolbar), "width-request", 100, NULL);
    gtk_toolbar_set_icon_size(GTK_TOOLBAR(toolbar), GTK_ICON_SIZE_MENU);

    GtkToolItem *button_exec = 
        gtk_tool_button_new_from_stock(GTK_STOCK_EXECUTE);

    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_exec, -1); 
    g_signal_connect(button_exec, 
        "clicked", 
        (GCallback) calc_entire_playlist_dr, 
        FALSE);

    GtkToolItem *button_save =  gtk_tool_button_new_from_stock(GTK_STOCK_SAVE);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_save, -1); 
    g_signal_connect(button_save, 
        "clicked", 
        (GCallback) dr_save_to_file, 
        FALSE);

    GtkToolItem *button_properties =  
        gtk_tool_button_new_from_stock(GTK_STOCK_PROPERTIES);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_properties, -1); 
    g_signal_connect(button_properties, 
        "clicked", 
        (GCallback) dr_meter_configure, 
        FALSE);

    // tree view
    GtkWidget *dr_tree_view = gtk_tree_view_new();

    GtkCellRenderer *dr_cell_renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (
        GTK_TREE_VIEW (dr_tree_view), -1, "#", dr_cell_renderer, 
        "text", 0, NULL );
    gtk_tree_view_insert_column_with_attributes (
        GTK_TREE_VIEW (dr_tree_view), -1, "Artist", dr_cell_renderer, 
        "text", 1, NULL );
    gtk_tree_view_insert_column_with_attributes (
        GTK_TREE_VIEW (dr_tree_view), -1, "Title", dr_cell_renderer, 
        "text", 2, NULL );
    gtk_tree_view_insert_column_with_attributes (
        GTK_TREE_VIEW (dr_tree_view), -1, "DR value", dr_cell_renderer, 
        "text", 3, NULL );
    gtk_tree_view_insert_column_with_attributes (
        GTK_TREE_VIEW (dr_tree_view), -1, "Peak (dB)", dr_cell_renderer, 
        "text", 4, NULL );
    gtk_tree_view_insert_column_with_attributes (
        GTK_TREE_VIEW (dr_tree_view), -1, "RMS (dB)", dr_cell_renderer, 
        "text", 5, NULL );

    gtk_widget_set_vexpand (dr_tree_view, TRUE);
    gtk_widget_set_hexpand (dr_tree_view, TRUE);

    // list store
    dr_tree_model = gtk_list_store_new (6, 
        G_TYPE_INT, 
        G_TYPE_STRING, 
        G_TYPE_STRING, 
        G_TYPE_STRING, 
        G_TYPE_STRING, 
        G_TYPE_STRING );

    // attach the tree view to list store
    gtk_tree_view_set_model (GTK_TREE_VIEW(dr_tree_view), 
        GTK_TREE_MODEL(dr_tree_model) );

    // tree view: set up columns attributes
    GtkTreeViewColumn *this_column = 
        gtk_tree_view_get_column (GTK_TREE_VIEW(dr_tree_view), 1);
    g_object_set (G_OBJECT (this_column), 
    "resizable", TRUE, "min-width", 120, NULL);

    this_column = gtk_tree_view_get_column (GTK_TREE_VIEW(dr_tree_view), 2);
    g_object_set (G_OBJECT (this_column), 
    "resizable", TRUE, "min-width", 120, NULL);

    this_column = gtk_tree_view_get_column (GTK_TREE_VIEW(dr_tree_view), 3);
    g_object_set (G_OBJECT (this_column), 
    "resizable", TRUE, "min-width", 50, NULL);

    this_column = gtk_tree_view_get_column (GTK_TREE_VIEW(dr_tree_view), 4);
    g_object_set (G_OBJECT (this_column), 
    "resizable", TRUE, "min-width", 50, NULL);

    // create a scrolled window and put the tree view into it
    GtkWidget *scrolledTreeContainer = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolledTreeContainer), dr_tree_view);

    // attach widgets to the grid 
    gtk_grid_attach(GTK_GRID(main_grid), scrolledTreeContainer, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(main_grid), toolbar, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(main_grid), main_progress_bar, 1, 1, 1, 1);

    gtk_widget_set_size_request(main_grid, 700, 400);
    return main_grid;
}

static void dr_meter_cleanup(void) {
    hook_dissociate("playback begin", (HookFunction) playback_start);
}

// AUD_GENERAL_PLUGIN->about callback
static void dr_meter_about(void) {

    static GtkWidget *about_dialog = NULL;

    audgui_simple_message (& about_dialog, GTK_MESSAGE_INFO, "About DR Meter",
    "Dynamic Range Meter plugin for Audacious.\n\n"
    "Copyright (c) 2012-2014 Rustam Tsurik");
}

// OutputAPI->open_audio:
// playback starts here, init the variables
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
    
    track_info_t *tmp = &playlist->tracks[ playlist->now_playing ];
    tmp->channels = channels;

    return 1;
}

// OutputAPI->set_replaygain_info
void output_set_replaygain_info (const ReplayGainInfo *info) {
}

// this is needed for the qsort function
int compare_doubles (const void *a, const void *b) {

    const double *da = (const double *) a;
    const double *db = (const double *) b;

    return (*da > *db) - (*da < *db);
}

// convert linear data to dB
double to_db (double x) {

    return (20 * log10(x));
}

// output the DR, RMS and peak values to the GUI
void add_values_to_tree(double dr, double rms, double peak ){
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

// OutputAPI->write_audio:
// this callback function receives decoded audio data
// which we process in here and use to calculate DR
void output_write_audio (void *data, gint length) {

    if (length == 0) { // output_close_audio()
    /* 
      prior to Audacious 3.3 there was a separate callback,
      OutputAPI -> .close_audio which has been removed now, 
      so we're moving the code in here
    */
    static gint ichn, i, upper_starts, upper_qty;
    static double tmp_rms_sum_upper[CHANNELS_MAX];
    static double dr_per_channel[CHANNELS_MAX];
    static double dr, peak, rms;
    
    // we need the upper 20% of data
    upper_starts = fragments_counter - (fragments_counter / 5); 
    upper_qty = (fragments_counter / 5) + 1;

    dr = 0.0; rms = 0.0; peak = 0.0;

    for (ichn = 0; ichn < au_channels; ichn++ ){

        qsort ( peaks_h[ichn], 
            fragments_counter + 1, sizeof(double), compare_doubles );
        qsort ( rms_h[ichn], 
            fragments_counter + 1, sizeof(double), compare_doubles );

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

        // put the values for each channel to the playlist struct
        tracks_list_set_value_chan(playlist, playlist->now_playing, 
            T_INFO_DR, ichn, dr_per_channel[ichn] );
        tracks_list_set_value_chan(playlist, playlist->now_playing, 
            T_INFO_RMS, ichn, to_db(tmp_rms_sum[ichn]) );
        tracks_list_set_value_chan(playlist,playlist->now_playing, 
            T_INFO_PEAK, ichn, to_db(peaks_h[ichn][fragments_counter]) );
    }

    dr = round( dr / au_channels);
    peak = to_db(peak);
    rms = to_db(rms / au_channels);

    tracks_list_set_value(playlist, playlist->now_playing, T_INFO_DR, &dr);
    tracks_list_set_value(playlist, playlist->now_playing, T_INFO_RMS, &rms);
    tracks_list_set_value(playlist, playlist->now_playing, T_INFO_PEAK, &peak);

    add_values_to_tree(dr, rms, peak);
    track_processed = 1;
    } // output_close_audio() end.

    // actually, I'm not sure if the length is always 
    // multiple of (channels * bytes per sample), suppose it is 
    // but this needs to be re-checked
            
    int samples = length / FMT_SIZEOF (au_format);
    int frames = samples / au_channels;

    // allocate memory for the received data
    float *new = g_malloc(sizeof(float) * samples);

    if (au_format != FMT_FLOAT)
    {   // if integer, convert to float
        audio_from_int (data, au_format, new, samples);
    } else { // if float, leave as is: just copy
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

        // each fragment is 3 seconds long
        if (frames_counter >= frames_per_three_sec){
            frames_counter = 0;

            for (ichn = 0; ichn < au_channels; ichn++){
                rms_h[ichn][fragments_counter] = sqrt(
                    2.0 * tmp_rms_sum[ichn] / frames_per_three_sec
                );
                peaks_h[ichn][fragments_counter] = tmp_peak[ichn];

                tmp_rms_sum[ichn] = 0.0; tmp_peak[ichn] = 0.0;
            }
            fragments_counter++;
        }
    }
    g_free (new);

}



// track decoding finished:
// calculate the final DR values from the collected data
void output_close_audio (void) {
    // removed in Audacious 3.3, moving the code to the write_audio callback

}

// OutputAPI->pause
void output_pause (gboolean pause) {
}

// OutputAPI->flush
void output_flush (gint time) {
}

// OutputAPI->written_time
gint output_written_time (void) {
    return 0;
}

// OutputAPI->buffer_playing
gboolean output_buffer_playing (void) {
    return FALSE; // no bufferring
}

// OutputAPI->abort_write
void output_abort_write (void) {
}


// InputPlayback->set_data
void ip_set_data (InputPlayback * p, void * data){
}

// InputPlayback->get_data
void * ip_get_data (InputPlayback * p) {
    return NULL;
}

// InputPlayback->set_pb_ready
void ip_set_pb_ready (InputPlayback * p) {
}

// InputPlayback->set_params
void ip_set_params (InputPlayback * p, 
    gint bitrate, gint samplerate, gint channels) {
}

// InputPlayback->set_tuple
void ip_set_tuple (InputPlayback * playback, Tuple * tuple) {
}

// InputPlayback->set_gain_from_playlist
void ip_set_gain_from_playlist (InputPlayback * playback) {
}

// payload invoked from calc_entire_playlist_dr()
void *dr_calc_thread(void *data) {

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
        .pause = output_pause,
        .flush = output_flush,
        .written_time = output_written_time,
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

    // zero the progress bar
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(main_progress_bar), 0);
    while( gtk_events_pending() ) gtk_main_iteration();

    // allocate memory for the playlist structure
    if (playlist != NULL ) tracks_list_free(playlist);
    playlist = tracks_list_new(playlist_entry_count);
    
    // process the tracks
    for ( i=0; i<playlist_entry_count; i++ ) {

        // update the status bar
        sprintf(status_message, 
            "Processing track %i of %i", i+1, playlist_entry_count);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(main_progress_bar), status_message);

        while( gtk_events_pending() ) gtk_main_iteration();

        // get the file name for the current track
        char * entry_filename = aud_playlist_entry_get_filename(playlist_num, i);

        // get a tuple which contains all track meta data
        Tuple *tuple = NULL;
        tuple = aud_playlist_entry_get_tuple (playlist_num, i, FALSE);

        // retrieve meta data from the received tuple 
        cur_track_artist = tuple_get_str (tuple, FIELD_ARTIST, NULL);
        cur_track_title = tuple_get_str (tuple, FIELD_TITLE, NULL);
        cur_track_album = tuple_get_str (tuple, FIELD_ALBUM, NULL);

        // populate the `playlist` structure with this meta data
        tracks_list_set_value(playlist, i, T_INFO_FILENAME, entry_filename);
        tracks_list_set_value(playlist, i, T_INFO_TITLE, cur_track_title);
        tracks_list_set_value(playlist, i, T_INFO_ARTIST, cur_track_artist);
        tracks_list_set_value(playlist, i, T_INFO_ALBUM, cur_track_album);
        playlist->now_playing = i;

        // we'll get rid of this later, 
        // as playlist->now_playing contain this data
        cur_track_no = i + 1;

        // free up the tuple
        tuple_unref(tuple);

        // free up the strings
        str_unref(cur_track_title);
        str_unref(cur_track_artist);
        str_unref(cur_track_album);

        // and receive the processed values from the playlist struct
        // e.g. it populates "N/A" string to the empty ones, etc.
        cur_track_title = tracks_list_get_value(playlist, i, T_INFO_TITLE);
        cur_track_artist = tracks_list_get_value(playlist, i, T_INFO_ARTIST);
        cur_track_album = tracks_list_get_value(playlist, i, T_INFO_ALBUM);

        // start decoding the track
        track_processed = 0;
        decoder = aud_playlist_entry_get_decoder(playlist_num, i, FALSE);
        decoder_cur = aud_plugin_get_header( decoder );
        VFSFile * file = vfs_fopen (entry_filename, "r");       
        dec_status = decoder_cur->play(
            &playback_api, entry_filename, file, -1, -1, FALSE);

        if (track_processed == 0) {
            output_write_audio(NULL, 0);
        }

        if (dec_status != 0) {
            // do something
        }

        // free up the filename string
        str_unref(entry_filename);

        // update the progress bar
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(main_progress_bar), 
            ((double)i+1)/(double)playlist_entry_count );
        while( gtk_events_pending() ) gtk_main_iteration();
    }

    // update the status bar
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(main_progress_bar), "Done");

    while( gtk_events_pending() ) gtk_main_iteration();

    calc_thread_status = 0;

    return 0;
}


// a callback function for the EXECUTE button
static void calc_entire_playlist_dr( void ) {
    int data = 0; pthread_t thread1;
    // check whether the thread is already running
    if (calc_thread_status == 0) {
        calc_thread_status = 1; //need a mutex here?
        int err = pthread_create( &thread1, NULL, &dr_calc_thread, (void *)data);
        if (err == 0) { 
          // do something?
        }
    } else {
        // stop the thread
    }
}

// a callback function for the SAVE button
static void dr_save_to_file(void) {
    
    GtkWidget *dialog;

    dialog = gtk_file_chooser_dialog_new ("Save File",
        NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_file_chooser_set_do_overwrite_confirmation (
        GTK_FILE_CHOOSER (dialog), TRUE );

    // ergh, later we'll want to change g_get_home_dir() to smth else
    gtk_file_chooser_set_current_folder(
        GTK_FILE_CHOOSER (dialog), g_get_home_dir() );

    gtk_file_chooser_set_current_name (
        GTK_FILE_CHOOSER (dialog), "dr_meter_data.txt" );

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
        char *filename;

        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

        // add some file & directory name checks

        // save data to a file

        FILE *file; 
        file = fopen(filename,"w");

        // print a header

        fprintf(file, "Audacious / Dynamic Range Meter v%s\n",DR_METER_VERSION);

        time_t timer; 
        timer=time(NULL);
        fprintf(file, "Log date: %s\n", asctime(localtime(&timer)));

        // print the rest of data

        if (playlist != NULL) {

            static int i, j;
            for (i=0; i < playlist->qty; i++){
                fprintf(file, "%i - %s - %s - [DR%.0f]  RMS: %.2fdB Peak: %.2fdB\n", 
                    i + 1,  
                    (char *)(tracks_list_get_value(playlist, i, T_INFO_TITLE)),
                    (char *)(tracks_list_get_value(playlist, i, T_INFO_ARTIST)),
                    *((double *)tracks_list_get_value(playlist, i, T_INFO_DR)),
                    *((double *)tracks_list_get_value(playlist, i, T_INFO_RMS)),
                    *((double *)tracks_list_get_value(playlist, i, T_INFO_PEAK))
                    );

                fprintf(file, "  URL: %s\n", 
                (char *)(tracks_list_get_value(playlist, i, T_INFO_FILENAME)));

                track_info_t *tmp = &playlist->tracks[i];
                for (j=0; j < tmp->channels; j++){
                    fprintf(file, "  chn %i DR: %.2f RMS: %.2fdB Peak: %.2fdB\n", 
                        j, tmp->chan_dr[j], tmp->chan_rms[j], tmp->chan_peaks[j] 
                    );
                }

            }
        }
        
        fclose(file);

        g_free (filename);
    }

    gtk_widget_destroy (dialog);
}

// AUD_GENERAL_PLUGIN->init
gboolean dr_meter_init(void) {

    playlist = NULL;
    return TRUE;
} 

// AUD_GENERAL_PLUGIN->configure
static void dr_meter_configure(void) {
}

AUD_GENERAL_PLUGIN
(
    .name = "Dynamic Range Meter",
    .cleanup = dr_meter_cleanup,
    .get_widget = dr_meter_get_widget,
    .init = dr_meter_init,
    .about = dr_meter_about,
    .configure = dr_meter_configure
)

