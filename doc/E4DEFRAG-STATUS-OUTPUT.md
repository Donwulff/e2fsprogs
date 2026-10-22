# e4defrag status and verbosity

Normal output and `-v` retain the familiar upstream presentation. The fork's
additional action statuses and placement diagnostics are selected with
`-vv`. This supersedes the earlier proposal to replace ordinary output with
named fields separated by semicolons.

## Normal output and `-v`

A typical `-v` result remains:

```text
[1/1]data.db:    100%  extents: 48 -> 12    [ OK ]
```

The filename/progress prefix, `extents: before -> after`, and `[ OK ]` /
`[ NG ]` markers are retained. Normal output without `-v` omits the extent
counts. Ordinary filename spelling is retained; control bytes in defrag
result/progress lines are escaped. The `-c` tables and the single-file
`-c -v` extent dump also retain their existing presentation.

`[ OK ]` means processing succeeded, including a normal decision to skip
movement. It does not establish that blocks moved or access became faster.
`[ NG ]` means a required operation failed; the following explanation names
its stage and error. Known partial movement is always reported, regardless
of verbosity. Errors are not hidden behind `-v`.

Terminal progress updates the current line and never jumps to a fixed screen
row. Files and pipes receive final records without cursor-control sequences
or intermediate progress updates. `TERM=dumb` forces plain output for a
terminal recorder such as Screen's built-in logging.

The percentage follows logical file position, including holes. It describes
processing progress, not percentage improvement or the share of allocated
bytes moved. A skipped file can finish processing at 100% without movement.

The `Success`, `Failure`, `Total extents`, and `Fragmented percentage` summary
labels are retained. Success counts completed or skipped regular files;
non-file directory entries no longer inflate the Failure count. Extent totals
and percentages use files with available before/after observations, and a
separate message identifies missing measurements. These bookkeeping fixes
can change numbers compared with older runs.

## Extended output with `-vv`

The same result line adds an explicitly labelled estimate and an action:

```text
[1/1]data.db:    100%  extents: 48 -> 12 (estimated best: 1)    [ MOVED ]
```

| Status | Meaning |
| --- | --- |
| `[ MOVED ]` | The operation completed and the kernel reported moving blocks. |
| `[ UNCHANGED ]` | The operation completed but reported moving zero blocks. An equal before/after extent count alone does not cause this status. |
| `[ SKIPPED ]` | Movement was not attempted because of eligibility or policy; the next line gives the reason. |
| `[ FAILED ]` | A required operation failed; any known movement before failure remains visible. |

A compact table compares the source, donor and observed result:

```text
        Layout         Extents   Data runs   Spacing runs
        Before              48          37             37
        Donor                3           3              3
        After               48          37              3
```

This is an illustrative sparse layout, not a benchmark result. Additional
lines describe source data size, bridged holes, allocation groups, donor
requests and their total/largest sizes, moved data and elapsed time, move
calls, mapping retries, and effective hole/window/padding settings. Sizes
use familiar binary units. There is no machine-style key/value record.

The estimated best count uses allocated blocks and block-group/flex-group
geometry. It does not describe all sparse-file constraints or available
free space. When geometry is unavailable, the value 1 is explicitly labelled
as a fallback. The estimate is not a guarantee of the optimal layout.

`-v` obtains a final extent count; `-vv` obtains a full after-map to calculate
placement diagnostics. A failed optional measurement prints a warning and
an unknown count instead of changing a completed move into a failure.
`-c -vv` adds layout measurements while remaining read-only.

## Relocation without an extent-count improvement

Movement policy is independent of verbosity:

| Option | Donor accepted by the current run-count heuristic |
| --- | --- |
| Default | Fewer contiguous data runs; skip files already at the estimated best count |
| `--allow-equal-layout` | Fewer or equal runs, including files already at the estimate |
| `--shake` | Any count, including a worse layout |

Both relocation options consider single-block files too, and retain the
eligibility checks. They are mutually exclusive and cannot accompany `-c`.
The existing grouped allocation and hole-preservation policy is unchanged.
See [movement controls](E4DEFRAG-DONOR-ALLOCATION.md#runtime-controls).

`-vv` names the selected policy. A file successfully relocated from 12
extents to 12, or from 12 to 20 under `--shake`, still reports `[ MOVED ]`.
`[ UNCHANGED ]` means the kernel reported zero blocks moved, not an unchanged
extent count. Ordinary `-v` retains `[ OK ]` and displays the observed
before/after counts. A kernel rejection remains an error with every policy.

## What the measurements mean

- **Extents** count FIEMAP mapping records. Adjacent records need not imply
  a physical discontinuity. A sparse gap needs separate mappings even when
  surrounding data have useful physical spacing.
- **Data runs** require continuity in both logical and physical offsets.
  A sparse gap breaks a data run.
- **Spacing runs** allow a small logical hole when the physical gap has the
  same size. They are bounded by the source grouping policy: gaps of eight
  or more filesystem blocks start another group.

Within a group, consecutive mappings have the desired spacing when:

```text
B.physical_start - A.physical_end == B.logical_start - A.logical_end
```

The implementation avoids unsigned wraparound on backward physical jumps.
Before/after comparisons require the same allocated logical coverage;
splitting an extent does not by itself change that coverage. A changed
coverage is explicitly reported as not comparable. Unknown, delayed,
unaligned or otherwise unsuitable FIEMAP mappings cannot establish physical
placement and display unknown run counts.

These are observations of an online filesystem. The
[kernel FIEMAP documentation](https://docs.kernel.org/filesystems/fiemap.html)
allows a file to change between queries. A lower count is not a measured
speed improvement. Spacing across holes does not reserve those free blocks.
See [donor grouping and preserved holes](E4DEFRAG-DONOR-ALLOCATION.md).

The donor can include allocation over source holes. Its raw extent or run
count is therefore not interchangeable with the source's count. Donor
comparison remains the existing candidate-selection heuristic; the after-map
reports the actual source layout observed after movement.

## Failure and script behavior

Movement counts accumulate the kernel's `moved_len`, separately from logical
progress. The [Linux move implementation](https://github.com/torvalds/linux/blob/v7.0/fs/ext4/move_extent.c#L574-L657)
can report partial movement and return an error. Prior exchanges are not
rolled back. If the result cannot be copied back reliably, output identifies
the known minimum and says the total movement is unknown.

Required-operation and traversal failures produce a nonzero exit status,
even when other targets succeeded. Normal policy skips and optional
measurement/cache-advice warnings do not. This fixes the old "any success"
exit behavior. The primary move failure is preserved if synchronization or
cleanup also fails.

Ordinary line markers are retained to minimize disruption to existing grep
usage. A formal machine-readable format remains future work; no `--json`
option is implemented, and redirection does not implicitly select one.

## Why the older verbosity patch was not applied

The supplied alternate patch adds the same estimated best count already
present in the fork, but uses unexplained `%`, `/`, `<`, and `>` markers.
Its `[ FAIL ]` is on the successful move path immediately before
`succeed_cnt++`; it does not detect failed or unhelpful movement.

The fork's older `[ NOT ]` / `[ TRY ]` labels came from
[`a55bc0d6`](https://github.com/Donwulff/e2fsprogs/commit/a55bc0d6) in September
2024. They distinguished branch execution but not all actual outcomes.
The exact supplied `[ FAIL ]` variant was not found in locally available
Git refs; the other server's checkout was not inspected.
