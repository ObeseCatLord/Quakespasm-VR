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
