# ULP TZX
This is a proof-of concept to see if an ESP32-S3 could play a TZX file using the ULP co-processor.

Pros:
- Precise, per cycle timing in assembler
- Frees up the rest of the device for a potentially resource-heavy GUI

Cons:
- The ESP32-S3 ULP lacks a FIFO, so the bulk of the work is emulating the blocking behaviour of the Raspberry Pi Pico PIO.

## ZX Spectrum timings

The ZX Spectrum runs as follows:
- CPU of 3.5MHz
- Each tick / t-state is 285.7ns
- 1 sec pause == 3,500,000 t-states

For example a pilot tone is as follows:
- 8063 alternating pulses
- Each pulse is 2168 ticks long (619.4us) 
- Waveform is (1614.4 * 2) = 3228.8 Hz
- Therefore should last exactly 4.99445 secs

## ESP32-S3 RISC-V ULP timings

The RISC-v ULP has the following characteristics:
- 17.5MHz - with ~7-9 cycles per instruction
- This means it runs slower than the ZX Spectrum (around 40% from experiments)
- Change the `TZX_TICKS_TO_ULP_LOOPS` ratio value in `ulp_riscv/config.h` if the assembler changes

## Testing

### Audacity 4
Used to read the timing / signal profile

### ZEsarUX

[ZEsarUX](https://github.com/chernandezba/zesarux) has a "real tape" functionality with a tape checker that can use the microphone.

To enable:
```
Audio Menu
--- External Audio Source ---
[X] Enable external Audio Source
```

To load on the 48k: `LOAD ""`. This is done by entering `J`, `Opt+P`, `Opt+P` then `Return` on ZEsarUX.

### Sample Tape

To test, a standard block is used. Note: Each pulse is the **opposite** of the previous.

| Section | Type  | Pulses  | T-States           | Description              |
|---------|-------|---------|--------------------|--------------------------|
| Header  | TONE  | 8063    | 2168               | 4.99sec tone of high/low |
| Header  | SYNC  | 1       | 667                | Pulse LOW                |
| Header  | SYNC  | 1       | 735                | Pulse HIGH               |
| Header  | DATA  | 2 x 152 | 855 = 0 / 1710 = 1 | Two pulses per bit       |
| Header  | PAUSE | 1       | 3,500,000          | 1 second pause           |
| Data    | TONE  | 3223    | 2168               | 1.99sec tone of high/low |
| Data    | SYNC  | 1       | 667                | Pulse LOW                |
| Data    | SYNC  | 1       | 735                | Pulse HIGH               |
| Data    | DATA  | 2 x 120 | 855 = 0 / 1710 = 1 | Two pulses per bit       |
| Data    | PAUSE | 1       | 3,500,000          | 1 second pause           |

Standard Block Header Bytes:
```
0x00, 0x00, 0x48, 0x45, 0x4c, 0x4c, 0x4f, 0x20,
0x20, 0x20, 0x20, 0x20, 0x0d, 0x00, 0x00, 0x80,
0x0d, 0x00, 0xe2
```

Standard Block Data Bytes:
```
0xff, 0x00, 0x0a, 0x09, 0x00, 0xf5, 0x22, 0x48, 
0x45, 0x4c, 0x4c, 0x4f, 0x22, 0x0d, 0x46 
```

## TODO
- Get the ULP to self-terminate / clean-up
  - Seen some issues with changes in tone upon a soft-reset
  - Timer shutdown doesn't seem to work within assembler
- Restart playback using a button
  - Tests the above self-termination / clean-up
  - Helps validate polarity changes
- Integrate into my Raspberry Pi Pico [Tappioca](https://github.com/jlmurdoch/tappioca/tree/main) project
  - This code is 32-bit and can send up to a >1000 msec pulse for pausing
  - Tappioca is 16-bit and can only send a <1.8 msec pulse, but can block to pause
  - Improvment for both: Pilot tone calibration / checking
  - Improvment for both: How long left to play / percentage complete