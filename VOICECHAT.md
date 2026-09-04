# Experimental voice chat

The Linux and Windows builds include low-latency Opus voice chat by default.
In VR (`-vr`), microphone transmission is enabled by default and uses the
headset microphone when the runtime/device identity can be matched. If the
headset microphone cannot be identified, choose **Input device** under
**Options > Voice chat options**; the game never substitutes an unrelated
system microphone. Disabling transmission is remembered.

Desktop players must choose an **Input device** in that menu before microphone
capture starts. The chosen microphone, transmit setting, and VAD/PTT mode are
remembered separately for desktop and VR across restarts and mod switches.
A saved microphone that is missing or ambiguous stays unavailable until it is
reconnected or a different device is selected.

Voice activity detection is the default. For push-to-talk, select
**Push-to-talk** in the voice menu and assign **push to talk** to a physical
key/controller button in **Controls** once. That approval is remembered too.
Existing console or config bindings alone cannot grant permission because
servers can also change bindings. Running `+voicerecord` as a console command or through an alias cannot
press PTT. Physical release always stops PTT, including after a menu opens or a
binding changes while held.

Useful controls:

- `voice_receive 0|1`: disable or enable received voice.
- `voice_transmit`: report microphone permission (read-only; change it in the menu).
- `voice_mode`: report VAD/PTT mode (read-only; change it in the menu).
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
- `voice_input_device`: report the selected microphone (read-only; select it in the menu).
- `voice_restart`: retry the selected device.
- `voice_status`: print capture, receive, transmit, mode, and input-level state.

The same common controls are available under **Options > Voice chat options**.
Changing the input device there reopens capture immediately. In co-op, an
actively speaking player's existing world-space nametag changes from
`playername` to `((playername))` as an additional positional indicator.

Microphone preferences use a separate `voice-settings.dat` file in the operating
system's per-user Quakespasm/QuakespasmVR preferences directory, outside mod
configs and the game's search paths. Only local menu choices save this file;
server commands cannot enable transmission, replace the chosen microphone,
change VAD/PTT mode, or approve new PTT buttons. A desktop launch cannot acquire
automatic headset-microphone permission through a server's `vr_enabled` command.

Servers can disable negotiation and relay with `sv_voice 0`. Voice is carried
in bounded, unreliable supplemental datagrams after gameplay snapshots; stale
speech is dropped instead of increasing latency.

The microphone gate and voice HUD stay inactive outside a negotiated
multiplayer connection, including disconnected and single-player games.

For a build without voice or a direct libopus dependency:

```
make -C Quake -f Makefile.linux USE_VOICE=0
```
