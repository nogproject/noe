# NOE-2 -- Nog filesystem repos, BCP BioSupraMol filesystem
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1.0.2, 2018-03-15

## Summary

We decided in fall 2016 to use a traditional internal filesystem for
BioSupraMol and adapt Nog accordingly.

The final design goal is to support Nog repos that use filesystem storage
instead of object storage.

This NOE only describes the design of the filesystem for BCP BioSupraMol and
some initial design considerations for Nog.  The full design has been postponed
and will be described in a separate NOE, since this NOE already became quite
large.

## BCP BioSupraMol filesystem concept

The Filesystem concept for BioSupraMol is described in the supplementary
document [biosupramol-filesystem-2016](./biosupramol-filesystem-2016.md).

## Design Considerations

Andreas Grünbacher, POSIX Access Control Lists on Linux,
<http://web.archive.org/web/20161102055912/www.vanemery.com/Linux/ACL/POSIX_ACL_on_Linux.html>.

Users may place files into the filesystem, and Nog will detect the new files
and automatically represent them in a Nog repo.

The name mapping between the filesystem and the Nog repos must be flexible
enough to support context-specific naming conventions.  Example: Department
A and department B use different naming conventions, which grew over time and
cannot be simply changed.  Both naming conventions should be managed using Nog.
Ideas:

* Mount-like mapping rule engine.  A map-mount point controls the mapping
  scheme for the subpath below.  Different map-mount points may use different
  mapping implementations.

How to control workflows with shared permissions?  Example: Lab A acquires
images for lab B.  Alice, a technician from lab A, must be able to write files.
Bob, a scientist from lab B, must be able to read the files.  When the
acquisition and initial data processing is finished, Alice might want to
transfer responsibility for the repo completely to Bob.  The files usage quota
should be accounted to lab B, although the files were initially created by
a member of lab A.  Ideas:

* Bind-mount repos: The same repo can be accessed through different Nog paths.
  `alice/images` and `bob/images` would represent the identical repo.  Maybe
  use an array `repo.owner = ['alice', 'bob']` to represent multiple paths.
* Organization repos: Alice and Bob access a repo `org/images` and handle
  shared access permissions.
