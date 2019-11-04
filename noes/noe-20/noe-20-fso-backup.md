# NOE-20 -- Git Filesystem Observer Backup and Archival
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v2.0.1, 2019-11-01

2019-10-28: NOE-13 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-20 describes the backup strategy for filesystems that are tracked using Git
filesystem observer as described in NOE-13 and NOE-18.

Related NOEs:

* [NOE-13](./../noe-13/noe-13-git-fso.md) -- Git Filesystem Observer
* [NOE-18](./../noe-18/noe-18-fso-nesting.md) -- Git Filesystem Observer
  Nesting

## Motivation

Git filesystem observer shadow repositories track summary information about
a filesystem together with related metadata.  Repositories may be nested as
described in NOE-18.  The backup and archival system should use the repository
layout for efficient backups and to expose meaningful information about the
backup and archival state to users.

## Design

File data and metadata are handled separately.  File data is archived using
incremental tar.  Metadata is archived as full tars of the shadow repo,
assuming metadata is relatively small.  Metadata is redundantly included in
full file data archives.  See sections below for details.

### Eventual state

Control loops drive the system towards its eventual state, relying on mtime as
an indicator of change.  Assuming no new changes happen on the filesystem, the
eventual state is:

* The shadow `master-stat` has recorded the latest mtime range of all files
  below the repo toplevel.  This ensures that there will be a new commit on
  `master-stat` for every change on the filesystem, assuming changes update the
  mtime to the correct wall clock time.
* There is a file data backup for the latest `master-stat`.  This ensures that
  the latest filesystem state has propagated to the backup.
* There is a metadata backup for the latest shadow Git repo.  This ensures that
  essential data can be safely stored in the shadow Git repo.
* File data full backups include a dump of the latest metadata and the latest
  shadow repo, except for refs that are used to track the backup state, which
  cannot be part of the file data backup to avoid infinite recursion, where
  updating the backup state would trigger the next backup.  This ensures that
  a full file data backup includes an essentially complete copy of the
  metadata.
* If `master-stat` has not changed for a configurable period, for example one
  month, the backup for the latest `master-stat` is a full backup that contains
  metadata for the latest refs, except for explicitly excluded refs as
  discussed in the previous item.  This ensures that eventually there will be
  a single full backup of the file data together with the latest essential
  metadata.
* There is a separate backup of the latest archive secrets.  Retired secrets
  have been deleted.

The following control loops drive the system:

* Watch the broadcast events for any shadow ref updates.  Updates trigger
  a shadow backup.  A regular scan checks all repos to trigger backups, as
  a fallback for updates that were not broadcast: events may get lost during
  a server restart, or updates may be never broadcast by design, such as
  a direct git push into the shadow repo in a low-level script.
* Watch the broadcast events for `master-stat` updates.  Updates trigger a file
  data backup.  A regular scan checks all repos to trigger backups as
  a fallback.  The regular scan is also used for time-based triggers, such as
  creating a full backup if `master-stat` has not changed for a configurable
  period and the latest backup is not yet a full backup with the latest
  metadata.
* A regular scan checks mtime ranges.  If the mtime range has changed, it adds
  a `master-stat` commit that updates the toplevel `.nogtree`.  The scan could
  include a safety check to protect against clock skew: mtimes must never be
  newer than the current time.
* A regular scan performs time-based garbage collection in backup repos.  The
  latest backup will always be kept.
* A regular scan creates a backup of the latest archive secrets and deletes
  expired archive secrets backups.

### Shadow full tar

The algorithm for shadow backups is:

* On every backup trigger, compare a hash of `git for-each-ref` with the hash
  of the latest backup.  If the hashes differ, create a full tar of the shadow
  repo, else stop.
* Then create hard links in monthly, weekly, daily, hourly, and latest buckets.
  Create a hard link only if the latest backup in a bucket is older than
  a configurable per-bucket period.  Always create a hard link in latest.
* Remove expired backups.

The shadow backup location is encoded as a `nogfsobak://` URL and stored in the
registry.  Example:

* `nogfsobak://files.example.com/nogfso/backup/shadow/1530091728/ag-charly/94/86/94866807-c39c-4a30-8b83-5b511bc27fdd`

Proof of concept:

* `nogfsosdwbakd3` in
  `fuimages_nog_2016/next@55aeaa19e5954f62e09fb4a2d77aeb87d31c896e` 2018-06-27
  'Tie next: p/bcpfs-like-fso-dev'

Earlier proof of concept versions:

* `nogfsosdwbak2d` (for Nog FSO shadow backup daemon, version 2) in
  `fuimages_stdcloud_2017/master@e0bcb74cf66d7001c0a758e0bdd6bd3208ee258a`
  2018-04-19 'fso2 kit: nogfsoreabakd ignores subrepos'

Example disk layout:

```
78/e0/78e0176b-4191-4048-b637-d9befbaeaade
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/origin.gitrefstate
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/latest
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/latest/78e0176b-4191-4048-b637-d9befbaeaade_20180419T074659Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/latest/78e0176b-4191-4048-b637-d9befbaeaade_20180419T103634Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/hourly
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/hourly/78e0176b-4191-4048-b637-d9befbaeaade_20180416T091105Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/hourly/78e0176b-4191-4048-b637-d9befbaeaade_20180417T161949Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/hourly/78e0176b-4191-4048-b637-d9befbaeaade_20180418T111711Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/hourly/78e0176b-4191-4048-b637-d9befbaeaade_20180418T152255Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/hourly/78e0176b-4191-4048-b637-d9befbaeaade_20180419T074659Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/hourly/78e0176b-4191-4048-b637-d9befbaeaade_20180419T103634Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/daily
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/daily/78e0176b-4191-4048-b637-d9befbaeaade_20180416T091105Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/daily/78e0176b-4191-4048-b637-d9befbaeaade_20180417T161949Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/daily/78e0176b-4191-4048-b637-d9befbaeaade_20180419T074659Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/weekly
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/weekly/78e0176b-4191-4048-b637-d9befbaeaade_20180416T091105Z.tar.gpg
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/monthly
78/e0/78e0176b-4191-4048-b637-d9befbaeaade/monthly/78e0176b-4191-4048-b637-d9befbaeaade_20180416T091105Z.tar.gpg
```

### File data incremental tar

The algorithm for incremental archives of the real file data is:

* On every backup trigger, compare the `master-stat` commit with the commit
  that was observed before the latest backup.  If the hashes differ, trigger
  a Tartt archive, else stop
* As a special case, check if the latest backup is incremental; if so, check if
  `master-stat` has not changed for more than a configurable period; if so,
  force a full backup.
* See details below.

Tartt organizes GNU tar incremental dumps into a tree with several frequency
levels.  Each incremental tar stream is split into chunks that are compressed
and encrypted in parallel and recombined into an intermediate tar stream that
is then split into large pieces and saved to permanent storage together with
a manifest file with checksums.  The manifest can be GPG-signed later.  Each
tar stream is symmetrically encrypted with a different random secret.  The
random secret is GPG-encrypted and stored into a separate file, so that the
secret can be re-encrypted later without re-encrypting the entire tar stream.
The tar logs are stored as `metadata.tar`, which is encrypted with the same
random secret as the file data tar stream.

A full archive contains the following additional information:

* A README that describes the storage format
* Tar archives for the latest Git tree master-meta and master-sha in
  `metadata.tar`
* A full tar of the shadow Git repo in `metadata.tar`

Tartt supports a storage driver `localtape` to write the data tar archives into
a separate directory tree.  It will be used to store the essential archive data
directly to a hierachical tape filesystem that is mounted via NFS.

The Tartt repo location is encode as a `tartt://` URL, including the essential
driver details, and stored in the registry.  Examples:

* Tar data stored in repo:
  `tartt://files.example.com/nogfso/archive/tartt/1530091728/ag-charly/94/86/94866807-c39c-4a30-8b83-5b511bc27fdd.tartt?driver=local`
* Tar data stored in a separate directory:
  `tartt://files.example.com/nogfso/archive/tartt/1530091728/ag-charly/94/86/94866807-c39c-4a30-8b83-5b511bc27fdd.tartt?driver=localtape&tardir=/nogfso/tape/tartt/1530091728/ag-charly/94/86/94866807-c39c-4a30-8b83-5b511bc27fdd.tars`

Proof of concept:

* `tartt` (for tar time tree), `tartt-store`, `nogfsotard`, `nogfsotargctd` in
  `fuimages_nog_2016/next@55aeaa19e5954f62e09fb4a2d77aeb87d31c896e` 2018-06-27
  'Tie next: p/bcpfs-like-fso-dev'

Earlier proof of concept versions:

* `tartt` (for tar time tree) and `tartt-store` in
  `fuimages_nog_2016/next@46427fd2965219c98bbaabcadc5265135a8246fd` 2018-04-23
  'Tie next: p/tartt'
* `nogfsoreabakd` (for Nog FSO realdir backup daemon) in
  `fuimages_stdcloud_2017/master@35e39dd6f4e325fa0600f40868dbbabfd927bd0d`
  2018-04-30 'fso2 kit: nogfsoreabakd checks that latest full archive includes
  latest shadow'

Example backup tree:

```
+-- active or frozen
| +-- number of children
| | level  min time             max time                store timestamp path
a 1   mo1  2018-04-22T05:57:35Z 2018-04-23T12:41:21Z    localhost
a 1  full  2018-04-22T05:57:35Z 2018-04-23T12:41:21Z    localhost/20180422T055735Z
a 1    d1  2018-04-23T09:06:47Z 2018-04-23T12:41:21Z    localhost/20180422T055735Z/d1
a 1 patch  2018-04-23T09:06:47Z 2018-04-23T12:41:21Z    localhost/20180422T055735Z/d1/20180423T090647Z
a 1    h1  2018-04-23T12:37:37Z 2018-04-23T12:41:21Z    localhost/20180422T055735Z/d1/20180423T090647Z/h1
a 1 patch  2018-04-23T12:37:37Z 2018-04-23T12:41:21Z    localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z
a 3    s0  2018-04-23T12:38:00Z 2018-04-23T12:41:21Z    localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0
f 0 patch  2018-04-23T12:38:00Z 2018-04-23T12:38:00Z    localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z
f 0 patch  2018-04-23T12:41:12Z 2018-04-23T12:41:12Z    localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z
a 0 patch  2018-04-23T12:41:21Z 2018-04-23T12:41:21Z    localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z
```

Example filesystem layout:

```
./.git/...
./tarttconfig.yml
./origin.gitrefstate
./.gitignore
./stores
./stores/localhost
./stores/localhost/20180422T055735Z
./stores/localhost/20180422T055735Z/full
./stores/localhost/20180422T055735Z/full/secret.asc
./stores/localhost/20180422T055735Z/full/origin.snar
./stores/localhost/20180422T055735Z/full/info.log
./stores/localhost/20180422T055735Z/full/out.log
./stores/localhost/20180422T055735Z/full/manifest.shasums
./stores/localhost/20180422T055735Z/full/data.tar.zst.gpg.tar.000
./stores/localhost/20180422T055735Z/full/metadata.log
./stores/localhost/20180422T055735Z/full/metadata.tar.gpg
./stores/localhost/20180422T055735Z/full/README.md
./stores/localhost/20180422T055735Z/d1
./stores/localhost/20180422T055735Z/d1/20180423T090647Z
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch/origin.snar
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch/secret.asc
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch/out.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch/manifest.shasums
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch/data.tar.zst.gpg.tar.000
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch/metadata.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/patch/metadata.tar.gpg
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch/origin.snar
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch/secret.asc
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch/out.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch/manifest.shasums
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch/data.tar.zst.gpg.tar.000
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch/metadata.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/patch/metadata.tar.gpg
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z/patch
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z/patch/secret.asc
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z/patch/out.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z/patch/manifest.shasums
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z/patch/data.tar.zst.gpg.tar.000
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z/patch/metadata.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T123800Z/patch/metadata.tar.gpg
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z/patch
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z/patch/secret.asc
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z/patch/out.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z/patch/manifest.shasums
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z/patch/data.tar.zst.gpg.tar.000
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z/patch/metadata.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124112Z/patch/metadata.tar.gpg
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch/origin.snar
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch/secret.asc
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch/out.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch/manifest.shasums
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch/data.tar.zst.gpg.tar.000
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch/metadata.log
./stores/localhost/20180422T055735Z/d1/20180423T090647Z/h1/20180423T123737Z/s0/20180423T124121Z/patch/metadata.tar.gpg
```

Example level configuration:

```yaml
stores:
  - name: "vsl4"
    driver: local
    levels:
      - { interval: "1 month", lifetime: "10 months" }
      - { interval: "5 days", lifetime: "40 days" }
      - { interval: "1 day", lifetime: "8 days" }
      - { interval: "1 hour", lifetime: "50 hours" }
      - { interval: "0", lifetime: "120 minutes" }
```

Example for a separate directory hierarchy with driver `localtape`:

```
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/s0
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/s0/20180627T093021Z
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/s0/20180627T093021Z/patch
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/s0/20180627T093021Z/patch/data.tar.zst.gpg.tar.000
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/s0/20180627T093021Z/patch/metadata.tar.gpg
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/s0/20180627T093021Z/patch/manifest.shasums
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/full
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/full/data.tar.zst.gpg.tar.000
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/full/README.md
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/full/metadata.tar.gpg
.../94866807-c39c-4a30-8b83-5b511bc27fdd.tars/20180627T092849Z/full/manifest.shasums
```

### Avoiding spurious `ctime` modifications

GNU tar checks `mtime` and `ctime` to determine whether to include a file in an
incremental tar.  Using `ctime` seems to be crucial to ensure that a renamed
file is correctly included in an incremental tar.  See GNU tar mailing list
thread [#BUGTARCTIME](#BUGTARCTIME).

Ideally, `ctime` would not be unnecessarily modified to achieve efficient
incremental backups.  Specifically the following should be avoided:

`chmod`, `chown`, and `chgrp` modify `ctime` even if the file has already the
desired attributes.  To avoid such changes, use `find` instead to identify
files whose attributes need to be changed.

NetBackup seems to change `ctime` by default in order to reset `atime`, see
[#NETBACKUP](#NETBACKUP).  The option `DO_NOT_RESET_FILE_ACCESS_TIME` should be
used instead.

But avoiding ctime changes can be difficult in practice, in particular if NFS
access to the file system is provided.  Users can modify permissions, which are
later fixed by a background script.  Files would be included in an incremental
backup again, although their content is unmodified.

We, therefore, use a patched GNU tar with the option
`--listed-incremental-mtime` to use only mtime when detecting file
modifications, ignoring ctime, see [#GNUTARPATCH](#GNUTARPATCH).  The advantage
over `--listed-incremental` is that file attribute changes do not cause files
to be included in the next incremental archive; only file modifications that
update mtime trigger an incremental backup, which is consistent with the
handling of mtime ranges in shadow repos.  A potential disadvantage is that
files may unexpectedly be missing in incremental archives.  Full archives,
however, are reliable; and the eventual state for every repo is a full archive.
The trade off seems acceptable in order to save archive space.

References:

* <a name="BUGTARCTIME"></a>#BUGTARCTIME:
  bug-tar mailing list thread that follows message
  "[Bug-tar] Skipped files with --listed-incremental after rename",
  <http://lists.gnu.org/archive/html/bug-tar/2003-10/msg00013.html>
* <a name="NETBACKUP"></a>#NETBACKUP:
  Veritas NetBackup™ Administrator's Guide, Volume I,
  How NetBackup determines when UNIX files are due for backup,
  <https://www.veritas.com/support/en_US/doc/18716246-126559472-0/v41780104-126559472>
* <a name="GNUTARPATCH"></a>#GNUTARPATCH:
  `gnu-tar@p/listed-incremental-mtime@cc7208d21aa956db69b5d2085de12acd12695c01`
  2018-11-23 "Tie p/listed-incremental-mtime: added test", available at
  <https://github.com/sprohaska/gnu-tar>, also on branch `next`

### Default handling of `atime`

Trying to preserve `atime` on the backup application level seems unnecessary.
Performance should not be a concern on a modern Linux, which uses the mount
option `relatime` by default since Linux 2.6.30, released in 2009, see
StackExchange question and answers [#SERELATIME](#SERELATIME).

If we wanted to preserve `atime` nonetheless, for example to completely hide
from users the fact that tar read a file, we could add an option to run GNU tar
with `--atime-preserve=system` for scenarios where tar is guaranteed to have
the capability `CAP_FOWNER`, which is required to use the flag `O_NOATIME`
during `open()`, see [#MANOPEN](#MANOPEN).

References:

* <a name="SERELATIME"></a>#SERELATIME:
  StackExchange question and answers: When was `relatime` made the default?,
  <https://unix.stackexchange.com/q/17844>
* <a name="MANOPEN"></a>#MANOPEN:
  Man page `open(2)`,
  <http://man7.org/linux/man-pages/man2/open.2.html>

### Storing tartt and backup locations in the registry

The location of the tartt repo and the shadow backup repo are stored in the
registry, so that the naming convention can be changed for new repos without
renaming existing tartt and shadow backup repos.

There are two new `fsorepos` events:

* `EV_FSO_TARTT_REPO_CREATED` aka `EvTarttRepoCreated` confirms that a tartt
  repo has been created.  It contains the tartt location as a `tartt://` URL.
* `EV_FSO_SHADOW_BACKUP_REPO_CREATED` aka `EvShadowBackupRepoCreated` confirms
  that a backup directory has been created.  It contains the location as
  a `nogfsobak://` URL.

The names are chosen such that corresponding `fsoregistry` events could be
added in the future that would indicate the start of the repo creation process.
Example:

* Future registry event `tartt repo init started` would reserve the URL.
* Repos event `EV_FSO_TARTT_REPO_CREATED` would then indicate completion of the
  inititialization process.

The relation of the two event is similar to:

  - registry event `EV_FSO_REPO_ENABLE_GITLAB_ACCEPTED`
  - repos event `EV_FSO_GIT_REPO_CREATED`

Proof of concept:

* `nogfsotard` in
  `fuimages_nog_2016/next@55aeaa19e5954f62e09fb4a2d77aeb87d31c896e` 2018-06-27
  'Tie next: p/bcpfs-like-fso-dev'

### File data tar with nested repos

File data that is tracked in a subrepo is excluded from the backup of the
superrepo using an anchored GNU tar exclude list that is compiled from the FSO
registry repo listing before starting the backup.  The exclude list preserves
subrepo directory stubs but ignores the directory content.

Example exclude list:

```
./2011/*
./2017/*
./besprechung/*
```

Proof of concept:

* `nogfsotard` in
  `fuimages_nog_2016/next@55aeaa19e5954f62e09fb4a2d77aeb87d31c896e` 2018-06-27
  'Tie next: p/bcpfs-like-fso-dev'

Earlier proof of concept versions:

* `nogfsoreabakd` (for Nog FSO realdir backup daemon) in
  `fuimages_stdcloud_2017/master@013894d069d2a794d8fb8b94e1f26be72e8efd9e`
  2018-04-23 'fso2 kit: nogfsoreabakd locks the tartt repo for git add and
  commit'

### Archive encryption

Tar archives are symmetrically encrypted with a secret that is GPG-encrypted
separately.  Key rotation requires only re-encrypting the secret.  The tar
archives may be large, so that separate secrets seem necessary to support
efficient key rotation.

The approach would also allow storing the secrets in a separate secret
management system, like Vault, which could be useful for auditing.

### Tracking Tartt state

The essential content of Tartt repositories is tracked in Git and pushed to
the branch `master-tartt` in the shadow repo, where it will be included in the
backup and can be used in the GUI.

The following Tartt repo content is ignored:

* logs: They are also packed as metadata.
* tar incremental `.snar` state: It is tied to the local filesystem state.  It
  would be lost in a major disaster that looses the local filesystem with the
  local Tartt repos.  The backup process would be restarted with a full
  archive, which would create a fresh `.snar` state.  The device identity would
  probably be lost anyway in a disaster that looses local filesystem.
* tar data: Tar files are big.  They are copied to cold storage.  Only the
  manifest is stored in Git.
* secrets: They are stored separately for security.  Secrets should be
  regularly re-encrypted and the previous versions forgotten, which conflicts
  with storing them permanently in the Git history.

`.gitignore`:

```
*.log
*.snar
*.tar
*.tar.*
secret.asc
```

### Full disaster recovery

A full disaster recovery requires the following steps:

* Restore shadow repo from backup.
* Restore the essential Tartt repo by cloning the shadow branch `master-tartt`.
* Restore the Tartt archive secrets from the separate secret backup.
* Fetch the Tartt tar archives from cold storage.
* `tartt restore` the file data.

### Data archival

The latest Tartt archive is never deleted.  Full Tartt archives may be marked
as protected to exclude them from garbage collection.

Future work: A mechanism to switch the files of a repository to immutable,
create a latest full archive, and remove the data from the online filesystem,
keeping only a placeholder.

### Checking mtime ranges

In order to create a new `master-stat` commit on changes anywhere in
a directory tree without modifying the mtime of the toplevel directory, the
toplevel `.nogtree` records information about the mtime range of the whole
directory tree below.  The mtime range is represented as integer Unix times
`mtime_min` and `mtime_max`, which are computed from the floor() of higher
precision mtimes, because this is what `rsync -t` does when syncing from an OS
with sub-second precision, such as Linux, to an OS with integer-second
precision, such as macOS.  The mtime range can be computed for a directory tree
as follows:

```
find . -printf '%T@\n' | gawk '
BEGIN { PREC="double"; min = 1e100; max = -1e100; }
{ min = (min < $1) ? min : int($1); max = (max > $1) ? max : int($1); }
END { printf("%d %d\n", min, max); }
'
```

`git-fso stat` does not always check the mtime range, because it is
a relatively expensive operation.  It checks the mtime range if the toplevel
`.nogtree` is updated for other reasons.  A check can also be forced with
`git-fso stat --mtime-range` or `git-fso stat --mtime-range-only`, which tells
`git-fso` to update only the toplevel `.nogtree`.  `--mtime-range-only` is
exposed in gRPC `Stat()` in order to allow system daemons to regularly force
rechecks of mtime ranges.

Regular mtime range checks are performed as a background task in `nogfsostad`.
Other system daemons need not trigger rechecks.  This approach is not yet
implemented in the proof of concept.

We decided against restricting mtime ranges to regular files and symlinks,
which might have been helpful to better support syncing of directory trees
without syncing directory mtimes, for example with `rsync -O`, which can be
useful in a group-shared directory tree, because mtime cannot be modified
arbitrarily if a directory is owned by a different user, even if the current
user has write permission to the directory.  But `git-fso` already tracks
directory mtimes in `.nogtree` entries, so it would be inconsistent to exclude
them when computing the mtime range.

### Naming principles update

Principles:

* Unix users may be limited <= 8 chars.  User names should be short.  We use
  Unix user and group names `ngf...`.
* Program names may be longer.  We use `nogfso...`.
* Supporting programs should have the name of the main program as a prefix;
  example `nogfsotard` + `nogfsotargctd`.
* We use the term "archive" instead of "backup" to describe tartt of file data.
  The purpose is a combined archival and backup service.  Archive is the more
  general term.
* Use a digit after the `d` to distinguish a backwards incompatible newer
  version of a program.  Example: `nogfsosdwbakd3`.

Names:

* `nogfsostad`: as before
* `nogfsotard`: tartt of file data
* `nogfsotargctd`: regular garbage collection of tartt repos
* `nogfsotarsecbakd`: backup of tartt secrets
* `nogfsosdwbakd3`: backup of shadow repos
* `nogfsosdwgctd`: regular garbage collection of shadow repos; should be
  integrated as a background task in `nogfsostad`
* `nogfsotchd3`: regular mtime check; should be integrated as a background
  task in `nogfsostad`

Proof of concept:

* `fuimages_nog_2016/next@55aeaa19e5954f62e09fb4a2d77aeb87d31c896e` 2018-06-27
  'Tie next: p/bcpfs-like-fso-dev'

### Deploying to a BCPFS-like filesystem

We use the following users and permission when deploying the daemons to
a BCPFS-like filesystem:

`nogfsostad`: partition the reposibility to mutiple daemons that each are
reponsible for a subset of organization units.  Users `ngfsta<serial>` with
primary group `ngfsta` and secondary groups to access the files of the
organizational units.  The partitioning uses global path prefixes.

`nogfsosdwgctd`: Same partitioning as `nogfsostad`.  `git gc` will be
integrated as a background task in `nogfsostad` to avoid running a separate
daemon.

`nogfsotchd3`: A separate nobody-like user `ngftch` with group `ngftch`.  The
mtime range checks will be integrated as a background task in `nogfsostad` to
avoid running a separate daemon.

`nogfsotard`: a single daemon with user `ngftar` and group `ngftar`.  It uses
`CAP_DAC_READ_SEARCH` on special binaries `tar` and `git` to read file data and
Git refs from shadow repos.  It uses `git-remote-ext` with sudo to push to
shadow repos.

`nogfsotargctd`: user `ngftar` and group `ngftar`.  Access to the tartt repos
is enough.

`nogfsotarsecbakd:`: user `ngftar` and group `ngftar`.  Access to the tartt
repos is enough.

`nogfsosdwbakd3`: a single daemon with user `ngfbak` and group `ngfbak`.  It
uses `CAP_DAC_READ_SEARCH` on special binaries `tar` and `git` to read from the
shadow repos.

Example groups:

```
// user primary-group secondary-groups...
ngfsta2 ngfsta org_ag-alice org_ag-bob srv_rem-707 srv_tem-505
ngfsta3 ngfsta org_ag-charly srv_rem-707 srv_tem-505
ngftar ngftar
ngfbak ngfbak
ngftch ngftch
```

Proof of concept:

* `fuimages_nog_2016/next@55aeaa19e5954f62e09fb4a2d77aeb87d31c896e` 2018-06-27
  'Tie next: p/bcpfs-like-fso-dev'

### ISO 8601

An earlier version of this NOE used a combination of ISO 8601 extend date
format and ISO 8601 basic time format, for example `2018-04-19T074659Z`.  ISO
8601 does not allow such a combination.  Date and time must either both use the
basic format or both use the extended format.  The NOE now uses the ISO 8601
basic format for filesystem paths, for example `20180419T074659Z`.  The basic
format has been chosen to avoid colons in file names.

## How we introduce this

We create a testing setup using a ZIB filesystem.  The file data streams are
written to a scratch disk and replaced with placeholders after the backup
completed in order to simulate the throughput of a real system without
duplicating data unnecessary.  The ZIB filesystem is already in backup.

## Limitations

Intentionally left empty.

## Alternatives

### Bubbling mtime

Instead of tracking the mtime range in the toplevel repo `.nogtree`, parent
directories could be "touched" to ensure that their mtime is eventually newer
than all children.  We decided against bubbling mtimes, because it requires
write permission in order to change mtimes of the real toplevel directories.

The eventual state would include:

* The mtime of a parent directory is newer than the mtimes of all children.
  This ensures that changes to children can be detected by observing the mtime
  of the parent.
* The shadow `master-stat` has recorded the latest mtime of the parent
  directory.  This ensure that there will be a new commit on `master-stat` for
  every change on the filesystem.

The control loops would include:

* A regular scan that checks mtimes and bubbles newer mtimes to parent
  directories: The scan should include a safety check to protect against clock
  skew: mtimes must never be newer than the current time.
* A regular scan for uncommitted `master-stat` changes: The scan either
  automatically triggers a commit or notifies an owner that a commit is needed
  to ensure that file data is backed up.  Alternative: see section "mtime
  range".

The control loop described in more detail:

A regular scan ensures that the mtime of each repo toplevel directory is
greater or equal than the maximum of the mtimes of files below.  If a file
below is newer, we could either set the toplevel mtime to the maximum mtime of
the files below or we could set it to now, after checking that the maximum
mtime of the files below is not in the future.  We use now.  It felt slightly
more reasonable to use now in order to give unrelated observers a chance to
detect recent changes to the filesystem based on a time-based find.

The regular scan needs write permission to update the mtime of the toplevel
directory.  If it has only read permission, it can still report that an update
is needed.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

* Where to store the archive of archive secrets?
* How to manage GPG keys?
* Details of data archival.  For example, will we support explicit requests for
  a full archive of the current state?

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* v2.0.1, 2019-11-01: polishing
* 2019-10-28: frozen
* v2, 2019-07-24
* 2019-06-04: Paths now use ISO basic format
* 2019-03-06: Mention why the mtime range includes directory mtimes
* 2018-11-23: Patched GNU tar with `--listed-incremental-mtime`
* 2018-10-10: Discussion of `ctime` and `atime`
* 2018-06-27: Full BCPFS-like proof of concept
* v1, 2018-05-14: General polishing
* 2018-04-30: Simpler condition for full archive, avoiding dummy master-stat
  commits
* 2018-04-30: Decision: use mtime range instead of bubbling mtimes
* 2018-04-27: Alternative: mtime range
* 2018-04-26: Details on bubbling mtimes
* 2018-04-23: `metadata.tar`; `tarttconfig.yml`, Tartt store names; tracking
  Tartt in Git
* 2018-04-19: Initial version
