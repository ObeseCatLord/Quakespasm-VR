# Current vkQuake upstream review

Date: 2026-10-10. Official upstream `master` remains
[`749b4fd4`](https://github.com/Novum/vkQuake/commit/749b4fd43fe8b8554b34163dc9469c4a08a4def1).
Fetch, `ls-remote`, and ancestry checks confirm this commit is already included
in `main` through merge `59c4df5c`; there are no new incoming commits.
The latest user README edit (`0567a1dd`) is preserved.

A read-only Astra review used verified effective **gpt-6-astra/xhigh** settings.
It audited the newest five substantive code commits (`9c9c8326`, `fced0f6e`,
`8b27d4f6`, `6b4a163e`, and `749b4fd4`) against their current integrated code.
The main agent independently checked the load-bearing source claims.
The earlier complete merge audit remains documented in
[the integration receipt](upstream-merge-dispositions-2.0.md).

| Finding or recommendation | Disposition |
| --- | --- |
| No incoming upstream history | Retain the existing merge; no empty merge or duplicate cherry-picks. |
| Contrast retained old 0.1 steps and one-decimal formatting | Adopt upstream's 0.05 steps and two-decimal display in the existing graphics-menu adapter. |
| Sky cache cleanup before model destruction, including ownerless textures | Retain the verified upstream ordering and ownership-independent cleanup. |
| Nonzero first vector allocation | Retain upstream allocation; inspected fork appenders initialize their elements. No blanket zero-allocation workaround. |
| Alias vertex diagnostic limit of 2400 | Retain the diagnostic limit separately from the actual engine limit. |
| Controller/slider layout adaptations | Retain current wheel row and VR/menu layout. No concrete defect was found in the inherited slider-label spacing difference. |

## Current checks

- Native Linux debug build passed with `-Wall -Werror`.
- An ASan/UBSan check executed the exact production slider helpers and contrast
  action/display arms, with a stub cvar boundary: keyboard increments, endpoint
  clamping, continuous pointer dragging, and `1.05` display passed.
- Existing graphics-menu layout, MD5 weapon geometry, and mod-browser metadata
  checks passed. The MD5 check did not include optional real-asset cases.

These are bounded code/build checks. They do not establish physical-headset
presentation or Windows/ARM runtime behavior. No deployment or release
publication is part of this update.
