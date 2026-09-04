# Experimental voice chat

The Linux and Windows builds include low-latency Opus voice chat by default.
The game receives voice without opening the microphone, and only opens the
selected capture device after the player opts in:

```
voice_transmit 1
```

Voice activity detection is the default (`voice_mode 0`). For push-to-talk,
use `voice_mode 1` and bind **push to talk** in the Controls menu (or bind
`+voicerecord` directly from the console).

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

The same common controls are available under **Options > Voice chat options**.
Changing the input device there reopens capture immediately. In co-op, an
actively speaking player's existing world-space nametag changes from
`playername` to `((playername))` as an additional positional indicator.

Servers can disable negotiation and relay with `sv_voice 0`. Voice is carried
in bounded, unreliable supplemental datagrams after gameplay snapshots; stale
speech is dropped instead of increasing latency.

The microphone gate and voice HUD stay inactive outside a negotiated
multiplayer connection, including disconnected and single-player games.

For a build without voice or a direct libopus dependency:

```
make -C Quake -f Makefile.linux USE_VOICE=0
```
