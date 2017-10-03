#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "midi.h"

#define MAX_SYSEX_DATA_LEN 128

typedef struct _midi_file {
  FILE *stream; /* Valid if loaded from file, otherwise NULL */
  char *mem; /* Valid if loaded from memory */
  size_t size; /* Valid if loaded from memory */
  long pos; /* Valid if loaded from memory */
  midi_hdr hdr;
} midi_file;

const char MTHD_MAGIC[] = {'M','T','h','d'};
const char MTRK_MAGIC[] = {'M','T','r','k'};

#define alloc(x, t) (((x) = ((t*)malloc(sizeof(t)))) != NULL && (memset((x), 0, sizeof(t)), 1))

#define msb32(b) ((b)[0] & 0xff) << 24 | ((b)[1] & 0xff) << 16 | ((b)[2] & 0xff) << 8 | ((b)[3] & 0xff)
#define msb24(b) ((b)[0] & 0xff) << 16 | ((b)[1] & 0xff) << 8 | ((b)[2] & 0xff)
#define msb16(b) ((b)[0] & 0xff) << 8 | ((b)[1] & 0xff)

static void error_log(char *fmt, ...);
static int midi_eof(midi_file *midi);
static int midi_getc(midi_file *midi);
static int midi_getnc(midi_file *midi, char* buf, int nbytes);
static int midi_seek(midi_file *midi, long pos, int whence);
static long midi_tell(midi_file *midi);
static int midi_readvarlen(midi_file *midi);
static int midi_parse_hdr(midi_file *midi);
static midi_evt_node* midi_parse_trk(midi_file *midi);
static void midi_free_trk(midi_evt_node *trk);

static midi_evt_node* combine_trk_abstime(midi_evt_node *trk1, midi_evt_node *trk2);
static void rebuild_backlink_trk(midi_evt_node *trk);

/* TODO: custom error logging */
static void error_log(char *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  fprintf(stderr, "MIDI parser error: ");
  vfprintf(stderr, fmt, arg);
  fprintf(stderr, "\n");
  va_end(arg);
}

static int midi_eof(midi_file *midi) {
  if (midi->stream) {
    return feof(midi->stream);
  }
  else {
    if (midi->pos >= midi->size) return 1;
    return 0;
  }
}

/* Returns -1 on error */
static int midi_getc(midi_file *midi) {
  if (midi->stream) {
    return fgetc(midi->stream);
  }
  else {
    if (midi->pos >= midi->size) return -1;
    return midi->mem[midi->pos++] & 0xff;
  }
}

/* Returns num of read bytes */
static int midi_getnc(midi_file *midi, char* buf, int nbytes) {
  int i, c;
  for (i = 0; i < nbytes; i++) {
    if ((c = midi_getc(midi)) < 0) break;
    buf[i] = (char)c;
  }
  return i;
}

/* Returns non-0 on error */
static int midi_seek(midi_file *midi, long pos, int whence) {
  long newpos;
  if (midi->stream) {
    return fseek(midi->stream, pos, whence);
  }
  else {
    switch (whence) {
    case SEEK_SET:
      newpos = pos;
      break;
    case SEEK_CUR:
      newpos = midi->pos + pos;
      break;
    case SEEK_END:
      newpos = midi->size - pos;
      break;
    default:
      return -1;
    }
    if (newpos < 0 || newpos >= midi->size) return -1;
    midi->pos = newpos;
    return 0;
  }
}

static long midi_tell(midi_file *midi) {
  if (midi->stream) {
    return ftell(midi->stream);
  }
  else {
    return midi->pos;
  }
}

/* Returns -1 on error */
static int midi_readvarlen(midi_file *midi) {
  int value;
  int c;
  value = 0;
  do {
    value <<= 7;
    c = midi_getc(midi);
    if (c < 0) return -1;
    value |= c & 0x7f;
  } while (c & 0x80);
  return value;
}

/* Returns non-0 on error */
static int midi_parse_hdr(midi_file *midi) {
  uint32_t length;
  int i, c;
  char buf[4];
  if (midi_getnc(midi, buf, 4) != 4) return -1;
  if (memcmp(MTHD_MAGIC, buf, 4)) return -1;
  if (midi_getnc(midi, buf, 4) != 4) return -1;
  length = msb32(buf);
  if (midi_getnc(midi, buf, 2) != 2) return -1;
  midi->hdr.format = msb16(buf);
  if (midi_getnc(midi, buf, 2) != 2) return -1;
  midi->hdr.ntrks = msb16(buf);
  if (midi_getnc(midi, buf, 2) != 2) return -1;
  midi->hdr.division = msb16(buf);
  return 0;
}

/* The function assumes pos points to the length after MTrk */
/* Returns NULL on error */
static midi_evt_node* midi_parse_trk(midi_file *midi) {
  /* <Track Chunk> = <chunk type><length><MTrk event> */
  /* <MTrk event> = <delta-time><event> */
  #define NEXT_CHAR if ((c = midi_getc(midi)) < 0) goto _err_cleanup
  int i, c;
  char buf[4];
  uint32_t length;
  long initpos;
  int deltatime;
  midi_evt_t evt;
  midi_meta_evt_t meta;
  midi_evt_node *node, *head, *tail;
  node = head = tail = NULL;
  /* Read track length */
  if (midi_getnc(midi, buf, 4) != 4) goto _err_cleanup;
  length = msb32(buf);
  initpos = midi_tell(midi);
  while (midi_tell(midi) - initpos < length) {
    /* Allocate a node */
    if (!alloc(node, midi_evt_node)) {
      error_log("No memory");
      goto _err_cleanup;
    }
    /* Read delta-time */
    deltatime = midi_readvarlen(midi);
    if (deltatime < 0) goto _err_cleanup;
    node->time = deltatime;
    /* Note that the status code may be omitted
    *  if not changed for channel messages */
    /* Probe a byte */
    NEXT_CHAR;
    if (c & 0x80) { /* A status byte */
      evt = c;
      if ((c & 0xf0) != 0xf0) { /* A channel message */
        /* Prepare the next byte as the first data byte */
        NEXT_CHAR;
      }
    }
    switch (evt & 0xf0) {
    /* channel messages with 2 parameters */
    case midi_noteoff:
    case midi_noteon:
    case midi_poly_key:
    case midi_cc:
      node->evt = evt & 0xf0;
      node->chan = evt & 0xf;
      node->param1 = c;
      NEXT_CHAR;
      node->param2 = c;
      break;
    /* channel messages with 1 parameter */
    case midi_pc:
    case midi_chan_press:
      node->evt = evt & 0xf0;
      node->chan = evt & 0xf;
      node->param1 = c;
      break;
    /* channel messages with 2 bytes as 1 parameter */
    case midi_pitch:
      node->evt = evt & 0xf0;
      node->chan = evt & 0xf;
      node->param1 = c;
      NEXT_CHAR;
      node->param1 |= c << 7;
      break;
    default:
      /* system messages */
      node->evt = evt;
      switch (evt) {
      case midi_sysex:
        if ((node->parambuf = (char*)malloc(MAX_SYSEX_DATA_LEN)) == NULL) {
          error_log("No Memory");
          goto _err_cleanup;
        }
        NEXT_CHAR;
        for (i = 0; i < MAX_SYSEX_DATA_LEN && c != 0xf7; i++) {
          node->parambuf[i] = (char)c;
          NEXT_CHAR;
        }
        if (c != 0xf7) goto _err_cleanup;
        node->paramsize = i;
        break;
      case midi_meta_evt:
        NEXT_CHAR;
        meta = c;
        i = midi_readvarlen(midi); /* meta event length */
        node->meta = meta;
        if (i < 0) goto _err_cleanup;
        switch (meta) {
        case midi_eot:
          break;
        case midi_set_tempo:
          if (midi_getnc(midi, buf, 3) != 3) goto _err_cleanup;
          node->param1 = msb24(buf);
          break;
        default:
          /* treated as unknown */
          node->evt = midi_unknown;
          if (midi_seek(midi, i, SEEK_CUR)) goto _err_cleanup;
          break;
        }
        break;
      /* The following are treated as unknown */
      case 0xf2:
        midi_seek(midi, 1, SEEK_CUR);
        /* fall thru */
      case 0xf3:
        midi_seek(midi, 1, SEEK_CUR);
        /* fall thru */
      case 0xf1:  case 0xf4:  case 0xf5:
      case 0xf6:  case 0xf7:  case 0xf8:
      case 0xf9:  case 0xfa:  case 0xfb:
      case 0xfc:  case 0xfd:  case 0xfe:
        node->evt = midi_unknown;
      default:
        /* TODO: Unrecognized */
        break;
      }
      break;
    }
    if (head == NULL)
      head = node;
    else
      tail->next = node;
    tail = node;
    /* If we come across EOT, stop parsing this track */
    if (node->meta == midi_eot) break;
  }
  rebuild_backlink_trk(head);
  /* Ensure to reach the end of track */
  midi_seek(midi, initpos + length, SEEK_SET);
  return head;
_err_cleanup:
  if (node) {
    if (node->parambuf) free(node->parambuf);
    free(node);
  }
  midi_free_trk(head);
  return NULL;
  #undef NEXT_CHAR
}

/* TODO: description */
static void midi_free_trk(midi_evt_node *trk) {
  midi_evt_node *node;
  while (trk != NULL) {
    node = trk;
    trk = trk->next;
    if (node->parambuf) free(node->parambuf);
    free(node);
  }
}

/* Combine 2 tracks*/
/* Both tracks have to be in absolute time */
/* Produces a one-way linked track */
/* Returns the new track */
static midi_evt_node* combine_trk_abstime(midi_evt_node *trk1, midi_evt_node *trk2) {
  midi_evt_node *head, *cur, *tmp;
  /* Two trivial cases */
  if (!trk1) return trk2;
  if (!trk2) return trk1;
  /* Process the first(root) node */
  if (trk1->time > trk2->time) {
    head = trk2;
    trk2 = trk2->next;
  }
  else {
    head = trk1;
    trk1 = trk1->next;
  }
  cur = head;
  /* Process the following */
  while (trk1 || trk2) {
    if (!trk1) {
      cur->next = trk2;
      break;
    }
    if (!trk2) {
      cur->next = trk1;
      break;
    }
    if (trk1->time > trk2->time) {
      cur->next = trk2;
      trk2 = trk2->next;
    }
    else {
      cur->next = trk1;
      trk1 = trk1->next;
    }
    cur = cur->next;
  }
  /* Remove redundant eot messages */
  cur = head;
  while (cur) {
    tmp = cur->next;
    if (tmp && tmp->evt == midi_meta_evt && tmp->meta == midi_eot && tmp->next) {
      cur->next = tmp->next;
      free(tmp);
    }
    cur = cur->next;
  }
  return head;
}

/* Complete double links after one-way links are established */
static void rebuild_backlink_trk(midi_evt_node *trk) {
  midi_evt_node *node;
  node = trk;
  node->prev = NULL;
  while (node) {
    if (node->next) node->next->prev = node;
    node = node->next;
  }
}

midi_file* midi_open_file(char *path) {
  midi_file *midi;
  FILE *stream;
  stream = fopen(path, "rb");
  if (!stream) return NULL;
  if (!alloc(midi, midi_file)) {
    fclose(stream);
    return NULL;
  }
  midi->stream = stream;
  if (midi_parse_hdr(midi)) return NULL;
  return midi;
}

midi_file* midi_open_mem(char *mem, size_t size) {
  midi_file *midi;
  if (!alloc(midi, midi_file)) return NULL;
  midi->stream = NULL;
  midi->mem = mem;
  midi->size = size;
  midi->pos = 0;
  if (midi_parse_hdr(midi)) return NULL;
  return midi;
}

midi_trks* midi_parse_tracks(midi_file *midi) {
  uint32_t length;
  int i;
  char buf[4];
  midi_trks *trks;
  midi_seek(midi, 0, SEEK_SET);
  i = 0;
  if (!alloc(trks, midi_trks) ||
      !(trks->trk = (midi_evt_node**)malloc(midi->hdr.ntrks * sizeof(midi_evt_node*)))
      ) {
    error_log("No Memory");
    goto _err_cleanup;
  }
  trks->ntrks = midi->hdr.ntrks;
  while (!midi_eof(midi)) {
    if (midi_getnc(midi, buf, 4) != 4) {
      if (midi_eof(midi)) break;
      goto _err_cleanup;
    }
    if (!memcmp(MTRK_MAGIC, buf, 4)) {
      if (i >= trks->ntrks) {
        error_log("Bad ntrks in MIDI header");
        goto _err_cleanup;
      }
      if (!(trks->trk[i] = midi_parse_trk(midi))) {
        error_log("Failed to parse track");
        goto _err_cleanup;
      }
      i++;
    }
    else {
      if (midi_getnc(midi, buf, 4) != 4) goto _err_cleanup;
      length = msb32(buf);
      midi_seek(midi, length, SEEK_CUR);
    }
  }
  return trks;
_err_cleanup:
  midi_free_tracks(trks);
  return NULL;
}

void midi_free_tracks(midi_trks *trks) {
  int i;
  if (!trks) return;
  for (i = 0; i < trks->ntrks; i++) {
    midi_free_trk(trks->trk[i]);
  }
  free(trks);
}

void midi_get_header(midi_file *midi, midi_hdr *hdr) {
  memcpy(hdr, &midi->hdr, sizeof(midi_hdr));
}

void midi_combine_tracks(midi_trks *trks) {
  int i;
  midi_convert_abstime(trks);
  for (i = 1; i < trks->ntrks; i++) {
    trks->trk[0] = combine_trk_abstime(trks->trk[0], trks->trk[i]);
    trks->trk[i] = NULL;
  }
  rebuild_backlink_trk(trks->trk[0]);
  trks->ntrks = 1;
}

void midi_convert_abstime(midi_trks *trks) {
  int i, time;
  midi_evt_node *node;
  if (trks->abstime) return;
  for (i = 0; i < trks->ntrks; i++) {
    node = trks->trk[i];
    time = 0;
    while (node) {
      time += node->time;
      node->time = time;
      node = node->next;
    }
  }
  trks->abstime = 1;
}

void midi_convert_deltatime(midi_trks *trks) {
  int i, prevtime, tmp;
  midi_evt_node *node;
  if (!trks->abstime) return;
  for (i = 0; i < trks->ntrks; i++) {
    node = trks->trk[i];
    prevtime = 0;
    while (node) {
      tmp = node->time - prevtime;
      prevtime = node->time;
      node->time = tmp;
      node = node->next;
    }
  }
  trks->abstime = 0;
}

void midi_close(midi_file *midi) {
  if (!midi) return;
  if (midi->stream) fclose(midi->stream);
  free(midi);
}