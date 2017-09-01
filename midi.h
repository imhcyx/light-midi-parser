#include <stdint.h>

typedef enum {
	unknown = 0,
	/* Channel Voice Messages */
	/* low 8 bits are chan no */
	note_off = 0x80,
	note_on = 0x90,
	poly_key_press = 0xa0,
	ctrl_change = 0xb0,
	prog_change = 0xc0,
	chan_press = 0xd0,
	pitch_change = 0xe0,
	/* System Common Messages */
	sysex = 0xf0,
	/* others are not concerned */
	/* System Real-Time Messages */
	/* not concerned */
	meta_evt = 0xff /* we dont care about the reset msg */
} midi_evt_t;

typedef enum {
	eot = 0x2f,
	set_tempo = 0x51
} midi_meta_evt_t;

typedef struct midi_hdr_s {
	uint16_t format; /* As specified in MIDI File Format 1.1 */
	uint16_t ntrks;
	uint16_t division;
} midi_hdr_t;

typedef struct midi_evt_node_s {
	struct midi_evt_node_s *prev;
	struct midi_evt_node_s *next; /* NULL if no more events */
	int deltatime;
	midi_evt_t evt;
	midi_meta_evt_t meta; /* valid if evt == meta_evt */
	char chan;
	int param1; /* note that pitch_change uses param1 only */
	int param2;
	char* parambuf; /* for sysex */
	int paramsize; /* for sysex */
} midi_evt_node_t;

typedef struct midi_trk_s {
	int trkno;
	midi_evt_node_t *first;
} midi_trk_t;

typedef struct midi_s midi_t;

/*
* Open a MIDI file
* Returns NULL on error
*/
midi_t* midi_open_file(char *path);

/*
* Open a MIDI file from memory
* Returns NULL on error
*/
midi_t* midi_open_mem(char *mem, size_t size);

/*
* Test if a opened file is a MIDI file
* Note: This only checks the MThd header
* Returns non-0 if test passed or 0 otherwse
*/
int midi_is_midi_format(midi_t *midi);

/*
* Parse a MIDI file
* Returns 0 if success or non-0 otherwise
*/
int midi_parse(midi_t *midi);

/*
* Get the parsed header of an MIDI file
*/
midi_hdr_t* midi_get_header(midi_t *midi);

/*
* Get the parsed header of an MIDI file
*/
midi_trk_t* midi_get_track(midi_t *midi, int ntrk);

/*
* Combine all tracks into one
* This is useful for MIDI sequencers
* After this, midi_get_track returns the combined track
*/
void midi_combine_track(midi_t *midi);

/*
* Close an opened MIDI file
*/
void midi_close(midi_t *midi);