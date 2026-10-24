# e4defrag status and verbosity

## Ordinary output and `-v`

Keep the upstream file/progress format. In particular, `-v` adds only the
usual before/after extent counts to completed file lines:

```text
[1/4]data.db:    100%  extents: 48 -> 12    [ OK ]
[2/4]other.db:   100%  extents: 2 -> 2     [ OK ]
```

`[ OK ]` means processing completed. Like upstream, that includes deciding
that the current layout is already good enough or that the donor offers no
improvement. It does not mean that blocks moved. The default output without
`-v` omits extent counts.

Early exclusions retain upstream's verbose filename and reason, for example:

```text
[3/4] "empty.log"
        File size is 0          [ NG ]
[4/4] "directory"
        File is not regular file        [ NG ]
```

Empty, unallocated, one-block, locked, and non-regular entries must not turn
into unexplained `100% [ OK ]` lines. Non-regular entries must not disappear
from `-v` output. These normal exclusions are silent without `-v`; actual
operational failures remain visible at every verbosity.

`[ NG ]` on an upstream-style exclusion is not an operational failure or a
nonzero exit status. The reason distinguishes an exclusion from an error.

## Compact extended output with `-vv`

Keep the filename, extent arrow, and final bracketed status. Add the estimated
best count in parentheses and distinguish movement from skipped work:

```text
[1/4]data.db:    100%  extents: 48 -> 12 (1)    [ MOVED ]
[2/4]other.db:   No better donor layout  extents: 2 -> 2 (1)    [ SKIPPED ]
[3/4]empty.log:  File size is 0    [ SKIPPED ]
[4/4]directory: File is not regular file    [ SKIPPED ]
```

Each file has one final line, including any error or partial-movement detail.
Grepping a filename keeps its result and explanation together. There are no
per-file placement tables, allocation summaries, timings, or settings dumps.
Repeating `-v` more than twice does not enable developer diagnostics.

| Status | Meaning |
| --- | --- |
| `[ MOVED ]` | The operation completed and the kernel reported moving blocks, even if the extent count stayed the same or increased. |
| `[ UNCHANGED ]` | A completed move operation reported moving zero blocks. |
| `[ SKIPPED ]` | Movement was not attempted; the same line gives the reason. |
| `[ FAILED ]` | A required operation failed; the same line identifies the error and any known partial movement. |

The parenthesized count is the existing estimate based on allocated blocks
and filesystem geometry. It falls back to 1 when geometry is unavailable.
It is not a promise that this layout is attainable, especially for sparse
files. Unknown observed extent counts are printed as `unknown`, never as 0.
Both `-v` and `-vv` obtain a final extent count without collecting an extra
full placement map.

`-c` retains its statistics format, including the single-file `-c -v` extent
dump. `-c -vv` uses that same output, without additional placement tables.

## Developer diagnostics

Compile with `-DDEBUG` to collect and print the detailed source/donor/after
placement measurements, allocation requests, moved bytes, timings, retry
counts, and settings. These are development diagnostics, not another runtime
verbosity level. Debug builds can produce additional lines at any verbosity.

The debug measurements distinguish data runs (logical and physical adjacency)
from spacing runs (matching small logical and physical gaps). Before/after
comparisons require unchanged allocated logical coverage and usable FIEMAP
flags. See [donor grouping, holes, and alignment](E4DEFRAG-DONOR-ALLOCATION.md)
for their interpretation and limitations.

## Movement, logging, and errors

`--allow-equal-layout` accepts a donor with fewer or equal contiguous data
runs; `--shake` accepts any donor layout. Both can relocate a file already
at the estimated best count. They change movement policy independently of
verbosity and retain the usual eligibility checks. See
[movement controls](E4DEFRAG-DONOR-ALLOCATION.md#runtime-controls).

Terminal progress redraws the current line. Files and pipes receive final
output without cursor-control sequences or intermediate progress updates.
`TERM=dumb` also selects plain output for terminal recording. Ordinary path
spelling is retained; control bytes in defrag result/progress lines are escaped.

The percentage describes logical processing position, including holes,
not improvement or the fraction of allocated bytes moved. Known partial
movement is retained on error; an uncertain kernel result is identified as
unknown. A failure does not undo exchanges already completed.

Required-operation and traversal failures cause a nonzero exit status even
when other targets succeed. Normal exclusions and optional measurement or
cache-advice warnings do not. The existing Success/Failure, Total extents,
and Fragmented percentage summaries remain; totals use available paired
measurements and exclude non-file entries from file counts.
