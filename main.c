#include <stdio.h>
#include "midi.h"

int main(int argc, char* argv[]) {
	int i, j;
	midi_t *midi;
	midi_hdr_t *hdr;
	midi_trk_t *trk;
	midi_evt_node_t *node;
	if (argc != 2) return 1;
	midi = midi_open_file(argv[1]);
	if (!midi) {
		fprintf(stderr, "Failed to open %s\n", argv[1]);
		return 1;
	}
	if (!midi_is_midi_format(midi)) {
		fprintf(stderr, "Not a valid MIDI file\n");
		midi_close(midi);
		return 1;
	}
	if (midi_parse(midi)) {
		fprintf(stderr, "Failed to parse MIDI file\n");
		midi_close(midi);
		return 1;
	}
	//midi_combine_track(midi);
	hdr = midi_get_header(midi);
	printf("format: %d\n", hdr->format);
	printf("ntrks: %d\n", hdr->ntrks);
	printf("division: %d\n", hdr->division);
	for (i = 0; i < hdr->ntrks; i++) {
		printf("=== Track %d ===\n", i);
		trk = midi_get_track(midi, i);
		for (node = trk->first; node != NULL; node = node->next) {
			switch (node->evt) {
			case note_off:
				printf("%d\tnoteoff chan%d\t%d\t%d\n", node->deltatime, node->chan, node->param1, node->param2);
				break;
			case note_on:
				printf("%d\tnoteon  chan%d\t%d\t%d\n", node->deltatime, node->chan, node->param1, node->param2);
				break;
			case poly_key_press:
				printf("%d\tpkpress chan%d\t%d\t%d\n", node->deltatime, node->chan, node->param1, node->param2);
				break;
			case ctrl_change:
				printf("%d\tcc      chan%d\t%d\t%d\n", node->deltatime, node->chan, node->param1, node->param2);
				break;
			case prog_change:
				printf("%d\tpc      chan%d\t%d\n", node->deltatime, node->chan, node->param1);
				break;
			case chan_press:
				printf("%d\tcpress  chan%d\t%d\n", node->deltatime, node->chan, node->param1);
				break;
			case pitch_change:
				printf("%d\tpitch   chan%d\t%d\n", node->deltatime, node->chan, node->param1);
				break;
			case sysex:
				printf("%d\tsysex   ", node->deltatime);
				for (j = 0; j < node->paramsize; j++) printf("%02x ", node->parambuf[j]);
				printf("\n");
			case meta_evt:
				switch (node->meta) {
				case eot:
					printf("%d\teot\n", node->deltatime);
					break;
				case set_tempo:
					printf("%d\ttempo   %d\n", node->deltatime, node->param1);
					break;
				}
				break;
			default:
				printf("%d\tunknown\n", node->deltatime);
				break;
			}
		}
	}
	midi_close(midi);
	return 0;
}