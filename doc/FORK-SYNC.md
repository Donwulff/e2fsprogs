# Keeping this fork in sync

The repository is `Donwulff/e2fsprogs`, and its default branch is
`defrag-mergesort`. Its e4defrag changes are kept on that branch.

| Git name | Meaning |
| --- | --- |
| `origin` | Your fork: `https://github.com/Donwulff/e2fsprogs.git` |
| `upstream` | The original project: `https://github.com/tytso/e2fsprogs.git` |
| `defrag-mergesort` | Your local fork branch |
| `origin/defrag-mergesort` | The last fetched state of your branch on GitHub |
| `upstream/master` | The last fetched state of upstream's development branch |

This branch already has published upstream merges. Continue using **merge**:
it preserves the existing commit IDs and combines new upstream work with
your fork's changes. A **rebase** instead replays commits onto a different
base, rewriting their IDs. That is useful for some unpublished feature
branches, but would complicate this branch's existing published history.

## Update the branch

Run these commands from your e2fsprogs checkout. Commit or otherwise save
tracked changes before merging.

```sh
git switch defrag-mergesort
git status --short
git fetch origin
git merge --ff-only origin/defrag-mergesort
git branch backup/defrag-mergesort-$(date +%Y%m%d-%H%M%S)
git fetch upstream
git merge --no-edit upstream/master
```

`fetch` downloads commits and updates the remote-tracking names; it does
not change your working files. The first merge brings in any newer commits
already on your fork. `--ff-only` stops if local and remote histories have
diverged, so you can inspect that situation before combining them.

The backup branch preserves the starting commit. The final merge combines
the current upstream development branch with your fork and normally creates
a merge commit. Upstream development can include changes newer than the
latest release tag; inspect what you fetched with:

```sh
git log -1 --oneline upstream/master
git describe --tags --match 'v[0-9]*' upstream/master
git rev-list --left-right --count HEAD...upstream/master
```

After a successful merge, the second count from `rev-list` is zero: there
are no upstream commits missing from your branch. The first count includes
your fork's own commits, which are expected to remain.

If `upstream` is not configured in a fresh clone, add it once:

```sh
git remote add upstream https://github.com/tytso/e2fsprogs.git
```

## Resolve a conflict

Use `git status` to identify conflicted files, edit them to combine the
intended changes, and remove the conflict markers. Stage only the files
you resolved, then finish the merge:

```sh
git add path/to/resolved-file
git commit
```

`git merge --abort` cancels an unfinished merge and returns to its starting
state. A backup branch remains useful after a merge has been committed.

The fork description lives in `.github/README.md`. The root `README` is the
upstream file and should remain unchanged by fork-only documentation edits.
This avoids the repeated conflict when upstream updates its release number.
GitHub displays the README from `.github` ahead of the root README.

## Review the intended changes

To review an upstream merge, compare its result with its **first parent**,
which is the fork immediately before that merge. For the October 2026 sync:

```sh
git diff --stat c78515c2^1 c78515c2
git diff c78515c2^1 c78515c2
```

Using `git diff upstream/master...HEAD` answers a different question: it
shows everything the fork adds to upstream, including old features and
workflows. That is useful for a whole-fork review, but does not isolate a
new upstream merge. Use explicit commit IDs when later fixes have already
been added on top of the merge.

## Build, test, and publish

An out-of-tree build keeps generated files separate from the checkout:

```sh
sync_source=$PWD
sync_build=$(mktemp -d /tmp/e2fsprogs-build.XXXXXX)
cd "$sync_build"
"$sync_source/configure" CFLAGS='-O2 -g -Wall'
make -j8 check CFLAGS_WARN=-Werror
cd "$sync_source"
```

After the build and tests pass, review the branch and publish it:

```sh
git diff --check
git status --short
git push origin defrag-mergesort
```

Review the build and test results before pushing. These commands build and
test the programs without installing them on the system. Some optional
components and tests depend on available libraries and kernel features.

`push` publishes your branch on your GitHub fork. A normal push is enough
after this merge workflow; it does not require rewriting the remote history.

See [GitHub's fork synchronization guide](https://docs.github.com/en/pull-requests/how-tos/work-with-forks/syncing-a-fork)
and [README selection documentation](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes).
