# NOE-9 -- BCPFS access rules: project sharing (redacted)
By Vincent Dercksen, Ulrike Homberg, Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v2.0.1, 2019-11-01

Some personal information has been redacted from this version of the document.
The full version is
[noe-9-bcpfs-project-sharing](./../../../noe-sup/noes/noe-9/noe-9-bcpfs-project-sharing.md)
in the supplementary repo.  It can be made available upon request.

2019-10-28: NOE-9 contains ideas that are actively used on BCPFS.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-9 describes a design for sharing any directory tree on a filesystem for
services and organizational units as described in
[NOE-2](./../noe-2/noe-2-filesystem-repos.md) and
[NOE-10](./../noe-10/noe-10-posix-acls-smb-srv-ou.md).

## Motivation

The permission design described in NOE-2 and revised in NOE-10 enables sharing
of research data within research groups and image data between a facility and
research groups.

In practice, we have observed the need for more flexible sharing.  Examples:

* A facility wants to share information that is not directly related to
  a specific microscope with a number of organizational units.
* Two research groups want to create an ad hoc workspace for joint project
  work.

## Design

The design is based on an export list that can be used to share any directory
without moving its realpath location.  See section
['Alternatives'](#alternatives) for the two other main design alternatives that
we had considered.

The design consists of a general mechanism that supports nested exports.  It is
presented first.  The practical implementation, however, enforces some
restrictions to simplify sharing.  It is presented afterwards.

### General export lists

Any directory can be shared with any other group, read-only or read-write.
Each group maintains a list of exports.

Export entries are specified on their ou path, that is the path that a user
sees when browsing through the organizational unit share.  A consequence is
that a realpath may have multiple export entries; see details below.

Each group also maintains an import filter, at least conceptually.  An export
becomes only active if the import filter accepts it.  A practical workflow for
sharing may require multiple steps: We add a directory to our export list.
They receive a notification that we want to share a directory.  If they accept
the export, it will be added to their accept list, so that their import filter
accepts it, and the share becomes active.  If they decline the export, it will
be added to their reject list, and the share remains inactive.

Exported directories are accessed via symlinks in a `shared/` directory tree.
The shared tree contains only directories and symlinks.  It is managed by root.
Each organizational unit has a separate shared tree.  The shared tree includes
the exports of the owning organizational unit.  For nested exports, the
shortest path determines the symlink.  Example:

```
<us>/projects/foo-data                      # From us to them ro.
<us>/projects/foo-data/them-rw              # From us to them rw.
<us>/projects/foo-data/for-charly-ro        # From us to charly ro.
<us>/tem-101/2017/image-data                # From us to them.
<facility>/tem-101/<us>/2017/image-data     # From em to charly.
<them>/projects/bar                         # From them to us.

 # `...` indicates repeated `../../`.
<us>/shared/<us>/projects/foo-data          --> .../<us>/projects/foo-data
<us>/shared/<us>/tem-101/2017/image-data    --> .../<us>/tem-101/2017/image-data
<us>/shared/<them>/projects/bar             --> .../<them>/projects/bar

 # `foo-data/them-rw` is reachable through symlink `foo-data`.
<them>/shared/<us>/projects/foo-data          --> .../<us>/projects/foo-data
<them>/shared/<us>/tem-101/2017/image-data    --> .../<us>/tem-101/2017/image-data
<them>/shared/<them>/projects/bar             --> .../<them>/projects/bar

<charly>/shared/<us>/projects/foo-data/for-charly-ro        --> .../<us>/projects/foo-data/for-charly-ro
<charly>/shared/<facility>/tem-101/<us>/2017/image-data     --> .../<facility>/tem-101/<us>/2017/image-data

<facility>/shared/<facility>/tem-101/<us>/2017/image-data   --> .../<facility>/tem-101/<us>/2017/image-data
```

Exports are specified as ou paths to directories together with named group
ACLs.  Only other groups are listed.  Exports can be nested.  A deeper export
can only add permissions.  Example:

```
 # Valid nesting.
group:<them>:r-x <us>/projects/foo
group:<them>:rwx <us>/projects/foo/from-them

 # Invalid nesting, rwx from the parent propagates to the child.
group:<them>:rwx <us>/projects/foo
group:<them>:r-x <us>/projects/foo/read-only-for-them

 # Multiple exports of the same realpath.
group:<them>:r-x <us>/tem-101/2017/image-data
group:<charly>:r-x <em>/tem-101/<us>/2017/image-data
```

Exports that are rejected by import filters are ignored as much as possible.
Specifically, groups that rejected an export will not be visible in the
filesystem ACLs named groups.

Some care is necessary in the implementation to correctly handle facility
groups.  Facilities have an organizational unit group and an ops group with
special permission to write to all the facility device folders.  It seems
reasonable to assume that members in any of the two groups represent the
facility and may add exports.

### Applying general export lists

Sharing is applied to the filesystem in three steps:

* ACLs on the realpath trees
* Directory traversal permissions
* `shared/` symlink tree

#### ACLs on the realpath tree

Construct a list of realpath ACLs:

* Merge the export lists to build a global export list.
* Apply import filters, so that the global share list contains only directories
  and permissions that have been exported by one side and accepted by the other
  side.  Import filtering may remove paths entirely or only remove some named
  groups.
* Scan the share list and resolve realpaths.  Combine multiple shares for the
  same realpath into a single ACL.

Invariant: Each realpath has a single ACL that is permissive enough to allow
all shares.

Propagate realpath ACLs.  Setup:

* Sort the list by realpath name in depth-first, parent-first order.
* Initialize a state stack.
* Set the current state to a sentinel that is above all paths, like `/`.

Propagate realpath ACLs.  For each realpath entry, depth-first, parent-first:

* If the current state is not above the current entry, pop the stack into the
  current state until it is above the current entry.
* Invariant: The current entry path is below the current state.  The stack may
  be empty.
* Push the state, update the state, and save the state as the current entry
  ACL.  Updating the state means: Compute the union ACL of the current state
  and the current entry, so that child ACLs are more permissive than parent
  ACLs.

Invariant: Children ACLs are more permissive than parent ACLs.

Apply realpath ACLs:

* Sort the list by realpath in reverse depth-first, parent-first order, so that
  children are visited before their parents.
* For each entry: Apply the ACL to the full subtree, skipping subtrees roots
  that have been processed before.  Mark the subtree root as processed.

#### Directory traversal permissions

Simple traversal permissions assume that export paths contain at most one
standard symlink from an organizational unit to a microscope.  It is then
sufficient to set `group:<them>:--x` on the organizational unit directory to
allow traversal through the symlink to the microscope directory.

Organizational unit traversal: For each ou:

* Set `group:<them>:--x` on `<our>` directory for the union of named groups
  computed over all our share entries.

Realpath traversal: For each realpath:

* Set `group:<them>:--x` on parent directories of the realpath.
* The parent walk towards the root can be stopped at some level in order to
  apply a general scheme to restrict permissions.  Specifically `--x` is not
  set on `srv/<device>` but only on `srv/<device>/<ou>`, so that access to
  device subdirectories requires membership in the device group.

The simple algorithm needs to be extended if multiple symlinks are allowed.
`group:<them>:--x` needs to be set for all parent directories of all unresolved
and resolved symlinks on all levels.

#### Shared symlink tree

Setup:

* Merge the export lists to build the global export list.
* Apply import filters to build the global share list; see above.
* Sort the list by share path in depth-first, parent-first order.
* Initialize a state stack.
* Set the current state to a sentinel that is above all paths, like `/`.

Scan the list, for each share entry:

* If the current state is not above the current entry, pop the stack into the
  current state until it is above the current entry.
* Invariant: The current entry path is below the current state.  The stack may
  be empty.
* Push the state, update the state, and apply changes.  If the entry adds
  a named group, create directories in the receiving group's shared tree as
  needed and a symlink to the exported path.

The practical implementation will process each organizational unit
independently to ensure that default permissions and special cases like
facilities are properly handled.

### Simple export lists

Simple export lists are a variant of the general export lists with several
policies that restrict valid sharing configurations:

* Nesting of export paths and nesting of realpaths is forbidden.
* Symlinks are forbidden, except for the standard toplevel symlinks from the
  organizational unit directory to the device directories.
* Exported directories must use a certain naming convention.

The hope is that these policies simplify practical reasoning about exports, so
that they are manageable in practice.  For example, a shared path
`<us>/tem-101/2017/image-data/` implies that there can neither be a share to
`<us>/tem-101/2017/` nor to
`<us>/tem-101/2017/image-data/some/subfolder/`.  Forbidding realpath nesting
also implies that the facility can only export
`<facility>/tem-101/<us>/2017/image-data` but neither a parent directory nor
a sub-directory.

#### No nesting policy

Export paths must not be nested, neither the original export path nor the
realpath.  Technically it means that for all pairs of exports neither of the
two original paths is a prefix of the other, and the same holds for the
realpaths.

Nesting is forbidden on the original export list before import filtering.

A consequence of the realpath condition is that it is a global property that
cannot be verified within an organizational unit alone.  A check whether an
export can be added must consider all organizational units that may create
exports for the corresponding realpath tree.

The no nesting policy is verified before adding an export.  A trie-based
implementation should be reasonably efficient.  A separate trie can be used for
the original paths and one for the realpaths.  Checks are applied to both
the original path and the realpath:

 * If the path is a prefix to an inner trie node, it is rejected, because there
   is already an export of a sub-directory that would become nested by adding
   the export.
 * Peel the path level-by-level and search a prefix.  If a prefix to a leaf
   trie node is found before a prefix to an inner trie node, the path is
   rejected, because there is already a direct ancestor of the export, so the
   export itself would be nested.  If an inner trie node is found first, the
   peeling stops and the path is accepted.

By considering the symlink and sharing depth policies, it should be possible to
partition the problem, so that the checks need not use global tries for all
exports at once.

#### Limited symlinks policy

Symlinks are forbidden, except for the standard toplevel symlinks from the
organizational unit directory to the device directories.

The policy should be straightforward to verify before adding an export by
applying checks on the original path and the realpath.

#### Export naming policy

Directories can only be exported if they follow a naming convention.

This can, for example, be used to forbid sharing of toplevel directories.
Because sharing a directory blocks the sharing of sub-directories, one should
be carefully at which level to share.  Sharing at a too high level may block
more fine-grained sharing later.  It seems reasonable to enforce a certain
depth to avoid accidental problems.

It can also be a way to gradually introduce standardized naming conventions to
the filesystem.

The implementation will use a list of patterns to match export paths.  A path
must be explicitly allowed, or it will be denied by default.  The following
illustrates the possibilities using a mixed glob and regex pseudo-syntax:

* `deny  <ou>/people/*/2[0-9]{3}`: Do not export year folders directly.
* `allow <ou>/people/*/2[0-9]{3}/**`: Export year subfolders at any depth.
* `allow <ou>/people/*/**`: Export other subfolders at any depth.

* `deny  <ou>/projects/*/2[0-9]{3}`: Do not export year folders directly.
* `allow <ou>/projects/*/2[0-9]{3}/**`: Export year subfolders at any depth.
* `allow <ou>/projects/*/**`: Export other subfolders at any depth.

* `allow <ou>/tem-101/2017/<some-naming-convention>`: Allow only 2017
  directories that follow a certain naming convention.  Implicitly deny all
  others.

In addition to pattern whitelisting, a rule could be useful that enforces
a minimal path level that is required for sharing.  Such a rule may be useful
for optimizing no nesting checks, because it might allow partitioning the check
somehow.  Details would need to be clarified.

### Applying simple export lists

Based on the preconditions that exports can neither be nested on the original
paths nor on the realpaths and symlinks are forbidden, the implementation that
applies sharing to the filesystem can be simpler than for general export lists.

Setup:

* Merge the export lists to build a global export list.
* Apply import filters to build the global share list.  The global share list
  contains only directories and permissions that have been exported by one side
  and accepted by the other side.  Import filtering may remove paths entirely
  or only remove some named groups.
* Scan the share list and resolve realpaths to build a realpath list.  Combine
  multiple shares of the same realpath into a single ACL.

Apply ACLs on realpaths:

* For each realpath, compute the union of the share ACL with the default ACL
  for the realpath location and apply the union to the realpath and its
  subtree.
* For each realpath, gather `group:<them>:--x` traversal ACLs along sufficient
  levels of the parent path.  `--x` is set on `srv/<device>/<ou>` but not on
  `srv/<device>`, so that access always requires membership in the device
  group, independently of any exports.
* For each parent path that received some traversal permissions, compute the
  union of the traversal ACL with the default ACL for the realpath location and
  apply the union.

Apply traversal permissions to allow resolving microscopy symlinks:

* Set `group:<them>:--x` on the organizational unit `<us>` directory for the
  union of named groups computed for our shares.

Update the shared symlink tree for each ou:

* Build a list of active shares that the ou can access.
* Scan the current share tree in children-before-parent order.  Remove symlinks
  and directories that are not in the active shares.
* Scan the active shares in parent-before-children order.  Create directories
  and symlinks if missing.

### Practicalities

Exports specify permissions only for groups that cannot directly access
a directory due to its location.  Groups that can directly access
a directory are:

* The owning organizational unit
* The facility owning a device if it is a device directory, which can be the
  same as the organizational unit

The responsibility for ACL entries is partitioned:

* `bcpfs-perms` handles only ACL entries for groups that can directly access
  a directory.  It will be modified accordingly.
* The sharing implementation handles only ACL entries for groups that cannot
  directly access a directory.

Full ACLs are never set without inspecting existing ACLs.  With this strategy
the two concerns are only loosely coupled.  A separate check could verify that
permissions are neither too closed nor too open.

### Prototype implementation

An example prototype implementation of simple sharing is available in
`fuimages_nog-control_2017` run kit `bcpshare-x-imp706-prod-kindkrynn`.  It
uses a global configuration file and `sudo` to manage ACLs and the `shared/`
tree.  The authors can provide the implementation upon request.

## How we introduce this

We will initially implement ad hoc scripts that implement permissions for a few
specific scenarios.  We will careful review that ad hoc scripts do not violate
the design.

We then observe how sharing works in practice and reconsider later how to
proceed.

When we implement more sharing, we will try it first for fake resources and
sharing situation that involves our research group.

We probably should make sharing available more widely only when we have a web
UI that enables users to at least inspect the current sharing settings, better
also let them manipulate settings.  Without such a UI, users may get confused
or we may receive more change requests that we are willing to handle.

## Limitations

Simple export lists intentionally limit expressiveness compared to completely
flexible filesystem permissions.  This may confuse expert users.  But we
believe that such restrictions are key to keeping the overall system practical.

Traversal `--x` permission are likely to scatter throughout the filesystem,
which may sometimes be confusing.  But they do not pose a security risk if
lower directories are properly protected.

The responsibility separation as proposed in practicalities may be difficult to
reason about.  A single algorithm that would determine complete ACLs from
a single source of truth seems desirable.  But it would require more effort to
develop it initially compared to an incremental evolution of the system.

## Alternatives

### Specify sharing on realpath

Exports could be specified on a single canonical path, based on the Linux
realpath.  Microscopy directories would be named in device-ou order.

**SEE FULL VERSION FOR DETAILS.**

The Linux realpath would be technically simpler than export paths relative to
the organizational unit directory.  The realpath is easy to determine through
an API call, independently of our design.  Starting from the organizational
unit directory, however, seems easier to communicate to users.

### Additional Samba shares for exports

Additional Samba shares could be defined as needed to export deep directories.
ACLs would still need to be maintained on the directory subtree.  But directory
`--x` traversal could be avoided.

IMP has Samba installations with several 100 shares.

### Separate Samba share for sharing

Instead of adjusting the original permission design, we could keep it and
instead create a new Samba share with a different permission design.  It could
be some kind of general BioSupraMol or general project share that is usually
mounted by everybody.

### Design alternative group-shared-folder

All shared data is located below organizational unit directories with paths
like:

```
<us>/shared/<us>/our-data/our-real.data
<us>/shared/<they>/their-data --> ../../../<they>/shared/<they>/their-data
<they>/shared/<they>/their-data/their-real.data
```

As an organizational unit, our data is located in `shared/<us>/our-data`.  The
real data is there, not a symlink.  Data that others have shared with us is
mapped into our view as a symlink `shared/<they>/their-data`, which points to
their real data.

The view is symmetric.  The following paths work for us and them relative to
the organizational unit toplevel directory in the same way:

```
shared/<us>/our-data/our-real.data
shared/<they>/their-data/their-real.data
```

For us, their data is resolved through a symlink.  For them, our data is
resolved through a symlink.  Samba resolves symlinks on the server, so both
organizational unit SMB mounts appear to have the same shared data.

Data that is outside of `shared/` can never be shared.  In particular, data
below people, project, or microscopy directories cannot be shared with other
units.  Only our group can see it.  If sharing is desired, data can either be
copied temporarily to a shared folder, or it can be moved to a shared folder
and from then on be managed there.

Folders can be shared read-only or read-write.  A different decision can be
made for each group.

Compared to the owner-repo-like design, the group-shared-folder design is more
restricted.  Sharing is organized by group and limited to the `shared/` folder.
This restriction provides a certain clarity and should avoid accidental,
unintended data sharing.  A drawback is that data may need to be duplicated in
order to share it.

The repetition of `<us>` in `<us>/shared/<us>/our-data` is necessary to achieve
symmetry.  Relative paths `shared/<us>/our-data` that we see will work for them
when copy-pasted and transmitted, for example, via email.

The implementation of POSIX ACLs is straightforward.  The named group ACL
entries on `shared/<us>/our-data/` determine the other groups that can access
the directory tree and the access mode read-write or read-only.  The toplevel
ACL can be propagated to the full tree below.  The parent directories need
`group:<they>:--x` ACL entries for all groups that data is shared with.  The
list of groups can be determined using set operation on the named group ACL
entries of the directories `shared/<us>/*` to determine a list of all names
groups except us.

Sharing can be implemented as two separate basic operations:

 - we export, which sets our ACLs.
 - they import, which creates a symlink from their to our `shared/` folder.

We can revoke sharing by removing their named group from the ACL on
`our-data/`.  They would have a stale symlink, which can be cleaned up later.

Eventual consistency can be achieved by a global process that inspects all
`<x>/shared/<x>/*` directories, adds the necessary `group:<they>:--x` ACL
entries, maybe revokes unnecessary `group:<they>:--x` ACL entries, creates
symlinks, and removes stale symlinks.

Example: **SEE FULL VERSION FOR DETAILS.**

### Design alternative owner-repo-like

**SEE FULL VERSION FOR DETAILS.**

#### Proof-of-concept owner-repo-like

A Python script `setSharingPermissions` is available that takes a file defining
the shares as input and produces the symlinks as described above. In the current
implementation, directories are shared by a specific source user from a specific
source group with a target group.

**SEE FULL VERSION FOR DETAILS.**

## Future work

The questions in this section seem relevant but will not be answered in this
NOE.  They are left for future work.

The export list could perhaps be extended to a repo-like mechanism that enables
history tracking and viewing in a web-based interface like Nog.  The export
paths would become the repo roots.

The export list could perhaps be extended to a more general permission list.
The permission list would not only specify ACLs that grant access to other
groups but it would also become the source of truth for our permissions.  It
could, for example, specify that a directory is read-only after an image
acquisition has been completed.

## CHANGELOG

* v2.0.1, 2019-11-01: polishing
* 2019-10-28: frozen
* 2019-09-25: minor polishing
* v2, 2017-08-01
* 2017-08-01: Mention prototype implementation
* 2017-08-01: Added alternatives: Samba share for export, special Samba sharing
  share
* 2017-08-01: Clarify that facilities use two groups that need to be handled
* 2017-08-01: Stop realpath `--x` traversal at `srv/<device>/<ou>`
* v1, 2017-07-31
* 2017-07-29: Added practical responsibility separation between `bcpfs-perms`
  and sharing implementation
* 2017-07-28: Export list became the actual design
* 2017-07-27: Design alternative export list
* 2017-07-26: Design alternative group-shared-folder
* 2017-07-19: Proof-of-concept script for design alternative owner-like-repo
* 2017-05-18: Initial placeholder
