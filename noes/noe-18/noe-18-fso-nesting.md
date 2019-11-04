# NOE-18 -- Git Filesystem Observer Nesting
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v2, 2018-09-28

2019-10-28: NOE-18 contains ideas that are actively used in Nog FSO.

For the initial implementation, see commits in roughly one week up to
`fuimages_nog_2016/next@c256992884f75e7bb4ac7972a9c3672aa0bbcf02` 2018-03-26
'Tie next: p/merge-accounts'.

For toplevel summary stat information and `ignore-most`, see
`fuimages_nog_2016/p/fso-ignore-most@0d6404d97d1834d5fd0fa027df40b0d90e2bcde9`
2018-09-27 'Tie p/fso-ignore-most: ready for next'.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-18 is an amendment to NOE-13.  NOE-13 describes the general Git filesystem
observer design.  NOE-18 describes how to handle nested shadow repos.

Related NOEs:

* [NOE-13](./../noe-13/noe-13-git-fso.md) -- Git Filesystem Observer

## Motivation

Assume the following directory layout:

```
/projects/foo
/projects/foo/2017/bar-data
```

We would like to create an overview repo for `foo` and a repo that contains
details for `bar-data`.  This requires nesting repos below other repos, similar
to filesystem directories or Git submodules.  The design in NOE-13 does not
describe how to do that.

NOE-18 amends the design to support nested repos.

## Design

### Shadow filesystem layout

The shadow tree uses repo UUIDs to allow nesting:

```
realdir: /projects/foo
realdir: /projects/foo/2017/bar-data

shadow: /shadow/projects/foo/<uuid>.fso
shadow: /shadow/projects/foo/2017/bar-data/<uuid>.fso
```

By using a separate `<uuid>.fso` directory for the shadow repo, the directory
that corresponds to the realdir is free to contain subdirectories for nesting.

### Tree stat information

When nesting repos, realdirs may be covered by multiple shadow repos.  Repos
close to the root may cover large subtrees.  To manage repo responsibility and
size, stat information for entire trees can be tracked instead of the
individual files.  There are the following mechanisms:

* Subdirectories that are Git repos are detected by Git and added as submodules
  without entering the directory.  The submodule information is converted as
  described below.
* Nogbundles can be explicitly configured to track tree summary information
  instead of individual files.  See specifying nogbundles below.
* The toplevel tree stat information always includes tree summary information
  for the entire realdir tree.
* A repo can be configured to ignore most files below, so that the Git tree
  size is independent of the number of files in the repo toplevel.

Git submodules and nogbundles are both represented as Git blobs on
`master-stat`.  We cannot use Git trees for submodules, since trees that point
into submodules would confuse Git later.  Since submodules and nogbundles serve
a similar purpose, we use Git blobs for nogbundles, too.

The Git blob contains information about the number of directories, regular
files, symlinks, and other files and the total size of all unique regular file
inodes.  For Git submodules, the Git commit hash is also recorded.  Example:

```
name: "2017"
mtime: 1429250263
dirs: 307
files: 350
links: 0
others: 0
size: 337387393538
submodule: "460621bc552cb53c75ac9e86d37fccf5ced88127"  # Only if Git repo.
```

If the realdir itself is a Git repository, tree stat information about the
`.git` are recorded in the toplevel `.nogtree` in fields `git_*`.  Example:

```
name: "root"
mtime: 1520962272
dirs: 51
files: 51
links: 0
others: 0
size: 203073009234
git: "5e1b51c8f7bf3e8abc45d6bf51c2ff2f187e8fbf"
git_mtime: 1520951711
git_dirs: 41
git_files: 41
git_links: 0
git_others: 0
git_size: 116072009234
```

### Specifying nogbundles

Nogbundles are specified as follows.

The nogbundle paths are configured in `.gitignore` to be ignored in normal Git
operations.  Example `.gitignore`:

```
/data/**
```

The file `.nogbundles` contains glob patterns that select nogbundle candidate
directories.  Parent directories must be selected in order to find their
children.  Example `.nogbundles`:

```
/data/
/data/*/
```

Directories that should be tracked as a nogbundle have a Git attribute
`nogbundle` for the nogbundle path with a trailing slash.  Example
`.gitattributes:

```
/data/*-data/ nogbundle
```

### Discovering nested repos

The root repo naming `PathPatterns` uses a new action `superrepo` to select
a directory as a repo and also enter it.  The root directory itself may be
a repo.  The root is indicated by `"."` when a relative path is used.

Example `patterns` for root path `/project`:

```
superrepo .
    enter data
     repo data/*
superrepo data2
     repo data2/*
   ignore *
```

Example repo candidates:

```
/project
/project/data/foo
/project/data2
/project/data2/bar
```

`/projects/data` is not a repo candidate.

### Init strategies

There are several strategies to avoid tracking entire trees in super-repos.

If the real tree uses a hierarchy of Git repos, tracking will automatically
stop at sub-module boundaries.  The shadow repos will contain a similar number
of files as the Git repos in the real tree.

A shadow repo can be initialized with `git-fso init --ignore-most` to ignore
all files except for a number of well-known files.  It is equivalent to:

```bash
cat >'.gitignore' <<\EOF
*
!.gitignore
!.gitattributes
!.nogbundles
!README.md
!README.txt
EOF
```

A shadow repo can be initialized with `git-fso init --ignore-subdirs` to ignore
all sub-directories.  It is equivalent to:

```bash
echo '/*/' >'.gitignore'
```

A shadow repo can be initialized with `git-fso init --bundle-subdirs` to handle
all sub-directories as nogbundles:

```bash
echo '/*/' >'.gitignore'
echo '/*/ nogbundle' >'.gitattributes'
echo '/*/' >'.nogbundles'
```

The FSO roots use a new mechanism to specify init policies.  `nogfsoregd`
determines the init strategy and sets in in the repo init event.  `nogfsostad`
then passes the strategy to `git-fso init`.  The policy is specified as a list
of glob patterns.  The first match decides.  If no glob matches, the default is
`ignore-most`.  Example init policy config:

```
bundle-subdirs .
ignore-subdirs data2
ignore-subdirs data3
 enter-subdirs data/*
 enter-subdirs data2/*
   ignore-most data3/*
```

The default for roots without explicit init policy configuration is
`enter-subdirs`, which indicates `git-fso init` without option, for backward
compatibility.

## How we introduce this

Use nesting and tree stats for real trees that use a hierarchy of Git repos.

Then implement init strategies and try them:

* Try `bundle-subdirs` for directories with legacy data.
* Try `ignore-most` for BCPFS backup by admins.

## Limitations

It is obvious how to track all files below a directory or how to track none and
store only summary tree stat information.  The two approaches, however, cannot
be mixed.  We considered an overlay mechanism that would allow tracking stat
for some files within a nogbundle.  Metadata paths could perhaps be allowed to
point into the nogbundle.  But such a design could cause substantial confusion.
We abandoned the idea for now.

With nesting, real dirs can be covered by several shadow repos.  This may cause
confusion.  For example, which shadow repo is responsible for archiving?  We
ignore the question for now.  But we should be careful not to overuse nesting.

Initially only expert admins will be able to change `.gitignore` and
`.nogbundles` by editing the files directly an applying the changes with
`git-fso apply-stub`.  The process might be fragile.  See future work.

## Alternatives

We could require that repos must never be nested.  We could instead use
a naming convention that avoids nesting, for example use `project/overview` for
the overview repo instead of `project`.  Such a convention could avoid
confusion due to nesting.  But adding artifical directories only to avoid
nesting may cause a different kind of confusion.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

How to manage `.gitignore` and `.nogbundles` after initialization?  See NOE-19
for a minimal approach that allows users to switch a repo between
`ignore-most`, `bundle-subdirs` and `enter-subdirs`.

How to manage archiving responsibility, in particular if directories are
covered by multiple shadow repos?

How to handle toplevel special `git_*` stat fields in `ListStatTree()`?

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v2, 2018-09-28
* 2018-09-28: Fixed `ignore-most` `.gitignore`
* 2018-09-27: Added toplevel summary stat information
* 2018-09-27: Added `ignore-most` init policy
* v1, 2018-03-26
* 2018-03-26: Polishing after initial implementation
* 2018-03-15: Initial version
