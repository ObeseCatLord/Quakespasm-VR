# Private server add-on flow: senior code review

Local Astra (`gpt-6-astra`, max effort) inspected the `2.0` implementation at
`810aadd0` and verified the loader, catalogue and reconnect owners. This was a
static review; it did not run a server or install a package.

| Finding | Disposition |
| --- | --- |
| A downloaded file of the advertised byte count can be renamed to `pak0.pak` without a PACK structure check. The inherited loader can read past its fixed directory array when `dirlen` exceeds its capacity by fewer than one entry. | **Adopt.** Validate the temporary package before publication, and repair the loader's exact byte/range/read checks. Keep the package unpublished and prevent reconnect on rejection. |
| A catalogue refresh can replace an entry between approval comparison and the install job's index lookup. | **Adopt.** Require READY before acceptance, and compare the approved entry while creating the job in the catalogue owner. The mutable `installed` flag is not part of manifest identity. |
| A completed owned refresh can be followed by a different user-started catalogue job before the client observes completion; a plain ownership boolean can cancel that successor. | **Adopt.** Give refresh/install operations a generation identity and cancel only a matching operation. |
| Replace the catalogue worker, network protocol or game-switch owner. | **Reject.** None of the findings requires a parallel subsystem. The existing owner boundaries remain suitable. |

The first package proof should cover a valid PAK, equal-size non-PAK content,
truncated/misaligned directories, capacity-plus-one directory bytes, out-of-file
entry ranges and unterminated names. The approval proof should replace/reorder
the manifest before acceptance and start a successor catalogue operation before
cancel. A passing Linux build alone cannot close these behavior checks.

The separate failed-connect frame-time boundary is recorded in
[the network map](migration-network-map.md#failed-connect-frame-time-boundary).

## Integrated disposition

The installer now validates the temporary PACK with the same on-disk entry
checks used by the mount path. The mount path also rejects directory lengths
that are misaligned, exceed its fixed array, or point outside the file before
reading; both paths check complete reads and bounded entry ranges. The
catalogue owns exact approved-entry comparison at job creation and identifies
refresh/install operations by a generation token, so a stale server operation
cannot cancel a successor. Temporary-file rename/removal use the small
UTF-8-aware `Sys_rename`/`Sys_remove` adapters copied from Ironwail's system
boundary. The client leaves the frozen loading plaque before showing the
consent prompt or advancing a frame-driven reconnect.

Linux SDL2, SDL3 and no-curl full links passed after integration. A
real-binary GDB call accepted a valid synthetic PACK and rejected wrong magic,
misaligned and capacity-plus-one directories, out-of-range entry data, an
unterminated name and a truncated directory. Mounting the capacity-plus-one
fixture in a disposable basedir produced the expected early `Sys_Error`
without reading the oversized directory. Against a disposable loopback
dedicated server, the shared datagram handshake returned an accepted socket
through both frame-driven and ordinary synchronous entry points. An
unanswered local endpoint returned promptly from two frame calls and cancel
released its pending socket. These checks do not prove the complete
server-directed install/reconnect UI flow, packet loss, headset presentation,
or Windows/ARM behavior.
