# NOE-23 -- Git Filesystem Observer Repo Freeze
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2019-06-24

2019-10-28: NOE-13 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

## Summary

This NOE describes how Git FSO uses the immutable file attribute to protect
data.

Related NOEs:

* [NOE-2](./../noe-2/noe-2-filesystem-repos.md) -- Nog filesystem repos, BCP
  BioSupraMol filesystem
* [NOE-13](./../noe-13/noe-13-git-fso.md) -- Git Filesystem Observer
* [NOE-20](./../noe-20/noe-20-fso-backup.md) -- Git Filesystem Observer Backup
  and Archival
* [NOE-22](./../noe-22/noe-22-fso-udo.md) -- File System Observer User
  Privilege Separation

## Motivation

Using the immutable file attribute to protect data was first mentioned in
NOE-2, which describes the original BCPFS design.  NOE-13 describes a design to
observe a filesystem using Git without tracking the actual content, but NOE-13
does not discuss how to protect data from modification.  NOE-20 describes data
backup and archival, but NOE-20 leaves protecting data from modification as
future work.

This NOE describes how the immutable file attribute is used to freeze repos.

## Design

### Design overview

`git-fso` tracks the immutable file attribute of the repo toplevel directory on
branch `master-stat`.

`nogfsotard` forces a full tar if the latest `master-stat` has the immutable
attribute.  This ensures that there is a complete archive of the latest data,
which is a prerequisite for replacing the online data with a placeholder.

Admins and/or users can request to freeze or unfreeze a repo through the FSO
registry.  The operation starts a workflow.  `nogfsostad` observes the
workflow, executes `chattr` as root, and then updates the registry state.

See proof of concept in commits up to
`fuimages_nog_2018/nog@p/freeze-repo-2@5ac45e99d3889b1dda875eb1a25553bfb3dde4fb`
2019-05-27 "Tie p/freeze-repo-2: update NOE before merging to next".

### Immutable file attribute tracking

`git-fso` maintains a new field `attrs` in the toplevel `.nogtree`:

```
$ git show master-stat:.nogtree
name: "root"
...
attrs: "i"
```

with two possible states:

* `attrs: ""`: The toplevel directory does not have the immutable file
  attribute.
* `attrs: "i"`: The toplevel directory has the immutable attribute.

The format is chosen such that we could add further attributes in the future,
although we have no specific plans to do so.

If the current `master-stat` has the immutable attribute, `git-fso` will update
`master-stat` only if the realdir exists and does not have the immutable
attribute.  The realdir, thus, can be replaced by an immutable placeholder
directory, and `git-fso` will ignore it.

### Full archive for immutable repo

`nogfsotard` inspects `master-stat` before creating a Tartt archive and forces
a full archive if the immutable attribute is set.  The rule is based on the
assumption that repos are usually frozen only once, so that eagerly creating
a full archive does not increase the required tape storage size, because a full
archive would be created eventually anyway (see NOE-20).

When we add support for replacing the online storage with a placeholder (see
future work and NOE-24), we will use the state of `master-stat` and
`master-tartt` to determine that the full archive for a frozen repo is complete
and only then move the online data to a trash directory and schedule it for
deletion.

### Repo nesting

Repo nesting must be considered.  A leaf repo can be frozen without conflict.
But a non-leaf repo cannot be simply frozen if there is an unfrozen subrepo.
Naively freezing the non-leaf repo would set the immutable attribute also on
the realdir of the subrepo, which is wrong if the subrepo is not already
frozen.  Checking repo nesting requires a view of related repos, which is not
available on the individual repo level but only on the registry level.

Freeze and unfreeze are implemented as workflows that coordinate commands on
the registry and repo aggregates.  The first registry command verifies repo
nesting before allowing a freeze.

The initial implementations allows freeze only for leaf repos.  If a repo is
frozen, commands to initialize child repos are rejected by the registry.

Alternatives:

* Freezing of non-leaf repos could perhaps be handled by excluding subrepos
  when setting the immutable flag, in a similar way as nogfsotard excludes
  subrepos from tartt archives.

### Freezing and unfreezing

Freezing and unfreezing are implemented as workflows.  The following describes
the freeze-repo workflow.  The unfreeze-repo is similar.  See proof of concept
for details.

Some events have a suffix `_2` to distinguish them from events that were used
in an earlier, experimental implementation.

The workflow is initiated by gRPC `BeginFreezeRepo()`.  It starts the workflow
with `WorkflowEvent_EV_FSO_FREEZE_REPO_STARTED_2` on the workflow and a
corresponding `WorkflowEvent_EV_FSO_FREEZE_REPO_STARTED_2` on the ephemeral
registry workflow index.

Nogfsoregd observes the workflow.  It changes the repo state to freezing in the
registry with `RegistryEvent_EV_FSO_FREEZE_REPO_STARTED_2` and on the repo with
`RepoEvent_EV_FSO_FREEZE_REPO_STARTED_2`.  It then posts
`WorkflowEvent_EV_FSO_FREEZE_REPO_FILES_STARTED` on the workflow to notify
Nogfsostad.

Nogfsostad observes the workflow.  It changes the files to immutable, commits
the shadow repo, and posts `WorkflowEvent_EV_FSO_FREEZE_REPO_FILES_COMPLETED`
on the workflow.

Nogfsoregd then completes the workflow:
`RepoEvent_EV_FSO_FREEZE_REPO_COMPLETED_2` on the repo,
`RegistryEvent_EV_FSO_FREEZE_REPO_COMPLETED_2` on the registry,
`WorkflowEvent_EV_FSO_FREEZE_REPO_COMPLETED_2` on the workflow,
`WorkflowEvent_EV_FSO_FREEZE_REPO_COMPLETED_2` on the ephemeral registry
workflow index, and a final `WorkflowEvent_EV_FSO_FREEZE_REPO_COMMITTED` on the
workflow.

The final workflow event has no observable side effect.  Its only purpose is to
explicitly confirm termination of the workflow history.  The final event may be
missing if a multi-step command to complete the workflow was interrupted.

The workflow is eventually deleted from the index with
`WorkflowEvent_EV_FSO_FREEZE_REPO_DELETED` on the ephemeral registry workflow
index.  A workflow may be deleted with or without the final workflow event.

Some details (for freeze; details for unfreeze omitted); see full details in
proof of concept in commits up to
`fuimages_nog_2018/nog@p/freeze-repo-2@5ac45e99d3889b1dda875eb1a25553bfb3dde4fb`
2019-05-27 "Tie p/freeze-repo-2: update NOE before merging to next":

gRPC service:

```
service FreezeRepo {
    rpc BeginFreezeRepo(BeginFreezeRepoI) returns (BeginFreezeRepoO);
    rpc CommitFreezeRepo(CommitFreezeRepoI) returns (CommitFreezeRepoO);
    rpc AbortFreezeRepo(AbortFreezeRepoI) returns (AbortFreezeRepoO);
    rpc GetFreezeRepo(GetFreezeRepoI) returns (GetFreezeRepoO);

    rpc BeginFreezeRepoFiles(BeginFreezeRepoFilesI) returns (BeginFreezeRepoFilesO);
    rpc CommitFreezeRepoFiles(CommitFreezeRepoFilesI) returns (CommitFreezeRepoFilesO);
    rpc AbortFreezeRepoFiles(AbortFreezeRepoFilesI) returns (AbortFreezeRepoFilesO);
}

service RegistryFreezeRepo {
    rpc RegistryBeginFreezeRepo(RegistryBeginFreezeRepoI) returns (RegistryBeginFreezeRepoO);
    rpc RegistryCommitFreezeRepo(RegistryCommitFreezeRepoI) returns (RegistryCommitFreezeRepoO);
    rpc RegistryAbortFreezeRepo(RegistryAbortFreezeRepoI) returns (RegistryAbortFreezeRepoO);
}

service ReposFreezeRepo {
    rpc ReposBeginFreezeRepo(ReposBeginFreezeRepoI) returns (ReposBeginFreezeRepoO);
    rpc ReposCommitFreezeRepo(ReposCommitFreezeRepoI) returns (ReposCommitFreezeRepoO);
    rpc ReposAbortFreezeRepo(ReposAbortFreezeRepoI) returns (ReposAbortFreezeRepoO);
}
```

`nogfsoctl` usage:

```
nogfsoctl [options] repo <registry> (--vid=<vid>|--no-vid) <repoid> [--repo-vid=<vid>] freeze [--wait=<duration>] --workflow=<uuid> --author=<user>
nogfsoctl [options] repo <registry> (--vid=<vid>|--no-vid) <repoid> [--repo-vid=<vid>] begin-freeze --workflow=<uuid> --author=<user>
nogfsoctl [options] repo <registry> <repoid> get-freeze [--wait=<duration>] <workflowid>
```

`nogfsostasuod` shell code to set immutable file attributes:

```
cd "${realdir}"
find . \( -type f -o -type d \) -print0 | xargs -0 chattr +i --
```

Alternatives:

* Files could be changed to read-only before setting the immutable flag.  Files
  with read-only mode might be less confusing to users than files with
  read-write mode that cannot be modified due to the immutable attribute.  See
  `freeze()` and `unfreeze()` shell code proposed in NOE-2.

Future work:

* File permissions and ACLs could be checked and updated before setting the
  immutable attribute and verified after setting the immutable attribute in
  order to ensure that only expected permissions are stored in the full
  archive.  Immutable files with unexpected permissions or ACLs would also
  cause problems during `bcpfs-propagate-toplevel-acls`.

There was an earlier proof of concept that stored freeze and unfreeze
operations only on the repo.  See commits up to
`fuimages_nog_2018/nog@p/repo-freeze@a070709ccd4be0acd1c0b208bc12e7c80c82efa7`
2019-05-13 "Tie p/repo-freeze: polishing".

### Storage tier

The state of the repo storage is called storage tier, with the following
states:

```
Online
Frozen
Archived // Future
Freezing
FreezeFailed
Unfreezing
UnfreezeFailed
Archiving // Future
ArchiveFailed // Future
Unarchiving // Future
UnarchiveFailed // Future
```

The registry and repos aggregates in `nogfsoregd` restricts the allows state
transitions.  Examples:

* `Online -> Freezing -> Frozen`: successful freeze repo operation
* `Online -> Freezing -> FreezeFailed -> Freezing -> Frozen`: failed freeze
  repo operation, retried after an admin cleared the repo error
* `Frozen -> Unfreezing -> Online`: successful unfreeze repo operation
* `Frozen -> Unfreezing -> UnfreezeFailed -> Unfreezing -> Online`: failed
  unfreeze repo operation, retried after an admin cleared the repo error

See proof of concept for details.

## How we introduce this

We first deploy to production and test with AG Prohaska.

Potential next steps:

* Ask facility operators to tell us repos that should be immutable.  Then use
  `nogfsoctl` to freeze them.
* Develop a minimal UI before we offer freeze and unfreeze to facility
  operators.

## Limitations

Wrong file permissions or ACLs cannot be fixed after a repo has been frozen.
See design section for details.

## Alternatives

File permissions could be changed to read-only before setting the immutable
attribute.  See design section for details.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

This NOE described only the API and command line interface to freeze and
unfreeze.  A GUI will also be needed if we want to offer freeze and unfreeze as
a self service to facility operators.

ACLs would ideally be checked and updated before setting the immutable
attribute to avoid conflicts with `bcpfs-propagate-toplevel-acls`.  See design
section for details.

The design for replacing data with a placeholder in online storage and later
unarchiving it from tape is not discussed in this NOE, although tentative
storage tier codes are mentioned in the design section.  When unarchiving data,
owners that were saved in a tar archive might not be available in LDAP anymore.
Ideally, a future design would include automatic remapping of such UIDs.  At
least, the issue should be discussed.

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v1, 2019-06-24
* 2019-05-27: Handle repo nesting, using workflows, new proof of concept
* 2019-05-14: Future work mentions GUI
* 2019-05-13: Polishing
* 2019-05-09: Initial version
