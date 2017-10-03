#include <stdint.h>

typedef enum {
  midi_unknown = 0,
  /* Channel Messages */
  /* low 8 bits are chan no */
  midi_noteoff = 0x80,
  midi_noteon = 0x90,
  midi_poly_key = 0xa0,
  midi_cc = 0xb0,
  midi_pc = 0xc0,
  midi_chan_press = 0xd0,
  midi_pitch = 0xe0,
  /* System Messages */
  midi_sysex = 0xf0,
  /* others are not concerned */
  midi_meta_evt = 0xff /* we do not care about the reset message */
} midi_evt_t;

typedef enum {
  midi_eot = 0x2f,
  midi_set_tempo = 0x51
  /* others are not concerned */
} midi_meta_evt_t;

typedef struct _midi_hdr {
  uint16_t format; /* As specified in MIDI File Format 1.1 */
  uint16_t ntrks;
  uint16_t division;
} midi_hdr;

typedef struct _midi_evt_node {
  struct _midi_evt_node *prev;
  struct _midi_evt_node *next;
  int time;
  midi_evt_t evt;
  midi_meta_evt_t meta; /* valid if evt == meta_evt */
  char chan;
  int param1; /* note that pitch_change uses param1 only */
  int param2;
  char* parambuf; /* for sysex */
  int paramsize; /* for sysex */
} midi_evt_node;

typedef struct _midi_trks {
  int ntrks;
  midi_evt_node **trk; /* array of the ptrs to the first nodes of tracks */
  int combined; /* 1 if all tracks are combined */
  int abstime; /* 1 if track has been converted into abs time */
} midi_trks;

typedef struct _midi_file midi_file;

/* TODO: Return MIDI check result in open functions */

/*
* Open a MIDI file
* Returns NULL on error
*/
midi_file* midi_open_file(char *path);

/*
* Open a MIDI file from memory
* Returns NULL on error
*/
midi_file* midi_open_mem(char *mem, size_t size);

/*
* Parse a MIDI file
* Returns NULL on error
*/
midi_trks* midi_parse_tracks(midi_file *midi);

/*
* Free parsed tracks
*/
void midi_free_tracks(midi_trks *trks);

/*
* Get the parsed header of an MIDI file
*/
void midi_get_header(midi_file *midi, midi_hdr *hdr);

/*
* Combine all tracks into one
* This is useful for MIDI sequencers
* Note: tracks will be comverted to abstime
*/
void midi_combine_tracks(midi_trks *trks);

/* TODO: description */
void midi_convert_abstime(midi_trks *trks);

/* TODO: description */
void midi_convert_deltatime(midi_trks *trks);

/*
* Close an opened MIDI file
*/
void midi_close(midi_file *midi);