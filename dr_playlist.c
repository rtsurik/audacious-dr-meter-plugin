#include <stdlib.h>
#include <string.h>
#include "dr_playlist.h"

tracks_list_t *tracks_list_new(int qty) {
	tracks_list_t *new_list;
	static int i;

	new_list = malloc(sizeof(tracks_list_t));
	new_list->qty = qty;
	new_list->tracks = malloc(sizeof(track_info_t) * qty);

	for (i=0; i<qty; i++){
		track_info_t *tmp;
		tmp = &new_list->tracks[i];
		tmp->filename = NULL;
		tmp->artist = NULL;
		tmp->album = NULL;
		tmp->title = NULL;

		tmp->codec = NULL;

		tmp->chan_peaks = NULL;
		tmp->chan_rms = NULL;
		tmp->chan_dr = NULL;
	}

	return new_list;
}

void tracks_list_free(tracks_list_t *list) {
	static int i;
	for (i=0; i< list->qty; i++){
		track_info_t *tmp;
		tmp = &list->tracks[i];
		free(tmp->filename);
		free(tmp->artist);
		free(tmp->album);
		free(tmp->title);

		free(tmp->codec);

		free(tmp->chan_peaks);
		free(tmp->chan_rms);
		free(tmp->chan_dr);
	}

	free(list->tracks);
	free(list);
	list = NULL;
}

static char *_value_malloc(void *value){
	int v_size; char *v_str;

	if (value == NULL ){
		v_size = 0;
	} else {
		v_size = strlen((char *)value);
	}

	if (v_size == 0){
		v_size = strlen("N/A");
		v_str = malloc(v_size+1);
		memcpy(v_str, "N/A", v_size+1);
		return v_str;
	} else {
		v_str = malloc(v_size+1);
		memcpy(v_str, value, v_size+1);
		return v_str;
	}
}

void tracks_list_set_value(tracks_list_t *list, int pos, int prop, void *value){
	track_info_t *tmp = &list->tracks[pos];

	switch (prop){
		case T_INFO_FILENAME:
			if (tmp->filename == NULL) tmp->filename = _value_malloc(value);
			break;
		case T_INFO_ARTIST:
			if (tmp->artist == NULL) tmp->artist = _value_malloc(value);
			break;
		case T_INFO_ALBUM:
			if (tmp->album == NULL) tmp->album = _value_malloc(value);
			break;
		case T_INFO_TITLE:
			if (tmp->title == NULL) tmp->title = _value_malloc(value);
			break;
		case T_INFO_DURATION:
			tmp->duration = *((long *)value);
			break;
		case T_INFO_DR:
			tmp->dr = *((double *)value);
			break;
		case T_INFO_RMS:
			tmp->rms = *((double *)value);
			break;
		case T_INFO_PEAK:
			tmp->peak = *((double *)value);
			break;
		default: 
			break;
	}
}

// set up RMS, peaks and DR for each channel
void tracks_list_set_value_chan(tracks_list_t *list, 
	int pos, int prop, int chan, double value ){

	track_info_t *tmp = &list->tracks[pos];
	switch (prop){
		case T_INFO_DR:
			// allocate memory
			if (tmp->chan_dr == NULL)
				tmp->chan_dr = malloc( sizeof(double) * tmp->channels );
			// set up the DR value
			tmp->chan_dr[chan] = value;
			break;
		case T_INFO_PEAK:
			// allocate memory
			if (tmp->chan_peaks == NULL)
				tmp->chan_peaks = malloc( sizeof(double) * tmp->channels );
			// set up the peak value
			tmp->chan_peaks[chan] = value;
			break;
		case T_INFO_RMS:
			// allocate memory
			if (tmp->chan_rms == NULL)
				tmp->chan_rms = malloc( sizeof(double) * tmp->channels );
			// set up the RMS value
			tmp->chan_rms[chan] = value;			
			break;
		default:
			break;
	}

}

void *tracks_list_get_value(tracks_list_t *list, int pos, int prop){
	track_info_t *tmp = &list->tracks[pos];
	switch (prop){
		case T_INFO_FILENAME:
			return tmp->filename;
			break;
		case T_INFO_ARTIST:
			return tmp->artist; 
			break;
		case T_INFO_ALBUM:
			return tmp->album;
			break;
		case T_INFO_TITLE:
			return tmp->title;
			break;
		case T_INFO_DURATION:
			return &tmp->duration;
			break;
		case T_INFO_DR:
			return &tmp->dr;
			break;
		case T_INFO_RMS:
			return &tmp->rms;
			break;
		case T_INFO_PEAK:
			return &tmp->peak;
			break;
		default: 
			return NULL;
			break;
	}	
}