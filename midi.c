#include "midi.h"

#define MAX_NUM_OF_TRKS 32
#define MAX_SYSEX_DATA_LEN 128

typedef struct midi_s {
	FILE *stream; /* Valid if loaded from file, otherwise NULL */
	char *mem; /* Valid if loaded from memory */
	size_t size; /* Valid if loaded from memory */
	long pos; /* Valid if loaded from memory */
	midi_hdr_t hdr;
	midi_trk_t trks[MAX_NUM_OF_TRKS];
} midi_t;

const char MTHD_MAGIC[] = {'M','T','h','d'};
const char MTRK_MAGIC[] = {'M','T','r','k'};

#define malloc_t_zero(x, t) (((x) = ((t*)malloc(sizeof(t)))) != NULL && memset((x), 0, sizeof(t)) != NULL)

#define msb32(b) ((b)[0] & 0xff) << 24 | ((b)[1] & 0xff) << 16 | ((b)[2] & 0xff) << 8 | ((b)[3] & 0xff)
#define msb24(b) ((b)[0] & 0xff) << 16 | ((b)[1] & 0xff) << 8 | ((b)[2] & 0xff)
#define msb16(b) ((b)[0] & 0xff) << 8 | ((b)[1] & 0xff)

static int midi_eof(midi_t *midi);
static int midi_getc(midi_t *midi);
static int midi_getnc(midi_t *midi, char* buf, int nbytes);
static int midi_seek(midi_t *midi, long pos, int whence);
static long midi_tell(midi_t *midi);
static int midi_readvarlen(midi_t *midi);
static int midi_parse_hdr(midi_t *midi);
static int midi_parse_trk(midi_t *midi, int trkno);

static int midi_eof(midi_t *midi) {
	if (midi->stream) {
		return feof(midi->stream);
	}
	else {
		if (midi->pos >= midi->size) return 1;
		return 0;
	}
}

/* Returns -1 on error */
static int midi_getc(midi_t *midi) {
	if (midi->stream) {
		return fgetc(midi->stream);
	}
	else {
		if (midi->pos >= midi->size) return -1;
		return *(midi->mem + midi->pos++) & 0xff;
	}
}

/* Returns num of read bytes */
static int midi_getnc(midi_t *midi, char* buf, int nbytes) {
	int i, c;
	for (i = 0; i < nbytes; i++) {
		if ((c = midi_getc(midi)) < 0) break;
		buf[i] = (char)c;
	}
	return i;
}

/* Returns non-0 on error */
static int midi_seek(midi_t *midi, long pos, int whence) {
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

static long midi_tell(midi_t *midi) {
	if (midi->stream) {
		return ftell(midi->stream);
	}
	else {
		return midi->pos;
	}
}

/* Returns -1 on error */
static int midi_readvarlen(midi_t *midi) {
	int value = 0;
	int c;
	do {
		value <<= 7;
		c = midi_getc(midi);
		if (c < 0) return -1;
		value |= c & 0x7f;
	} while (c & 0x80);
	return value;
}

/* The function assumes pos points to the length after MThd */
/* Returns non-0 on error */
static int midi_parse_hdr(midi_t *midi) {
	uint32_t length;
	int i, c;
	char buf[4];
	if (midi_getnc(midi, buf, 4) != 4) return -1;
	length = msb32(buf);
	if (midi_getnc(midi, buf, 2) != 2) return -1;
	midi->hdr.format = msb16(buf);
	if (midi_getnc(midi, buf, 2) != 2) return -1;
	midi->hdr.ntrks = msb16(buf);
	if (midi_getnc(midi, buf, 2) != 2) return -1;
	midi->hdr.division = msb16(buf);
	return midi_seek(midi, length - 6, SEEK_CUR);
}

/* The function assumes pos points to the length after MTrk */
/* Returns non-0 on error */
static int midi_parse_trk(midi_t *midi, int trkno) {
	char buf[4];
	uint32_t length;
	long initpos;
	int loopflag = 1;
	int deltatime, c, i;
	midi_evt_t evt = 0;
	midi_meta_evt_t meta;
	midi_evt_node_t *node, *tail;
	if (midi_getnc(midi, buf, 4) != 4) return -1;
	length = msb32(buf);
	initpos = midi_tell(midi);
	node = NULL;
	while (loopflag && midi_tell(midi) - initpos < length) {
		deltatime = midi_readvarlen(midi);
		if (deltatime < 0) return -1;
		c = midi_getc(midi);
		if (c < 0) return -1;
		if (c & 0x80) {
			evt = c;
			if ((evt & 0xf0) != 0xf0) {
				c = midi_getc(midi);
				if (c < 0) return -1;
			}
		}
		switch (evt & 0xf0) {
		case note_off:
		case note_on:
		case poly_key_press:
		case ctrl_change:
			if (!malloc_t_zero(node, midi_evt_node_t)) return -1;
			node->evt = evt & 0xf0;
			node->chan = evt & 0xf;
			node->param1 = c;
			c = midi_getc(midi);
			if (c < 0) return -1;
			node->param2 = c;
			break;
		case prog_change:
		case chan_press:
			if (!malloc_t_zero(node, midi_evt_node_t)) return -1;
			node->evt = evt & 0xf0;
			node->chan = evt & 0xf;
			node->param1 = c;
			break;
		case pitch_change:
			if (!malloc_t_zero(node, midi_evt_node_t)) return -1;
			node->evt = evt & 0xf0;
			node->chan = evt & 0xf;
			node->param1 = c;
			c = midi_getc(midi);
			if (c < 0) return -1;
			node->param1 |= c << 7;
			break;
		default:
			switch (evt) {
			case sysex:
				if (!malloc_t_zero(node, midi_evt_node_t)) return -1;
				node->evt = evt;
				if ((node->parambuf = malloc(MAX_SYSEX_DATA_LEN)) == NULL) {
					free((void*)node);
					return -1;
				}
				c = midi_getc(midi);
				if (c < 0) return -1;
				for (i = 0; i < MAX_SYSEX_DATA_LEN && c != 0xf7; i++) {
					*(node->parambuf + i) = (char)c;
					c = midi_getc(midi);
					if (c < 0) return -1;
				}
				if (c != 0xf7) {
					free((void*)node->parambuf);
					free((void*)node);
					return -1;
				}
				node->paramsize = i;
				break;
			case 0xf1:	case 0xf4:	case 0xf5:
			case 0xf6:	case 0xf7:	case 0xf8:
			case 0xf9:	case 0xfa:	case 0xfb:
			case 0xfc:	case 0xfd:	case 0xfe:
				break;
			case 0xf2:
				midi_seek(midi, 2, SEEK_CUR);
				break;
			case 0xf3:
				midi_seek(midi, 1, SEEK_CUR);
				break;
			case meta_evt:
				c = midi_getc(midi);
				if (c < 0) return -1;
				meta = c;
				i = midi_readvarlen(midi); /* meta event length */
				if (i < 0) return -1;
				switch (meta) {
				case eot:
					if (!malloc_t_zero(node, midi_evt_node_t)) return -1;
					node->evt = meta_evt;
					node->meta = meta;
					loopflag = 0;
					break;
				case set_tempo:
					if (!malloc_t_zero(node, midi_evt_node_t)) return -1;
					node->evt = meta_evt;
					node->meta = meta;
					if (midi_getnc(midi, buf, 3) != 3) {
						free((void*)node);
						return -1;
					}
					node->param1 = msb24(buf);
					break;
				default:
					if (midi_seek(midi, i, SEEK_CUR)) return -1;
					break;
				}
				break;
			default:
				/* TODO: Unrecognized */
				break;
			}
			break;
		}
		if (node) {
			node->deltatime = deltatime;
			if (midi->trks[trkno].first == NULL) 
				midi->trks[trkno].first = node;
			else
				tail->next = node;
			tail = node;
			node = NULL;
		}
	}
	return 0;
}

midi_t* midi_open_file(char *path) {
	midi_t *midi;
	FILE *stream;
	stream = fopen(path, "rb");
	if (!stream) return NULL;
	if (!malloc_t_zero(midi, midi_t)) {
		fclose(stream);
		return NULL;
	}
	midi->stream = stream;
	return midi;
}

midi_t* midi_open_mem(char *mem, size_t size) {
	midi_t *midi;
	if (!malloc_t_zero(midi, midi_t)) return NULL;
	midi->stream = NULL;
	midi->mem = mem;
	midi->size = size;
	midi->pos = 0;
	return midi;
}

int midi_is_midi_format(midi_t *midi) {
	int i, ret;
	ret = 1;
	for (i = 0; i < 4; i++) {
		if (midi_getc(midi) != MTHD_MAGIC[i]) {
			ret = 0;
			break;
		}
	}
	midi_seek(midi, 0, SEEK_SET);
	return ret;
}

int midi_parse(midi_t *midi) {
	int trkno = 0;
	char buf[4];
	while (!midi_eof(midi)) {
		if (midi_getnc(midi, buf, 4) != 4) {
			if (midi_eof(midi)) return 0;
			return -1;
		}
		if (!memcmp(MTHD_MAGIC, buf, 4)) {
			if (midi_parse_hdr(midi)) return -1;
			if (midi->hdr.ntrks > MAX_NUM_OF_TRKS) return -1;
		}
		else if (!memcmp(MTRK_MAGIC, buf, 4)) {
			if (trkno >= midi->hdr.ntrks) return -1;
			if (midi_parse_trk(midi, trkno++)) return -1;
		}
		else {
			/* TODO: skip */
		}
	}
	return 0;
}

midi_hdr_t* midi_get_header(midi_t *midi) {
	return &midi->hdr;
}

midi_trk_t* midi_get_track(midi_t *midi, int ntrk) {
	return &midi->trks[ntrk];
}

void midi_close(midi_t *midi) {
	int i;
	midi_evt_node_t *node, *tmp;
	if (!midi) return;
	for (i = 0; i < midi->hdr.ntrks; i++) {
		node = midi->trks[i].first;
		while (node != NULL) {
			if (node->parambuf) free((void*)node->parambuf);
			tmp = node->next;
			free((void*)node);
			node = tmp;
		}
	}
	if (midi->stream) fclose(midi->stream);
	free((void*)midi);
}