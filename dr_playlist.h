
enum {
	T_INFO_FILENAME = 0,
	T_INFO_ARTIST,
	T_INFO_ALBUM,
	T_INFO_TITLE,
	T_INFO_DURATION,
	T_INFO_DR,
	T_INFO_PEAK,
	T_INFO_RMS
};

typedef struct {
	char *filename;
	char *artist;
	char *album;
	char *title;
	long duration;

	char *codec;
	int channels;
	int samplerate;
	int bitrate;
	int bits_per_sample;

	double *chan_peaks, *chan_rms, *chan_dr;
	double peak, rms, dr;
} track_info_t;

typedef struct {
	track_info_t *tracks;
	int qty;
	int now_playing;
} tracks_list_t;

tracks_list_t *tracks_list_new(int qty);
void tracks_list_free(tracks_list_t *list);
void tracks_list_set_value(tracks_list_t *list, int pos, int prop, void *value);
void tracks_list_set_value_chan(tracks_list_t *list, 
	int pos, int prop, int chan, double value );
void *tracks_list_get_value(tracks_list_t *list, int pos, int prop);