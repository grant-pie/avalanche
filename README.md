# Avalanche

A granular delay module for [VCV Rack](https://vcvrack.com/) by [Knoppies](https://knoppies.grantpieterse.com).

## Description

Avalanche is a granular delay effect that captures incoming audio into a buffer and plays it back as a cloud of overlapping grains. Each grain is a short slice of the recorded buffer played back with its own envelope, allowing for lush, evolving textures ranging from subtle thickening to full granular cloudscapes. Pitch shifting, freezing, and reverse playback are all available per grain.

## Controls

### Parameters

| Knob | Description |
|------|-------------|
| **TIME** | Delay time — sets how far back into the buffer grains are read from (0 to 10 seconds) |
| **SIZE** | Grain size — controls the length of each individual grain (10ms to 500ms) |
| **DENSITY** | Grain density — sets how many new grains are triggered per second (1 to 50 Hz) |
| **PITCH** | Pitch shift — transposes grain playback up or down in octaves (±2 octaves) |
| **SPRAY** | Spray — randomises the read position of each grain, spreading them across the buffer |
| **FEEDBACK** | Feedback — feeds the grain output back into the buffer for accumulating textures |
| **MIX** | Dry/wet mix between the original input and the granular output |

### Buttons

| Button | Description |
|--------|-------------|
| **FREEZE** | Freezes the write position, so grains continue playing from the current buffer contents without recording new audio. Toggles on/off. |
| **REVERSE** | Plays all grains in reverse. Toggles on/off. |

### CV Inputs

All CV inputs accept 0–10V signals.

| Input | Description |
|-------|-------------|
| **TIME** | Modulates the delay time |
| **SIZE** | Modulates the grain size |
| **DENS** | Modulates grain density |
| **V/OCT** | 1V/oct pitch CV — adds to the PITCH knob value |
| **SPRY** | Modulates spray amount |
| **FDBK** | Modulates feedback amount |
| **FRZ** | Gate input — holds FREEZE active while signal is above 1V |
| **REV** | Gate input — holds REVERSE active while signal is above 1V |

Each of the first six CV inputs has a corresponding attenuverter knob on the main panel to scale and invert the incoming CV signal.

### Audio I/O

| Port | Description |
|------|-------------|
| **IN** | Audio input |
| **OUT** | Granular delay output |

## Visualizer

The waveform display shows the contents of the internal delay buffer. The red line indicates the current write position as it moves through the buffer. Yellow dots show the current playback positions of active grains.

## Building

Requires the [VCV Rack SDK](https://vcvrack.com/manual/PluginDevelopmentTutorial).

```bash
make install
```

## License

Proprietary — © Grant Pieterse / Vonk
