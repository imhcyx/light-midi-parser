#include <stdio.h>
#include "midi.h"

int main(int argc, char* argv[]) {
  int i, j;
  midi_file *midi;
  midi_hdr hdr;
  midi_trks *trks;
  midi_evt_node *node;
  if (argc != 2) return 1;
  midi = midi_open_file(argv[1]);
  if (!midi) {
    fprintf(stderr, "Failed to open %s\n", argv[1]);
    return 1;
  }
  trks = midi_parse_tracks(midi);
  if (!trks) {
    fprintf(stderr, "Failed to parse MIDI file\n");
    midi_close(midi);
    return 1;
  }
  /*
  midi_combine_tracks(trks);
  midi_convert_deltatime(trks);
  */
  midi_get_header(midi, &hdr);
  printf("format: %d\n", hdr.format);
  printf("ntrks: %d\n", hdr.ntrks);
  printf("division: %d\n", hdr.division);
  for (i = 0; i < trks->ntrks; i++) {
    printf("=== Track %d ===\n", i);
    for (node = trks->trk[i]; node != NULL; node = node->next) {
      switch (node->evt) {
      case midi_noteoff:
        printf("%d\tnoteoff chan%d\t%d\t%d\n", node->time, node->chan, node->param1, node->param2);
        break;
      case midi_noteon:
        printf("%d\tnoteon  chan%d\t%d\t%d\n", node->time, node->chan, node->param1, node->param2);
        break;
      case midi_poly_key:
        printf("%d\tpkpress chan%d\t%d\t%d\n", node->time, node->chan, node->param1, node->param2);
        break;
      case midi_cc:
        printf("%d\tcc      chan%d\t%d\t%d\n", node->time, node->chan, node->param1, node->param2);
        break;
      case midi_pc:
        printf("%d\tpc      chan%d\t%d\n", node->time, node->chan, node->param1);
        break;
      case midi_chan_press:
        printf("%d\tcpress  chan%d\t%d\n", node->time, node->chan, node->param1);
        break;
      case midi_pitch:
        printf("%d\tpitch   chan%d\t%d\n", node->time, node->chan, node->param1);
        break;
      case midi_sysex:
        printf("%d\tsysex   ", node->time);
        for (j = 0; j < node->paramsize; j++) printf("%02x ", node->parambuf[j]);
        printf("\n");
      case midi_meta_evt:
        switch (node->meta) {
        case midi_eot:
          printf("%d\teot\n", node->time);
          break;
        case midi_set_tempo:
          printf("%d\ttempo   %d\n", node->time, node->param1);
          break;
        }
        break;
      default:
        printf("%d\tunknown\n", node->time);
        break;
      }
    }
  }
  midi_free_tracks(trks);
  midi_close(midi);
  return 0;
}