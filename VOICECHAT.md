# Experimental voice chat

The Linux build includes low-latency Opus voice chat by default. The game
receives voice and opens the default microphone for its level meter, but never
transmits until the player opts in:

```
voice_transmit 1
```

Voice activity detection is the default (`voice_mode 0`). For push-to-talk,
use `voice_mode 1` and bind `+voicerecord` to a key or controller button.

Useful controls:

- `voice_receive 0|1`: disable or enable received voice.
- `voice_transmit 0|1`: disarm or arm microphone transmission.
- `voice_vad_sensitivity 0..100`: higher values open the gate more readily.
- `voice_input_gain 0..4`: software gain before metering, VAD, and encoding.
- `voice_volume 0..2`: overall received voice gain.
- `voice_radio_volume 0..2`: centered long-range voice gain.
- `voice_spatial_distance <units>`: distance over which positional voice blends
  into centered radio voice.
- `voice_hud 0|1`: show the mic meter and current talkers.
- `voice_mute <name|slot>`: toggle one player's local mute.
- `voice_player_volume <name|slot> <0..2>`: set one player's local gain.
- `voice_list_devices`: list SDL capture devices.
- `voice_input_device "<name>"; voice_restart`: select and reopen a device.
- `voice_status`: print capture, receive, transmit, mode, and input-level state.

Servers can disable negotiation and relay with `sv_voice 0`. Voice is carried
in bounded, unreliable supplemental datagrams after gameplay snapshots; stale
speech is dropped instead of increasing latency.

For a build without voice or a direct libopus dependency:

```
make -C Quake -f Makefile.linux USE_VOICE=0
```
