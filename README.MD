# light-midi-parser

<p>A very simple MIDI file parser which only cares about information commonly used in playing a MIDI file. This project may be buggy. Use at your own risk.</p>

## Supported MIDI Events

|Code (Hex)|Event Name             |
|----------|-----------------------|
|8x        |Note Off               |
|9x        |Note On                |
|Ax        |Polyphonic Key Pressure|
|Bx        |Control Change         |
|Cx        |Program Change         |
|Dx        |Channel Pressure       |
|Ex        |Pitch Wheel Change     |
|F0        |System Exclusive       |
|FF 2F     |META End of Track      |
|FF 51     |META Set Tempo         |

<p>In 8x~Ex, x represents channel no. which can be 0~F.</p>