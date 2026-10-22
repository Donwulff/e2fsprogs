# e4defrag status and verbosity assessment

Assessment of `defrag-mergesort` at `a7f8f5c5`, compared with the supplied
older verbosity patch and the locally merged upstream. This is a proposal;
it does not change program output, accepted options, or allocation policy.

Implementation update: explicit action results, partial-movement reporting,
`-v` measurements, separate outcome counts, and failure-sensitive exit status
are now implemented. Optional measurement failures are warnings. The original
comparison below describes the pre-change behavior; later diagnostic levels
and machine output remain proposals until noted otherwise.

Recommendation: show the action and any failure reason by default, retain
`-v` for measured before/after results, and add `-vv` for allocation and
placement diagnostics. Treat a future machine-readable format as a separate
interface. Successful movement and measured layout improvement are different
facts, especially when preserving sparse-file holes.

## Comparison with the older patch

| Case | Upstream | Supplied patch | Current fork |
| --- | --- | --- | --- |
| Skip because the source meets the estimate or the donor is no better | `[ OK ]` | `[ OK ]`, adds `best` after a literal `%` with `-v` | `[ NOT ]`, adds `best` in parentheses with `-v` |
| Move path completes successfully | `[ OK ]` | `[ FAIL ]`, adds `best` after `/` with `-v` | `[ TRY ]`, adds `best` in parentheses with `-v` |
| Free-space / ownership rejection | Error text with `-v` | Adds unexplained `<` / `>` markers to the extent line | Error text with `-v` |
| Redirected output | Unconditional cursor controls in these paths | Retains those controls | Terminal detection and plain output for files/pipes |

The supplied patch's `[ FAIL ]` is reached after the negative-return checks
and immediately before `succeed_cnt++`. It labels the successful path as a
failure; it does not detect unchanged or worse fragmentation. The commented
`printf` statements do nothing. The extra `%`, `/`, `<`, and `>` characters
are diagnostic markers, not additional measurements.

The current fork already has the useful extra `best` value. Its `[ NOT ]`
and `[ TRY ]` changes came from
[`a55bc0d6`](https://github.com/Donwulff/e2fsprogs/commit/a55bc0d6)
in September 2024, before the recent terminal fixes. The exact supplied
`[ FAIL ]` variant was not found in locally available Git refs; this does
not establish what exists on the other server.

Neither variant covers all outcomes clearly. In the current code:

- `[ NOT ]` merges two decisions: skip before allocating a donor because
  the source meets the estimate, or reject an allocated donor using its
  continuous-region count.
- `[ TRY ]` means the move loop returned success. It does not establish
  that any blocks moved, or that an after-map improved.
- Most pre-move errors are printed only with `-v`. `[ NG ]` covers both
  operational errors and harmless exclusions such as empty files.
- `Success` counts skipped and moved files together. The directory walk's
  `Failure = total_count - succeed_cnt` includes directories and other
  exclusions, so it does not count just failed defragmentations.
- The process can return zero if any target succeeded, despite errors
  elsewhere. Automation cannot interpret zero as "every target succeeded."
- Only `-v` performs the post-move extent-count query. If that query fails,
  the file is not counted as successful, even though movement may already
  have completed. Verbosity currently affects success accounting.

The implementation is in [e4defrag.c](../misc/e4defrag.c), especially
`file_check()`, `file_defrag()`, `call_defrag()`, and `main()`.

## Recommended ordinary output

Use explicit action labels and short reasons. Do not require a verbosity
flag to discover a failed operation or why a file was skipped.

| Proposed label | Meaning |
| --- | --- |
| `MOVED` | The move operation completed and the kernel reported moving some blocks. It makes no performance claim. |
| `UNCHANGED` | The move operation completed but reported zero blocks moved. This does not mean merely that the extent count stayed equal. |
| `SKIPPED` | A policy or eligibility decision avoided moving the file. Always give the reason. |
| `FAILED` | A required operation failed. Identify its stage and error; report any movement already completed. |

Illustrative final lines, not current output:

```text
data.db: MOVED
index.db: SKIPPED (already meets estimated target)
archive.db: SKIPPED (donor not better by current heuristic)
empty.dat: SKIPPED (empty file)
busy.db: SKIPPED (file locked)
other.db: FAILED (allocating donor: No space left on device)
partial.db: FAILED (moving extents: Input/output error; 64 MiB already moved)
```

An empty file, ordinary non-file directory entry, or policy exclusion is
not an I/O failure. An explicit inaccessible target or failed system call
must still be reported as an error. Classify ownership and lock exclusions
explicitly, rather than letting a generic error label decide their meaning.

Keep terminal progress transient. Show phases such as `allocating donor`,
`moving`, and `measuring`; only show a percentage when it has a defined
denominator. The current percentage tracks logical file position, including
holes: it is neither percentage improvement nor a count of bytes moved.
Omit a ceremonial `100%` from final and redirected result lines.

Summaries should count examined regular files by outcome: moved, unchanged,
skipped, and failed. Keep excluded directory entries separate. Report
measurement or cleanup warnings separately from these action counts.

## Verbosity and measurement

| Interface | Recommended information |
| --- | --- |
| Default | Compact action, skip reason or error, outcome summary; live progress on a capable terminal |
| `-v` | Before/after extent counts, bytes actually moved, explicitly labelled estimated target, elapsed time and measurement warnings |
| Proposed `-vv` | Source/donor run counts, grouping and hole totals, allocation sizes, policy settings, retry counts, and measured sparse-placement quality |
| `-c` | Read-only assessment of the existing layout; no claim about a donor that was never allocated |
| Proposed `--json` | Stable result records for automation, separate from human formatting and verbosity |

Today repeated `-v` is just the same boolean flag; `-vv` does not enable a
second level, and `--json` is not accepted.

For example, a proposed `-v` result could be:

```text
data.db: MOVED; extents 48 -> 12; moved 256 MiB; estimated target 1
```

Rename the bare `(1)` or `/ 1` to `estimated target 1`. `get_best_count()`
uses allocated block count and block-group/flex-group geometry. It does
not account for all sparse gaps or current free-space constraints. When
geometry is unavailable it returns 1; identify that as a fallback in
diagnostics. Neither "optimal" nor "best possible" is justified by it.

Keep these measurements distinct:

1. **Extent count:** the number of FIEMAP records. Adjacent records need
   not represent a physical discontinuity. Sparse gaps also require
   separate mappings even with desirable physical spacing.
2. **Contiguous data runs:** sequences continuous in both logical and
   physical position. This is what the current `get_physical_count()`
   attempts to count. A sparse gap breaks a run.
3. **Spacing runs within a source allocation group:** a proposed diagnostic
   for the fork's placement goal. Consecutive mappings belong to the same
   run when their physical displacement matches their logical displacement,
   including holes accepted by the grouping policy.

For the third measurement, the transition from A to B is a break when:

```text
B.physical_start - A.physical_end != B.logical_start - A.logical_end
```

Use mathematical differences here, not unsigned subtraction that could
wrap on a backward physical jump. Compare the same source groups and data
ranges before and after; a change in the file or grouping is a separate
fact. Do not derive improvement by comparing the hole-filled donor's raw
count with the sparse source's raw count.

For example, `extents 48 -> 48; spacing runs 37 -> 3 across 3 groups`
would expose an improvement in the intended spacing that extent count
alone misses. This measurement is not implemented. It describes placement,
not reserved free space or demonstrated read-speed improvement. See
[donor grouping and preserved holes](E4DEFRAG-DONOR-ALLOCATION.md).

The current extent-list representation discards FIEMAP flags. Reliable
placement diagnostics must retain them and mark unknown, delayed, or
otherwise unsuitable mappings as unavailable instead of guessing physical
adjacency. Treat before/after maps of an actively changing file as
observations, not an atomic snapshot. The
[kernel FIEMAP documentation](https://docs.kernel.org/filesystems/fiemap.html)
defines those flags and explicitly allows the file to change between calls.

`-v` need only add the existing kind of count query; full after-maps and
additional placement analysis can be limited to `-vv` or an explicit
measurement request. Basic action status should use the move result,
independently of whether these optional measurements succeed. Print
`after=unknown` with a warning on a measurement failure, not an invented
zero or an implication that movement was undone.

Currently `-c -v` on a single file dumps every extent, whereas a directory
gets a per-file table. Moving the potentially huge extent dump to `-c -vv`
would make levels consistent, but is a deliberate compatibility change
that needs documentation. A normal `-vv` defragmentation summary need not
dump every ioctl or extent; keep that much output explicitly opt-in.

## Partial work, errors, and scripting

Accumulate actual `moved_len` separately from logical progress, including
reported movement from a failing ioctl when available. The
[Linux v7.0 move implementation](https://github.com/torvalds/linux/blob/v7.0/fs/ext4/move_extent.c#L574-L657)
can update `moved_len` and subsequently return an error. Failure does not
imply that all previous extent exchanges were rolled back. If the ioctl
result cannot be read reliably, report the movement total as incomplete.

Track the failed stage: source inspection, eligibility check, donor creation,
allocation, mapping, extent movement, or required synchronization. A cache
advice warning and an error moving blocks are different events. Preserve
the primary error if cleanup also fails, and retain completed movement when
post-move measurement or cleanup reports a problem.

Human logs should contain complete final records, including filenames and
reasons, rather than rely on interleaving an unfinished stdout line with
stderr fragments. Keep cursor controls disabled for files and pipes and
retain the `TERM=dumb` override. Escape control characters in filenames so
one unusual pathname cannot create extra records or terminal commands.

For automation, prefer an explicit future JSON-lines mode over parsing
bracketed human labels. It should emit one versioned result record per file
plus a summary, with fields for operation/mode, action, reason code, failed
stage, errno, moved bytes, measured counts, and warnings. Use null for
unavailable measurements, byte units for lengths, and a documented encoding
for non-UTF-8 pathnames. Suppress banners and progress on this stdout stream.
Machine output is a format choice, not a side effect of output being piped.

Exit status needs its own reviewed change: recommend zero when requested
processing completes without operational or traversal errors, including
normal policy skips; nonzero when any requested operation fails. Optional
measurement warnings must not retroactively change a completed move into a
failed move. Document skip reasons so automation can apply a stricter policy
when desired. This differs from the current "any success" exit behavior.

## Suggested patch boundaries

1. Define per-file outcomes, stages, reasons, moved-block accounting, and
   summary accounting. Cover skips, zero moves, partial moves, and failures.
2. Render clear default and `-v` results from those outcomes; keep the
   terminal/plain-output distinction and document compatibility changes.
3. Add `-vv` allocation diagnostics and separately verified placement
   measurements, including equal-extent-count sparse layouts.
4. Add an explicit machine format and a documented exit-status contract.

Keep allocator policy, donor sizing, and RAID alignment changes separate.
Tests should verify facts and coverage across outcome paths, not just
substitute new labels in expected strings.
