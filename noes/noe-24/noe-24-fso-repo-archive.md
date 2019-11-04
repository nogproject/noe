# NOE-24 -- Git Filesystem Observer Repo Archive
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2019-06-24

2019-10-28: NOE-13 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

## Summary

This NOE describes how Git FSO manages online storage for archived repos.

Related NOEs:

* [NOE-2](./../noe-2/noe-2-filesystem-repos.md) -- Nog filesystem repos, BCP
  BioSupraMol filesystem
* [NOE-13](./../noe-13/noe-13-git-fso.md) -- Git Filesystem Observer
* [NOE-20](./../noe-20/noe-20-fso-backup.md) -- Git Filesystem Observer Backup
  and Archival
* [NOE-22](./../noe-22/noe-22-fso-udo.md) -- File System Observer User
  Privilege Separation
* [NOE-23](./../noe-23/noe-23-fso-repo-freeze.md) -- Git Filesystem Observer
  Repo Freeze

## Motivation

It should be possible to remove archived data from online storage and keep it
only on tape in order to efficiently use storage resources.  NOE-23 describes
how a repo can be frozen and mentions managing online storage as future work.
This NOE describes describes a design for removing data from online storage and
restoring it on request.

## Design

See proof of concept in commits up to
`fuimages_nog_2018/nog@p/archive-repo@956d8e8b6ab122df7560739f244309cbf8969a73`
2019-06-18 "Tie p/archive-repo: unarchive-repo".

### Design overview

A frozen repo can be archived.  An archive-repo workflow replaces the realdir
with a placeholder that indicates that the repo has been archived.  The
workflow moves the original realdir to a trash directory and schedules it for
deletion.

An archived repo can be restored to its frozen state.  An unarchive-repo
workflow restores the data from a full Tartt archive, applies ACLs, and moves
the restored directory to the original realdir location.

The archive state is tracked in the shadow repo on branch `master-archive`.
The archive state is not tracked on `master-stat`, so that unarchive-repo
followed by archive-repo leaves `master-stat` unmodified and, therefore, does
not trigger a new Tartt archive.

Only leaf repos that are not at a root toplevel may be archived.

### Archive state

The archive state is represented in the FSO registry, in the shadow repo, and
in the realdir.

The realdir is replaced by a placeholder directory with a README file that
contains information about the archive, like total archive size, number of
archived files, and archive date.  The placeholder is owned by a service
account and has the usual ACLs and the immutable attribute, so that users can
read it but not modify it.

The placeholder directory is also committed to the shadow repo on branch
`master-archive`, so that the shadow repo can be used to display information
about the archive without accessing the realdir.

Archive-repo and unarchive-repo workflows leave `master-stat` and
`master-tartt` unmodified, so that unarchive-repo followed by archive-repo does
not trigger a new Tartt archive.

### ACLs

ACLs must be set when creating a repo placeholder and also when restoring data
from an archive.  The Tartt tar files do not contain ACLs.

Setting ACLs works like `bcpfs-propagate-toplevel-acls`.  The FSO roots must be
configured such that they point to the toplevel directories that contain the
reference ACLs.  The ACLs for a repo are derived from the ACLs of the root
directory.  The derived ACLs are then applied to all repo or repo placeholder
files.  `nogfsostad` reads the ACLs from the root, derives the expected ACLs,
and uses a privileged operation (udo root) to apply them.

### Repo nesting

A leaf repo can be archived without conflicting with other repos.  But
a non-leaf repo cannot be archived naively, because naively replacing its
realdir with a placeholder would also remove the realdirs of subrepos.

Archiving roots may also be problematic.  Other services may expect that root
directories do not change once they have been created, e.g. Samba or NFS may
export the root, or a concurrent bcpfs-perms may check the roots.

In the initial design, only leaf repos that are not at a root toplevel may be
archived.

Checking repo nesting requires a view of related repos, which is not available
on the individual repo level but only on the registry level.  Archive-repo and
unarchive-repo are, therefore, implemented as registry workflows.  Nesting
preconditions for archive are checked independently of preconditions for freeze
(see NOE-23), so that freeze could be allowed for non-leaf repos without
affecting the precondition checks for archive.

Future work: Archiving of non-leaf repos could be handled by keeping subrepo
realdirs when constructing a placeholder, in a similar way as nogfsotard
excludes subrepos from tartt archives.

### Nogfsorstd server

`tartt restore` is executed by a separate server Nogfsorstd.

Nogfsorstd uses a `tar` program with capabilities to restore files without
running as root.

Nogfsorstd limits concurrent `tartt restore` execution.  The initial design
allows only a single `tartt restore` at a time.

Nogfsorstd may run as a separate user and use group permissions to access the
tartt repo and tar files.

### Archive repo workflow

<!-- Copied from `archiverepowf/doc.go`. -->

Package `archiverepowf` implements the archive-repo ephemeral workflow.

#### Workflow Events

The workflow is initiated by gRPC `BeginArchiveRepo()`.  It starts the workflow
with `WorkflowEvent_EV_FSO_ARCHIVE_REPO_STARTED` on the workflow and a
corresponding `WorkflowEvent_EV_FSO_ARCHIVE_REPO_STARTED` on the ephemeral
registry workflow index.

Nogfsoregd observes the workflow.  It changes the repo state to archiving in
the registry with `RegistryEvent_EV_FSO_ARCHIVE_REPO_STARTED` and on the repo
with `RepoEvent_EV_FSO_ARCHIVE_REPO_STARTED`.  It then posts
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_FILES_STARTED` on the workflow to notify
Nogfsostad.

Nogfsostad observes the workflow.  It polls the shadow repo until the full
Tartt archive appears and then posts
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_TARTT_COMPLETED`.

Nogfsostad determines the temporary placeholder location and the garbage
location and saves them in `WorkflowEvent_EV_FSO_ARCHIVE_REPO_SWAP_STARTED` for
a possible restart.  It then prepares the placeholder, swaps it with the
realdir, moves the original data to the garbage location, runs `git-fso
archive` to update the shadow repo, and posts
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_FILES_COMPLETED`.  Errors may be handled by
retrying or by aborting the workflow.

Nogfsoregd then completes the main workflow work:
`RepoEvent_EV_FSO_ARCHIVE_REPO_COMPLETED` on the repo,
`RegistryEvent_EV_FSO_ARCHIVE_REPO_COMPLETED` on the registry, and
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_FILES_COMMITTED`.

Nogfsostad regularly checks whether the garbage has expired.  When it has
expired, Nogfsostad removes the garbage and posts
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_GC_COMPLETED`.

Nogfsoregd then completes the workflow:
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_COMPLETED` on the workflow,
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_COMPLETED` on the ephemeral registry
workflow index, and a final `WorkflowEvent_EV_FSO_ARCHIVE_REPO_COMMITTED` on
the workflow.

The final workflow event has no observable side effect.  Its only purpose is to
explicitly confirm termination of the workflow history.  The final event may be
missing if a multi-step command to complete the workflow was interrupted.

The workflow is eventually deleted from the index with
`WorkflowEvent_EV_FSO_ARCHIVE_REPO_DELETED` on the ephemeral registry workflow
index.  A workflow may be deleted with or without the final workflow event.

#### Possible State Paths

Successful archive: StateInitialized, StateFiles, StateTarttCompleted,
StateSwapStarted, StateFilesCompleted, StateFilesEnded, StateGcCompleted,
StateCompleted, StateTerminated.

Error during begin registry or begin repo: StateInitialized, StateFailed,
StateTerminated.

Error when archiving files: StateInitialized, StateFiles, maybe
StateTarttCompleted, maybe StateSwapStarted, StateFilesFailed, StateFilesEnded,
StateGcCompleted, StateFailed, StateTerminated.

### Unarchive repo workflow

<!-- Copied from `unarchiverepowf/doc.go`. -->

Package `unarchiverepowf` implements the unarchive-repo ephemeral workflow.

#### Workflow Events

The workflow is initiated by gRPC `BeginUnarchiveRepo()`.  It starts the
workflow with `WorkflowEvent_EV_FSO_UNARCHIVE_REPO_STARTED` on the workflow and
a corresponding `WorkflowEvent_EV_FSO_UNARCHIVE_REPO_STARTED` on the ephemeral
registry workflow index.

Nogfsoregd observes the workflow.  It changes the repo state to archiving in
the registry with `RegistryEvent_EV_FSO_UNARCHIVE_REPO_STARTED` and on the repo
with `RepoEvent_EV_FSO_UNARCHIVE_REPO_STARTED`.  It then posts
`WorkflowEvent_EV_FSO_UNARCHIVE_REPO_FILES_STARTED` on the workflow to notify
Nogfsostad.

Nogfsostad observes the workflow.  It creates the working directory and saves
it in `WorkflowEvent_EV_FSO_UNARCHIVE_REPO_TARTT_STARTED` to tell Nogfsorstd to
start `tartt restore`.

Nogfsorstd observes the workflow.  It restores the tartt archive to the working
directory and then posts `WorkflowEvent_EV_FSO_UNARCHIVE_REPO_TARTT_COMPLETED`
to notify Nogfsostad.  Errors may be handled by retrying or aborting the
workflow.

Nogfsostad applies ACLs, swaps the restored data with the realdir placeholder,
and posts `WorkflowEvent_EV_FSO_UNARCHIVE_REPO_FILES_COMPLETED`.  Errors may be
handled by retrying or by aborting the workflow.

Nogfsoregd then completes the main workflow work:
`RepoEvent_EV_FSO_UNARCHIVE_REPO_COMPLETED` on the repo,
`RegistryEvent_EV_FSO_UNARCHIVE_REPO_COMPLETED` on the registry, and
`WorkflowEvent_EV_FSO_UNARCHIVE_REPO_FILES_COMMITTED`.

Nogfsostad regularly checks whether the garbage has expired.  When it has
expired, Nogfsostad removes the garbage and posts
`WorkflowEvent_EV_FSO_UNARCHIVE_REPO_GC_COMPLETED`.

Nogfsoregd then completes the workflow:
`WorkflowEvent_EV_FSO_UNARCHIVE_REPO_COMPLETED` on the workflow,
`WorkflowEvent_EV_FSO_UNARCHIVE_REPO_COMPLETED` on the ephemeral registry
workflow index, and a final `WorkflowEvent_EV_FSO_UNARCHIVE_REPO_COMMITTED` on
the workflow.

The final workflow event has no observable side effect.  Its only purpose is to
explicitly confirm termination of the workflow history.  The final event may be
missing if a multi-step command to complete the workflow was interrupted.

The workflow is eventually deleted from the index with
`WorkflowEvent_EV_FSO_UNARCHIVE_REPO_DELETED` on the ephemeral registry
workflow index.  A workflow may be deleted with or without the final workflow
event.

#### Possible State Paths

Successful unarchive: StateInitialized, StateFiles, StateTartt,
StateTarttCompleted, StateFilesCompleted, StateFilesEnded, StateGcCompleted,
StateCompleted, StateTerminated.

Error during begin registry or begin repo: StateInitialized, StateFailed,
StateTerminated.

Error during tartt restore: StateInitialized, StateFiles, StateTartt,
StateTarttFailed, StateFilesEnded, StateGcCompleted, StateFailed,
StateTerminated.

Error while moving swapping restored files with the realdir placeholder:
StateInitialized, StateFiles, StateTartt, StateTarttCompleted,
StateFilesFailed, StateFilesEnded, StateGcCompleted, StateFailed,
StateTerminated.

### Container deployment

When deploying services in containers, Nogfsostad does not directly modify
realdirs but only through Nogfsostaudod.  The working directory must be on the
same filesystem as the realdirs, so that `rename()` can be used to swap the
realdir.

Specifically, Nogfsostad uses the following bind-mounts:

```
.../data/ou:ro
.../data/srv:ro
.../data/.spool:rw
```

and Nogfsostaudod:

```
.../data:rw
```

## How we introduce this

We first deploy to production and test with AG Prohaska.

Potential next steps:

* Ask facility operators to tell us repos that should be archived.  Then use
  `nogfsoctl` to archive them.
* Develop a minimal UI before we offer archive and unarchive to facility
  operators.

## Limitations

The initial design supports unarchive-repo only with plaintext Tartt secrets.

Files will be restored with the original UID, which may no longer belong to
a named account.

## Alternatives

Not discussed.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

How to handle unknown accounts during restore.  The restored files would
ideally be reassigned in the same way as online files were reassigned when an
account was deleted.

How to support GPG-encrypted tartt secrets?  A simple solution would be to
encrypt the secrets also to a service GPG key whose secret key is available
when running `tartt restore`.

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v1, 2019-06-24
* 2019-06-21: Section on container deployment
* 2019-06-21: GPG-encrypted secrets are not yet supported
* 2019-06-18: First complete draft
* 2019-05-16: Initial, incomplete draft
