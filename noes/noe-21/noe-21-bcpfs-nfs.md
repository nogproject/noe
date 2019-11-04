# NOE-21 -- Mounting BCPFS via NFS
By Steffen Prohaska, Uli Homberg
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v3, 2019-02-28

2019-10-28: NOE-13 contains ideas that are still relevant for BCPFS.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-21 describes how to use NFS to mount BCPFS on Linux machines.

## Motivation

[NOE-2](./../noe-2/noe-2-filesystem-repos.md) describes that we provide only
Samba shares for several reasons: security aspects are satisfied with Samba's
user-based authentication; mounting Samba shares is easy to teach and easy to
do; [NOE-10](./../noe-10/noe-10-posix-acls-smb-srv-ou.md) amended the solution
to use a patched Samba version that protects file attributes, including POSIX
ACLs, from client-side changes.

NFS, however, has been used in practice in two use cases, despite our initial
decision to provide only Samba:

* a Linux compute cluster;
* a Linux workstation for microscopy image processing.

Since NFS proved to be useful in practice, it might be useful to offer it as an
alternative to Samba in some situations or in general. This NOE analyzes the
technical considerations and documents our decision when and how NFS can be
used.

## Design

### Design summary

This is a summary of the design decisions.  See sections below for details.

We provide NFS exports of BCPFS to Linux clients within the FU network with two
security alternatives:

* Unix authentication for clients within trusted networks;
* Kerberos for other clients that are managed by trusted admins.

Both alternatives provide security comparable to Samba.  With NFS, users are,
however, able to modify file attributes while they cannot modify file
attributes when using a patched Samba as described in NOE-10.  We mitigate the
risk of undesired file attributes changes through NFS by teaching users and
regularly running a script that checks and fixes permissions.

Preliminary tests indicate that NFS performance is superior to GVfs Samba.

To ensure the same read/write access decisions with NFS Unix authentication as
with Samba, NFS must be configured with `--manage-gids` to avoid the 16-group
limit of NFS.

BCP clients will be mounted with NFSv3.  NFS exports stay configured with NFSv4
pseudo-file system and specified `fsid=0` root.  Access to mountable
sub-directory trees at the root-level of the NFSv4 pseudo-file system will be
controlled via permissions.

We decide on a case-by-case basis whether we offer NFS as an alternative to
Samba.  We, however, do not provide NFS in scenarios like the following:

* Linux clients that are not managed by trusted admins.
* Linux clients that do not already use ZEDAT user and group information.

### NFS security

Compared to Samba's user-based authentication, a traditional NFS setup uses
machine-based authentication, which can be compromised by hijacking the host's
IP or the UIDs and group IDs of known users.

The following two NFS security configurations are considered as secure as
Samba.

#### Machine-based authentication in a trusted network

Hosts, like a compute cluster, can be configured in the traditional way with
`sec=sys` to use Unix authentication if all host admins are trusted and the
hosts are attached to a trusted network, which includes that physical access is
restricted to trusted staff.  The client Unix identities and network traffic
can, therefore, be trusted.  Users cannot take over identities of other users.

#### Session-based authentication with Kerberos

NFS exports for hosts outside of a trusted network will be configured to use
Kerberos.  The NFS server, NFS client, and the user must be registered and
authenticated with Kerberos (see Suse Kerberos Doc, Kerberos and NFS,
[#KERBERIZED_NFS_LEVELS](#KERBERIZED_NFS_LEVELS) and Secure Data Access with
Kerberized NFS, [#KERBERIZED_NFS](#KERBERIZED_NFS)).  A user who wants to use
the kerberized NFS needs to get a session ticket from Kerberos and send this
ticket to the service.  The server validates the session ticket to authenticate
incoming requests.  Hijacking the host IP or UIDs is not possible that way.
Only active sessions with a valid session time stamp could be misused if a user
gained access to the machine and hijacked the session context of another,
currently authenticated user.

There are three levels of Kerberos security, `krb5`, `krb5i`, and `krb5p`, with
different level of protection of network traffic.  See Suse Kerberos Doc,
Kerberos and NFS, [#KERBERIZED_NFS_LEVELS](#KERBERIZED_NFS_LEVELS), for
details.  We consider them all sufficiently secure here.  See also [future
work](#future-work).

### Identity mapping

We assume that all client and server hosts use the same identity information,
usually a central LDAP, and can map between user names, group names, UIDs, and
GIDs in the same way.  The identity mapping is relevant for access checks and
also when inspecting file attributes on the client.

More complicated identity mapping should in principle be possible.  See
[limitations](#limitations) and [future work](#future-work).

### File access checks via NFS

Since NFSv3, read/write access decisions are delegated to the server.  The
details of the access decision depend on the NFS security configuration.

#### NFS without Kerberos, 16-group limit

For exports with `sec=sys`, the client sends user and group information that
the server should use for the access check.  The NFS protocol limits the number
of groups to 16.  For users who belong to more than 16 groups, the access check
may result in 'permission denied'.

To avoid this problem, the mount daemon on the server must be configured with
`--manage-gids`, which instructs the server to replace the client-sent group
list by the full list from the server (see man page `rpc.mountd(8)`,
[#MAN_RPC_MOUNTD](#MAN_RPC_MOUNTD)).

#### NFS with Kerberos

For clients that use Kerberos (`sec=krb5X`), the group information is not sent
by the client.  Only the user is identified based on the Kerberos ticket, and
the server performs a lookup to determine the user ID and the full group list
(see Suse Kerberos Doc, Group Membership,
[#KERBERIZED_NFS_GROUPS](#KERBERIZED_NFS_GROUPS)).  Using the server's group
list enables access checks that can include more than 16 groups per user.

For kerberized NFS, the `--manage-gids` option would not be necessary, but it
must be set, because we use NFS with and without Kerberos.

#### NFSv3 vs. NFSv4

Both, NFSv3 and VFSv4, provide `--manage-gids` to ensure that the server
ignores the client-sent group list and instead resolves the user's group
information on the server.

Both, NFSv3 and NFSv4, use POSIX ACLs for server side access checks in the same
way as for local file access checks, assuming that the user and supplementary
groups have been correctly resolved, as discussed above.

NFSv3 and NFSv4 differ in how ACLs can be inspected and modified from the
client.  NFSv3 works well with POSIX ACLs on Linux (see section "NFS and ACLs"
in Grünbacher's "POSIX Access Control Lists on Linux",
[#POSIX_ACL](#POSIX_ACL)).  Commands like `ls -l` and `getfacl` work on the
client as expected.  NFSv4 is different: it provides a mapping between POSIX
ACLs and NFSv4 ACLs (see Linux NFS Wiki,
[#NFS_WIKI_INTEROP](#NFS_WIKI_INTEROP)).  This mapping, however, is not
necessarily valid in both directions, because NFSv4 ACLs are richer than POSIX
ACLs.  On an NFSv4 mount, `ls -l` and `getfacl` do not show ACLs.  The file
appears as if it had no ACLs.  `nfs4_getfacl` can instead be used to inspect
the NFSv4 ACLs.  The representation, however, is very different from POSIX
ACLs.  It might be difficult to use in practice.

Regarding Kerberos authentication, NFSv3 and NFSv4 have similar level of
security (see Secure Data Access with Kerberized NFS,
[#KERBERIZED_NFS](#KERBERIZED_NFS)).

IMP conducted experiments to compare NFSv3 and NFSv4 performance:

 - NFSv3 performance is better for big files;
 - NFSv4 performance is better for small files, due to better client caching.

Image data management is likely to use big files.  NFSv3 would probably provide
better performance.  But we believe that performance difference is not
significant enough to justify NFSv3.

The firewall configuration for NFSv4 is simpler than for NFSv3.  NFSv4 is,
therefore, preferred if client and server communicate through a firewall that
is configured by a different IT department.  Specifically, IMP may use NFSv3 if
they manage server, clients, and the network between.  IMP wants to transition
to NFSv4 if traffic crosses through a firewall that is managed by ZEDAT.
Nevertheless, IMP suggests to continue using NFSv3 for BCP clients to avoid
potential problems with user-modified permissions based on NFSv4 ACLs, although
it is unclear what the potential problems should be. Permissions must be
regularly checked anyway, by using `bcpfs-propagate-toplevel-acls` for example,
because clients can create undesired permissions also with NFSv3, using `chmod`
or `setfacl` for example.

### Implications of unexpected file attribute changes

Users can modify file attributes with NFS while they cannot modify file
attributes when using a patched Samba as described in NOE-10.

Unexpected file attributes may cause problems.  An NFS user, for example, could
change the permissions of a directory such that the group cannot read the
content anymore.  A `nogfsostad` instance that assumes group-read permissions
would be unable to scan the directory and, thus, would not trigger a backup.

We continue to develop and operate services under the assumption of correct
permissions that allow the group to read files.  We mitigate the risk of
undesired file attributes changes through NFS by teaching users and regularly
running a script that checks and fixes permissions.  We will reconsider later
whether we should operate services with higher privileges to allow the services
to read files that lack group-read permissions.

### NFSv4 pseudo-file system

With NFSv4, directories can be exported as a pseudo-file system
([#RC_7530_PSEUDOFS](#RC_7530_PSEUDOFS)) whose root will be identified at a
certain directory tree level by marking this level with `fsid=0`.  Directories
are then mounted relative to that root.  In communication with IMP, this is
also referred to as NFSv4 pseudo-root.

IMP generally provides NFSv4 and configures NFS exports with NFSv4 pseudo-file
system.  The NFS exports with NFSv4 pseudo-file system are configured in
`/etc/exports`, like so:

```
## `fsid=0` indicates the NFSv4 pseudo-root.
/srv/nfs       192.168.120.3/32(fsid=0,sec=sys,crossmnt,rw,insecure,no_wdelay,async,no_subtree_check)
/srv/nfs/data  192.168.120.3/32(fsid=999,sec=sys,rw,insecure,no_wdelay,async,no_subtree_check)
```

The NFSv4 mount would then be relative to the `fsid=0`-marked root:

```
mount -t nfs 192.168.120.2:/data /mnt
```

On clients with NFSv3 mounting enabled, mounting the full path uses NFSv3:

```
mount -t nfs 192.168.120.2:/srv/nfs/data /mnt
```

The exported NFSv4 pseudo-file system provides access to the entire directory
tree of the configured `fsid=0` root, so that BCP clients can mount the
specified root `/srv/nfs` and any sub-directories like or `/srv/nfs/data2`,
which might not be desired.

IMP configures exports as described above assuming that the permissions of
sub-directory trees of `/srf/nfs/` control the read/write access
instead of preventing undesired mounts.  We follow this strategy and adjust the
permissions of the sub-directories as described in section [Service directory
permissions](#service-directory-permissions).

Alternatively, undesired access could be avoided by using an empty directory as
the root of the NFSv4 pseudo-file system and bind-mounting only selected
directories to that root in order to export them.

However, the Linux NFS Wiki ([#NFS_WIKI_EXPORTS](#NFS_WIKI_EXPORTS)) recommends
to not use the NFSv4 pseudo-file systems with a `fsid=0`-marked root anymore,
but to explicitly export the directories as used to be with NFSv3:

`/etc/exports`:

```
/srv/nfs/data  192.168.120.3/32(fsid=999,sec=sys,rw,insecure,no_wdelay,async,no_subtree_check)
```

The NFSv4 mount uses the full path:

```
mount -t nfs 192.168.120.2:/srv/nfs/data /mnt
```

To mount with NFSv3, the version must be set explicitly:

```
mount -t nfs -o vers=3 192.168.120.2:/srv/nfs/data /mnt
```

See also the Vagrant example in supplementary information.

Using the explicit paths, we could restrict the exports to the desired
directories.
See [supplementary config information](./noe-21-sup-imp-config-details.md) for
BCPFS specific configuration.

### Service directory permissions

The NFS export for BCPFS is configured with NFSv4 pseudo-file system so that
other sub-directories at that level are mountable for BCP clients.  We operate
hidden services in such sub-directories, to which the access must be restricted
for BCP users by permissions.

The access will be controlled at the top-level of the service directories by:

* denying access to BCP users by setting no `other` permissions and
* allowing access to the service daemons by setting the directory group to the
  service group `srvgrp` and permissions `g=rx`.

The `setgid` flag should also be set to ensure that child directories will have
the same service group `srvgrp`, for consistency:

```
# file: /srv/nfs/shadow
# owner: root
# group: srvgrp
# flags: -s-
user::rwx
group::r-x
other::---
```

If `setgid` is used, we could ensure `other=-` for intermediate sub-directories
by adjusting scripts to use `umask 0027` when automatically creating
intermediate sub-directories:

```
( umask 0027 && install ... -d shadow/bcp/ag-bar/projects )
( umask 0027 && mkdir -p ... shadow/bcp/ag-bar/projects )
```

But `other=-` at the top level is alone sufficient for security.

### Summary of NFS configuration details

This section summarizes the config details discussed above.

#### Avoiding the NFS 16-group Limit

The mount daemon option `--manage-gids` lets the server perform a lookup for
the user's full group list in case of security mode `sec=sys`.
`/etc/default/nfs-kernel-server` must contain:

```
RPCMOUNTDOPTS="--manage-gids"
```

#### Enabling NFS Kerberos

For NFS with Kerberos, the GSS daemon must be activated on the server and the
client.

On the server, `/etc/default/nfs-kernel-server` must contain:

```
NEED_SVCGSSD="yes"
```

On the client, `/etc/default/nfs-common` must contain:

```
NEED_GSSD=yes
```

#### NFS Kerberos security mode

Exports without Kerberos are configured with:

```
sec=sys
```

Exports with Kerberos must be configured to allow one to three of the Kerberos
security levels (see Suse Kerberos Doc, Kerberos and NFS,
[#KERBERIZED_NFS_LEVELS](#KERBERIZED_NFS_LEVELS)):

```
sec=krb5p:krb5i:krb5
```

#### NFS exports

We use IMP's default export configuration, which contains an `fsid=0` root for
the NFSv4 pseudo-file system.  But clients mount with full path to enable
NFSv3.
See [supplementary config information](./noe-21-sup-imp-config-details.md).

#### NFS Mounts

The security mode for mounts from clients in trusted networks is `sec=sys`.

Clients that use Kerberos should use the highest Kerberos security level
`sec=krb5p`.  We will reconsider the recommendation if we observe unexpectedly
low transfer rates.

### Scenarios in which we do not offer NFS

We do not provide NFS in scenarios like the following:

* Linux clients that are not managed by trusted admins.
* Linux clients that do not already use ZEDAT user and group information.

NFS Kerberos requires client configuration.  We restrict NFS to hosts that are
managed by a trusted admin to keep configuration overhead low.  In the simplest
case, the client is centrally managed by the same IT department that also
manages the NFS server, i.e. IMP.  But we would probably also support NFS in
situations where the client is managed by a trusted admin of a different
department, such as ZEDAT.  We will decide on a case-by-case basis.

We do not offer NFS to clients that do not already use ZEDAT user and group
information.  Although it might, in principle, be possible and secure with
Kerberos, we would expect considerable configuration complexity, which we want
to avoid.  We would allow NFS mounts on self-administered clients that use
Kerberos.  IMP would provide a more detailed documentation how to configure
Kerberos, but nobody has yet asked for it.

## How we introduce this

BCPFS is already mounted for two use cases: a compute cluster and a microscope
processing workstation.  Both use NFSv3.

We will initially decide on a case-by-case basis whether we offer NFS, based on
the following considerations.  We will later reconsider whether we offer NFS
more widely to owners of client machines and let them decide whether to use
Samba or NFS.

* From a security point of view, we could provide NFS exports as a general
  alternative to Samba.  The described NFS setup has security comparable to
  Samba.

* From a practical point of view, NFS mounts require configuration work by IMP
  admins (NFS export configuration, Kerberos registrations, and client host
  configuration), whereas GVfs Samba mounts can be managed by users alone.  If
  we export BCPFS to new clients via NFS, the users must, furthermore, be
  instructed to not run commands that change file attributes, like `chmod` or
  `setfacl`.

* From a performance point of view, NFS is clearly better in practice than GVfs
  Samba.  In a measurement on a workstation that is connected via the campus
  backbone, NFS saturated a GigE link, while GVfs Samba achieved only 35 MB/s.
  The reason, however, is not obvious: `smbclient` achieved performance that is
  comparable to NFS for similar scenarios; we did not try the Linux kernel CIFS
  driver; see alternatives and supplementary information for more.

## Limitations

NFS mounts require that clients and servers use consistent user identity
information.  We assume that all hosts have access to a central LDAP, e.g.
ZEDAT/IMP.  It is not immediately obvious how to configure BCPFS NFS mounts on
clients that use a different LDAP, e.g. at ZIB, although it could, in
principle, be possible.  We decided to provide NFS for now only to IMP/ZEDAT
clients within the FU network.

Adding an NFS mount requires more coordination than using a GVfs Samba mount.
NFS exports and mounts must be configured by IT admins, while GVfs Samba can be
mounted by users alone.

We control file permissions centrally.  NFS clients, however, can change file
attributes, whereas our patched Samba (see NOE-10) reliably avoids changes to
file attributes.  We are not aware of an option to configure the NFS server to
allow file data modifications but deny file attribute changes.  Instead, we ask
NFS users to not use commands that change file attributes and regularly check
and fix ACLs centrally.

## Alternatives

### GVfs Samba

An ordinary Linux user can mount a Samba share using the Gnome Virtual File
System (GVfs) with `gvfs-mount` or the newer `gio mount`, which uses the same
options:

```
$ gvfs-mount smb://${SERVER}:${PORT}/${SHARE}
 # or
$ gio mount smb://${SERVER}:${PORT}/${SHARE}
User [...]: ...
Domain [...]: ...
Password: ...
```

Alternatively, the `smb://` URL can be pasted in the address bar of the
Nautilus file manager.  Connect as a registered user and fill out the same
options as on the command line.

The mount appears as:

```
/run/user/${UID}/gvfs/smb-share:port=${PORT},server=${SERVER},share=${SHARE}
```

The share can be unmounted with:

```
$ gvfs-mount -u smb://${SERVER}:${PORT}/${SHARE}
 # or
$ gio mount -u smb://${SERVER}:${PORT}/${SHARE}
```

Adjusting this approach to SSH port forwarding is straightforward.

We compared performance with NFS and GVfs Samba on a workstation that is
connected to the file server via the campus backbone.  NFS saturated a GigE
link, while GVfs Samba achieved only 35 MB/s.  The reason for the performance
difference is not obvious.  See supplementary material for details.

### Samba Linux kernel driver `mount.cifs`

Another alternative could be to use `mount.cifs` instead of GVfs, see
[#MOUNT_WITH_CIFS](#MOUNT_WITH_CIFS).  It might be faster.  We did not compared
performance.

Users can be allowed to mount fstab entries if they have access to the mount
point.  `mount.cifs` will ask them for their password.  The mount must be
configured in `/etc/fstab/` like:

```
//${SERVER}/${SHARE} /${MOUNTPOINT} cifs username=${USERNAME},domain=${DOMAIN},noauto,rw,users 0 0
```

The user mounts with:

```
mount.cifs //${SERVER}/${SHARE} /${MOUNTPOINT}
```

As this setup indicates, `mount.cifs` does not automatically provide a naming
convention that separates per-user mounts, like GVfs does with
`/run/user/${UID}/...`.

If we wanted to generally use `mount.cifs`, we would need a mechanism to manage
our naming convention and separate per-user mounts, like some kind of
auto-mounter or a sudo wrapper script that allows users to run `mount.cifs` in
a secure way.

With `pam_mount`, it might be possible to automatically mount with CIFS on user
login without typing a password if the local username and password are
identical with those of the Samba share.  The share parameters must be added to
`/etc/security/pam_mount.conf.xml`.  But also, the login-manager and the system
must be configured to use `pam_mount`.  See
[#PAM_MOUNT_STACKEXCHANGE](#PAM_MOUNT_STACKEXCHANGE) or
[#PAM_MOUNT_ARCHLINUX](#PAM_MOUNT_ARCHLINUX) for more details.  `pam_mount`
could be a comfortable setup for Linux users, although its configuration
initially seems a bit complex.

### Patch an NFS server to prevent unexpected file attribute changes

Ideally, we would control all file permissions centrally.  But we are not aware
of an option to configure the Linux kernel NFS server to allow file data
modifications but deny file attribute changes.

We could try to modify an existing NFS server to prevent file attribute
changes.  We could modify the Linux kernel NFS server and run a custom kernel,
while trying to send patches upstream.  An easier path could be to switch to
a user-space NFS server and modify it.  Specifically, NFS Ganesha,
<https://github.com/nfs-ganesha/nfs-ganesha>, seems promising.

## Future work

As mentioned in [Limitations](#Limitations), we did not consider how to use
NFS with clients that do not yet use the same LDAP.  If we wanted to provide
NFS to other organizations than FU, we would have to find a solution that is
able to map ZEDAT user information on external clients.

If file attribute changes by clients turn out to be a problem in practice, we
should reconsider sever-side changes to prevent them.  See section on
alternatives.

The NFS Kerberos security level could perhaps be more precisely specified.
Exports currently allow all three levels.  Clients should use the highest
security level `sec=krb5p`.  We did not measure NFS performance for the
different Kerberos security levels.  If we were convinced that the highest
security level `sec=krb5p` provides good performance, we could configure
servers to require it.

GVfs Samba could be further analyzed to understand why its performance is worse
than NFS.  It might be possible to fix it and achieve a performance similar to
`smbclient`, whose performance is comparable to NFS, at least for copying large
files.

The Linux kernel CIFS driver could be more seriously considered as an
alternative to NFS.

## Supplementary information

* [noe-21-sup-imp-config-details](./noe-21-sup-imp-config-details.md):
  IMP-specific configuration details.
* [noe-21-sup-nfs-samba-performance](./noe-21-sup-nfs-samba-performance.md):
  Samba and NFS performance measurements.
* `Vagrantfile` to test NFSv3 and NFSv4 mounts in a VM environment.  See usage
  instruction at the end of the `Vagrantfile`.

### Samba and NFS performance measurements summary

<!--
Maintain this section in ./noe-21-sup-nfs-samba-performance.md and copy to
noe-21-bcpfs-nfs.md.
-->

Values that have not been measured are indicated by "n/a".

Microscopy workstation, connected via campus network, probably GigE:

* NFS: read 100 MB/s, write 100 MB/s, CPU n/a.
* GVfs Samba: read 35 MB/s, write 35 MB/s, CPU n/a.

Login host, connected via IT department LAN, maybe better than GigE:

* NFS: n/a.
* GVfs Samba: read 60 MB/s, write 50 MB/s, CPU 30-60%.
* `smbclient`: read 100 MB/s, write n/a, CPU 40%.

Compute cluster, connected via IT department LAN, probably 10 GigE:

* NFS: read 1.1 GB/s, write 660 MB/s, CPU n/a.
* `smbclient`: read 980 MB/s, write n/a, CPU n/a.

There is no immediate explanation for the low GVfs Samba performance.

See supplementary information
[noe-21-sup-nfs-samba-performance](./noe-21-sup-nfs-samba-performance.md) for
details.

## References

<!--

References are typeset as #SCREAMING_SNAKE_CASE hashtags that link to the
anchors in the reference section, where the full reference and the URL is
found.  The hashtag formatting alone is considered sufficient to separate the
links from the surrounding text without additional parentheses.  The
programming-like style makes the anchors suitable for searching and editing.
Maintaining URLs only in the reference section avoids error-prone duplication,
although it might be a bit inconvenient when reading the text.

-->

* <a name="KERBERIZED_NFS"></a>#KERBERIZED_NFS:
  Ulf Troppens, Secure Data Access with Kerberized NFS, 2014,
  <https://www.ibm.com/developerworks/community/blogs/storageneers/entry/secure_data_access_with_kerberized_nfs?lang=en>,
  retrieved 2018-09-26.

* <a name="KERBERIZED_NFS_GROUPS"></a>#KERBERIZED_NFS_GROUPS:
  openSUSE Leap 15 Security Guide, Network Authentication with Kerberos,
  Group Membership,
  <https://doc.opensuse.org/documentation/leap/security/html/book.security/cha.security.kerberos.html#sec.security.kerberos.overview.group>,
  retrieved 2018-09-26.

* <a name="KERBERIZED_NFS_LEVELS"></a>#KERBERIZED_NFS_LEVELS:
  openSUSE Leap 15 Security Guide, Network Authentication with Kerberos,
  Kerberos and NFS,
  <https://doc.opensuse.org/documentation/leap/security/html/book.security/cha.security.kerberos.html#sec.security.kerberos.nfs>,
  retrieved 2018-09-26.

* <a name="MAN_RPC_MOUNTD"></a>#MAN_RPC_MOUNTD:
  Linux man page `rpc.mountd(8)`,
  <https://linux.die.net/man/8/rpc.mountd>.

* <a name="MOUNT_WITH_CIFS"></a>#MOUNT_WITH_CIFS:
  Blog, Ubuntu 14.04 – How to properly mount a CIFS share as a normal user,
  <https://www.strika.co/ubuntu-14-04-how-to-properly-mount-a-cifs-share-as-a-normal-user/>.

* <a name="NFS_WIKI_INTEROP"></a>#NFS_WIKI_INTEROP:
  Linux NFS Wiki, Interoperability Strategies,
  <http://wiki.linux-nfs.org/wiki/index.php/ACLs#Interoperability_Strategies>,
  retrieved 2018-09-26.

* <a name="NFS_WIKI_EXPORTS"></a>#NFS_WIKI_EXPORTS:
  Linux NFS Wiki, Exporting directories,
  <https://wiki.linux-nfs.org/wiki/index.php/Nfsv4_configuration#Exporting_directories>,
  retrieved 2019-02-26.

* <a name="PAM_MOUNT_ARCHLINUX"></a>#PAM_MOUNT_ARCHLINUX:
  Arch Linux wiki, `pam_mount`,
  <https://wiki.archlinux.org/index.php/pam_mount>.

* <a name="PAM_MOUNT_STACKEXCHANGE"></a>#PAM_MOUNT_STACKEXCHANGE:
  Stack Exchange answer, proper way to mount samba share,
  <https://unix.stackexchange.com/a/83668>.

* <a name="POSIX_ACL"></a>#POSIX_ACL:
  Andreas Grünbacher, POSIX Access Control Lists on Linux, USENIX 2003,
  <https://www.usenix.org/legacy/publications/library/proceedings/usenix03/tech/freenix03/full_papers/gruenbacher/gruenbacher_html/main.html>,
  retrieved 2018-09-26.

* <a name="RC_7530_PSEUDOFS"></a>#RRC_7530_PSEUDOFS:
  T. Haynes et al., RFC 7530: Network File System (NFS) Version 4 Protocol, 2015,
  <https://tools.ietf.org/html/rfc7530#page-78>.

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v3, 2019-02-28
* 2019-02-25: Keep NFSv4 pseudo-file system configuration, keep NFSv3 for BCP clients
* 2018-11-01: Mention patched NFS server in alternatives, specifically NFS Ganesha
* 2018-10-23 Minor polishing
* v2, 2018-10-18
* 2018-10-18: New section on potential impact of unexpected file attributes
* 2018-10-18: Decision to use NFSv4 instead of NFSv3
* v1, 2018-10-05
* 2018-10-05: NFSv4 pseudo-root mentioned in design summary
* 2018-10-04: General polishing
* 2018-10-04: Preliminary discussion of Samba client performance in supplementary material
* 2018-10-02: GVfs Samba and NFS performance comparison
* 2018-09-26: Initial version
