# NOE-10 -- POSIX ACLs on Samba Shares for Services and Organizational Units
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v3.1.2, 2018-03-15

2019-10-28: NOE-10 contains ideas that are actively used on BCPFS.

<div class="alert alert-warning"><p>
The analysis in v1 was incomplete; the conclusions were wrong.  The v2
approach, which used a combination of special settings and a custom module
`disable_chmod_acl`, was also incomplete.  v3 contains a more thorough analysis
and two possible solutions.
</p></div>

See [CHANGELOG](#changelog) at end of document.

Further related design decisions are document in other NOEs:

- [NOE-17](./../noe-17/noe-17-service-access-control.md) -- BCPFS access rules:
  Service-OrgUnits directories

## Open questions

The questions in this section need to be answered and the section removed
before the status is changed to final.

No open questions.

## Summary

This document assumes a detailed understanding of POSIX ACLs.  You should, in
particular, be familiar with Andreas Grünbacher, POSIX Access Control Lists on
Linux,
<https://www.usenix.org/legacy/events/usenix03/tech/freenix03/full_papers/gruenbacher/gruenbacher.pdf>
or
<http://web.archive.org/web/20161102055912/www.vanemery.com/Linux/ACL/POSIX_ACL_on_Linux.html>,
including the section on Samba.

[NOE-2](./../noe-2/noe-2-filesystem-repos.md) describes a filesystem design for
managing data of a research organization with facility services, such as
microscopes, and organization units, such as research labs.  The POSIX ACLs
described in NOE-2, however, do not work as intended with the Samba option
`inherit acls = yes`.

This document analyzes the problem and proposes a solution.

## Motivation

NOE-2 proposed POSIX ACLs like:

```
$ getfacl /ou_srv/data/ou/ag-foo/people
 # owner: root
 # group: ou_ag-foo
 # flags: -s-
user::rwx
group::rwx
group:ou_ag-foo:rwx
mask::rwx
other::---
default:user::rwx
default:group::rwx
default:group:ou_ag-foo:rwx
default:mask::rwx
default:other::---
```

The claim was that all directories and files below `people/` would be created
with uniform permissions via default POSIX ACL propagation if using
a reasonable default mode, and all would have the same owning group via setgid
propagation.

But with the following `smb.conf` options this is not true:

```
  writable        = yes
  create mode     = 0660
  directory mask  = 0770
  inherit acls    = yes
```

When a user `alice` who has a primary Unix group `ou_em-facility` creates
a directory, Samba assigns the following ACL:

```
$ getfacl /ou_srv/data/ou/ag-foo/people/alice
 # owner: alice
 # group: ou_em-facility
 # flags: -s-
user::rwx
user:alice:rwx
group::rwx
group:ou_ag-foo:rwx
group:ou_em-facility:rwx
mask::rwx
other::---
default:user::rwx
default:user:alice:rwx
default:group::rwx
default:group:ou_ag-foo:rwx
default:group:ou_em-facility:rwx
default:mask::rwx
default:other::---
```

The ACL differs from the default POSIX ACL that the Linux kernel assigns in
`mkdir()`; see man pages mkdir(2) and acl(5).  The owning group is the primary
group of the owner and not the owning group of the parent directory, as it
should be with setgid.  The ACL contains additional named user and named group
entries in the access and default section.

Samba apparently modifies the default ACL, which was confirmed by watching the
filesystem when creating a directory on a Samba share.  The new directory
appeared with the default ACL and immediately after changed to the ACL above.

## Design

<div class="alert alert-warning"><p>
See also [NOE-17](./../noe-17/noe-17-service-access-control.md) -- BCPFS access
rules: Service-OrgUnits directories.
</p></div>

The new design ensures that for the common case, identical ACLs are assigned
when creating directories and files via Samba or directly on Linux.  The new
design should also be more suitable for Samba's approach of mapping POSIX ACLs
to Windows SIDs.

### Design summary

The owner and owning group are used for quota management.  The owning group,
however, is not directly used for access control anymore.

Each group, including the owning group, is listed as a named group ACL entry.
This is the same approach that Samba seems to use; see Samba wiki 'Setting up
a Share Using POSIX ACLs', <https://goo.gl/zdQ9pi>, and 'Setting up a Share
Using Windows ACLs', <https://goo.gl/3agtCP>.  Examples:

```
$ getfacl /ou_srv/data/srv/em-titan
 # owner: root
 # group: srv_em-titan
 # NO SGID.
user::rwx
group::---
group:srv_em-titan:r-x
group:srv_em-ops:r-x
mask::r-x
other::---
default:user::rwx
default:group::---
default:group:srv_em-titan:r-x
default:group:srv_em-ops:r-x
default:mask::r-x
default:other::---
```

```
$ getfacl /ou_srv/data/srv/em-titan/ag-foo
 # owner: root
 # group: ou_ag-foo
 # flags: -s-
user::rwx
group::---
group:srv_em-ops:rwx
group:ou_ag-foo:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:srv_em-ops:rwx
default:group:ou_ag-foo:rwx
default:mask::rwx
default:other::---
```

Samba is configured to leave POSIX ACLs unmodified, so that they remain as
propagated by the Linux kernel.  There are two alternatives how this can be
achieved.  They are described in a separate section.

Although not strictly necessary for default POSIX ACL propagation, Samba is
furthermore configured to never modify POSIX ACLs, so that permissions changes
through the Windows security dialog are ignored.

We use the force mode alternative.  We  switch to a patched Samba only if the
force mode hack does not work reliably.  Specifically, we install the custom
debs `samba-vfs-disable-chmod` and `samba-vfs-disable-setfacl` and use the
following Samba share settings:

```
  create mask           = 0660
  force create mode     = 0660
  directory mask        = 0770
  force directory mode  = 0770
  map archive           = no
  map hidden            = no
  map system            = no
  inherit permissions   = no
  inherit acls          = no
  vfs objects           = disable_chmod disable_setfacl
```

The Kernel contains a bug that looses the setgid bit when creating child
directories if default POSIX ACLs are present and the owning group of the
directory is not one of the current process's groups.  See details in separate
section.

The consequence is that files may have the wrong owning group.  Quota
accounting, therefore, does not work as intended.  This specifically happens
when facility operators store data for organizational units in microscope
directories.  Access permissions are unaffected, because they are managed
through named group ACL entries; see above.

We mitigate the setgid problem by running a script that fixes the owning group
and setgid bits from time to time.  Ideally, we would switch to a patched
kernel to avoid the wrong owning groups in the first place.

### Migration to the new design

The migration happen roughly as follows:

 - Change the Samba configuration for fake resources; use them for testing.
 - Modify `bcpfs-perm`; apply it to update the toplevel ACLs.
 - Change the Samba full configuration.
 - Apply a script to fix existing files and directories.
 - Repeatedly apply a check script to verify the desired ACLs.
 - Repeatedly apply script to fix owning group and setgid.

### Debian packaging

The custom VFS object `disable_chmod` is deployed as a separate deb
`samba-vfs-disable-chmod`.

The deb version is based on the Debian source that was used as a base for the
build, as indicated in the official Debian package changelog, followed by
`+bcpfs<serial>`, 1-based, to indicate that the package is maintained by us.
Our suffix must not contain a dash, so that the Debian build tools consider it
part of the Debian revision and strip it when determining the upstream source
version.  Example:

```
samba                       2:4.2.14+dfsg-0+deb8u7+b1
samba-vfs-disable-chmod     2:4.2.14+dfsg-0+deb8u7+bcpfs3
samba-vfs-disable-setfacl   2:4.2.14+dfsg-0+deb8u7+bcpfs3
```

Debian `Depends` is set to a range that indicates that the packages
`samba-vfs-disable-*` are compatible with the Samba `4.2.x` series.  The
official Samba deb can be upgraded, while keeping the custom VFS modules.

### Two Samba configuration alternatives

Both alternatives configure Samba to leave POSIX ACLs unmodified, so that they
remain as propagated by the Linux kernel.

The VFS module `disable_setfacl` is included in both approaches to prevent ACL
changes when trying to edit permissions through the Windows security dialog.

#### Samba force mode hack

The first alternative uses `force * mode` settings together with a custom
module to disable chmod operations:

```
  create mask           = 0660
  force create mode     = 0660
  directory mask        = 0770
  force directory mode  = 0770
  map archive           = no
  map hidden            = no
  map system            = no
  inherit permissions   = no
  inherit acls          = no
  vfs objects           = disable_chmod disable_setfacl
```

The options deliberately deviate from the Samba wiki instructions 'Setting up
a Share Using POSIX ACLs'.  The proposed `inherit acls = no` disables the Samba
code path that updates POSIX ACLs to handle the special `CREATOR GROUP` Windows
principal.  Instead, Samba leaves POSIX ACLs as created by the Linux kernel.
The options `force create mode` and `force directory mode` ensure that the
`mode` parameters in `mkdir()` and `open()` are suitable for POSIX ACLs default
propagation; see man page acl(5).

The custom VFS object `disable_chmod`, described in a separate section below
and included in detail in the supplementary information, ensures that Samba
chmod calls are ignored.  The `map * = no` options prevent mapping of special
Windows modes to Unix x-bits, which would cause mode modifications of existing
files.  Disabling the map code paths is redundant when chmod operations are
ignored.  The mapping is disabled for clarity, nonetheless.

#### Samba inherit posix acls patch

The second alternative uses a patched Samba server that has the additional
option `inherit posix acls` to disable the Samba ACL propagation.  Chmod
operations are disabled, too:

```
  create mask           = 0660
  directory mask        = 0770
  map archive           = no
  map hidden            = no
  map system            = no
  inherit permissions   = no
  inherit acls          = yes
  inherit posix acls    = yes
  vfs objects           = disable_chmod disable_setfacl
```

This option more clearly expresses the intend and precisely disables the code
path that we do not want without using the `force * mask` workaround, which
might break in the future.

### Source code for Samba VFS disable modules

The modules hook into Samba's VFS layer and pretend calls were successful
without touching the filesystem.

`disable_chmod`:

```c
#include "../source3/include/includes.h"
#include "lib/util/tevent_ntstatus.h"

static int disable_chmod_chmod(vfs_handle_struct *handle, const char *path,
                               mode_t mode)
{
        return 0;
}

static int disable_chmod_fchmod(vfs_handle_struct *handle, files_struct *fsp,
                                mode_t mode)
{
        return 0;
}

static int disable_chmod_chmod_acl(vfs_handle_struct *handle,
                                       const char *name, mode_t mode)
{
        return 0;
}

static int disable_chmod_fchmod_acl(vfs_handle_struct *handle,
                                        files_struct *fsp, mode_t mode)
{
        return 0;
}

struct vfs_fn_pointers disable_chmod_fns = {
        .chmod_fn = disable_chmod_chmod,
        .fchmod_fn = disable_chmod_fchmod,
        .chmod_acl_fn = disable_chmod_chmod_acl,
        .fchmod_acl_fn = disable_chmod_fchmod_acl,
};

NTSTATUS vfs_disable_chmod_init(void)
{
        return smb_register_vfs(SMB_VFS_INTERFACE_VERSION, "disable_chmod",
                                &disable_chmod_fns);
}
```

`disable_setfacl`:

```c
#include "../source3/include/includes.h"
#include "lib/util/tevent_ntstatus.h"

static int disable_setfacl_sys_acl_set_file(vfs_handle_struct *handle,
					    const char *name,
					    SMB_ACL_TYPE_T acltype,
					    SMB_ACL_T theacl)
{
	return 0;
}

static int disable_setfacl_sys_acl_set_fd(vfs_handle_struct *handle,
					  files_struct *fsp,
					  SMB_ACL_T theacl)
{
	return 0;
}

static int disable_setfacl_sys_acl_delete_def_file(vfs_handle_struct *handle,
						   const char *path)
{
	return 0;
}

struct vfs_fn_pointers disable_setfacl_fns = {
	.sys_acl_set_file_fn = disable_setfacl_sys_acl_set_file,
	.sys_acl_set_fd_fn = disable_setfacl_sys_acl_set_fd,
	.sys_acl_delete_def_file_fn = disable_setfacl_sys_acl_delete_def_file,
};

NTSTATUS vfs_disable_setfacl_init(void)
{
	return smb_register_vfs(SMB_VFS_INTERFACE_VERSION, "disable_setfacl",
				&disable_setfacl_fns);
}
```

See [supplementary information](#supplementary-information) for Git references.

### Samba code path analysis

The code analysis below confirms that the proposed options configure Samba to
use Linux default POSIX ACL propagation and disable the Samba code path that
manipulates the default ACLs.

The proposed Samba options are:

```
  create mask           = 0660
  force create mode     = 0660
  directory mask        = 0770
  force directory mode  = 0770
  map archive           = no
  map hidden            = no
  map system            = no
  inherit permissions   = no
  inherit acls          = no
  vfs objects           = disable_chmod disable_setfacl
```

The custom VFS objects are included in the supplementary information.

The code analysis was performed on Samba Git tag `samba-4.2.14`
c7c5fe127366aa8edb69247f80a4e015969cf1b3 'VERSION: Disable git snapshots for
the 4.2.14 release.' to match the Samba version that was used on Debian at the
time of writing:

```
$ date && apt-cache policy samba
Sat Jul 15 15:52:47 CEST 2017
samba:
  Installed: 2:4.2.14+dfsg-0+deb8u7+b1
  Candidate: 2:4.2.14+dfsg-0+deb8u7+b1
  Version table:
 *** 2:4.2.14+dfsg-0+deb8u7+b1 0
        500 http://security.debian.org/debian-security/ jessie/updates/main amd64 Packages
        100 /var/lib/dpkg/status
     2:4.2.14+dfsg-0+deb8u5 0
        500 http://ftp.de.debian.org/debian/ jessie/main amd64 Packages
```

Samba Bugzilla entries that seem related to the question how setgid is handled:

 - <https://bugzilla.samba.org/show_bug.cgi?id=12716>
 - <https://bugzilla.samba.org/show_bug.cgi?id=10647>
 - <https://bugzilla.samba.org/show_bug.cgi?id=9054>

The option `inherit acls` corresponds to calls to `lp_inherit_acls()`:

```
$ git grep -n lp_inherit_acl
source3/modules/vfs_ceph.c:329: if (lp_inherit_acls(SNUM(handle->conn))
source3/modules/vfs_default.c:445:      if (lp_inherit_acls(SNUM(handle->conn))
source3/smbd/open.c:2717:        if ((flags2 & O_CREAT) && lp_inherit_acls(SNUM(conn)) &&
source3/smbd/open.c:4769:               } else if (lp_inherit_acls(SNUM(conn))) {
```

The match in `vfs_ceph.c` seems irrelevant for our setup.  The other matches
are discussed in detail.

`vfs_default.c`: With `inherit acls = yes`, the following condition sets `mode`
in `mkdir()` to the value of the config option `directory mask`, so that the
POSIX default ACL is correctly propagated:

```
source3/modules/vfs_default.c-437-static int vfswrap_mkdir(vfs_handle_struct *handle, const char *path, mode_t mode)
source3/modules/vfs_default.c-438-{
...
source3/modules/vfs_default.c:445:      if (lp_inherit_acls(SNUM(handle->conn))
source3/modules/vfs_default.c-446-          && parent_dirname(talloc_tos(), path, &parent, NULL)
source3/modules/vfs_default.c-447-          && (has_dacl = directory_has_default_acl(handle->conn, parent)))
source3/modules/vfs_default.c-448-              mode = (0777 & lp_directory_mask(SNUM(handle->conn)));
...
source3/modules/vfs_default.c-452-      result = mkdir(path, mode);
```

The proposed `smb.conf` options achieve the same `mode` by using `force
directory mode`.  See analysis of `unix_mode()` below.

`open.c`: With `inherit acls = yes`, the following condition sets `unx_mode`,
which is later used as the `mode` for `open()`, to the value of the config
option `create mask`:

```
source3/smbd/open.c:2717:        if ((flags2 & O_CREAT) && lp_inherit_acls(SNUM(conn)) &&
source3/smbd/open.c-2718-           (def_acl = directory_has_default_acl(conn, parent_dir))) {
source3/smbd/open.c-2719-               unx_mode = (0777 & lp_create_mask(SNUM(conn)));
source3/smbd/open.c-2720-       }

source3/smbd/open.c-2728-       fsp_open = open_file(fsp, conn, req, parent_dir,
source3/smbd/open.c-2729-                            flags|flags2, unx_mode, access_mask,
source3/smbd/open.c-2730-                            open_access_mask, &new_file_created);
```

The proposed `smb.conf` options achieve the same `unx_mode` by using `force
create mode`.  See analysis of `unix_mode()` below.

The second match in `open.c` calls `inherit_new_acl()` if `inherit acls = yes`:

```
source3/smbd/open.c:4769:               } else if (lp_inherit_acls(SNUM(conn))) {
source3/smbd/open.c-4770-                       /* Inherit from parent. Errors here are not fatal. */
source3/smbd/open.c-4771-                       status = inherit_new_acl(fsp);
```

`inherit_new_acl()` is the call that we want to avoid, because it calls
`chown()` and manipulates the default ACL that the Linux kernel created.  The
call is avoided with `inherit acls = no`.

The alternative solution adds an option `inherit posix acls = yes`, which can
be used to disable the call to `inherit_new_acl()` explicitly:

```
... else if (lp_inherit_acls(SNUM(conn)) && !lp_inherit_posix_acls(SNUM(conn))) {
    /* Inherit from parent. Errors here are not fatal. */
    status = inherit_new_acl(fsp);
    ...
```

It is the only call site to `inherit_new_acl()`:

```
$ git grep -n inherit_new_acl
source3/smbd/open.c:3967:static NTSTATUS inherit_new_acl(files_struct *fsp)
source3/smbd/open.c:4015:               DEBUG(10,("inherit_new_acl: parent acl for %s is:\n",
source3/smbd/open.c:4143:               DEBUG(10,("inherit_new_acl: child acl for %s is:\n",
source3/smbd/open.c:4771:                       status = inherit_new_acl(fsp);
source3/smbd/open.c:4773:                               DEBUG(10,("inherit_new_acl: failed for %s with %s\n",
```

`unix_mode()` determines the `mode`.  `force directory mode` and `force create
mode` are evaluated here to override the `result`:

```
source3/smbd/dosmode.c:112:mode_t unix_mode(connection_struct *conn, int dosmode,
...
source3/smbd/dosmode.c-117-     mode_t dir_mode = 0; /* Mode of the inherit_from directory if
source3/smbd/dosmode.c-118-                           * inheriting. */
...
source3/smbd/dosmode.c-124-     if ((inherit_from_dir != NULL) && lp_inherit_permissions(SNUM(conn))) {
...
// We do not use inherit permissions, so dir_mode remains 0.
...
source3/smbd/dosmode.c-157-     if (IS_DOS_DIR(dosmode)) {
...
source3/smbd/dosmode.c-169-                     /* Apply directory mask */
source3/smbd/dosmode.c-170-                     result &= lp_directory_mask(SNUM(conn));
source3/smbd/dosmode.c-171-                     /* Add in force bits */
source3/smbd/dosmode.c:172:                     result |= lp_force_directory_mode(SNUM(conn));
...
source3/smbd/dosmode.c-174-     } else {
...
source3/smbd/dosmode.c-188-                     /* Apply mode mask */
source3/smbd/dosmode.c-189-                     result &= lp_create_mask(SNUM(conn));
source3/smbd/dosmode.c-190-                     /* Add in force bits */
source3/smbd/dosmode.c-191-                     result |= lp_force_create_mode(SNUM(conn));
...
source3/smbd/dosmode.c-193-     }
...
source3/smbd/dosmode.c-197-     return(result);
```

Call sites to `unix_mode()` in `open.c`:

```
$ git grep -n 'unix_mode(' -- source3/smbd/open.c
source3/smbd/open.c:2467:               unx_mode = unix_mode(conn, new_dos_attributes | FILE_ATTRIBUTE_ARCHIVE,
source3/smbd/open.c:3279:               mode = unix_mode(conn, FILE_ATTRIBUTE_DIRECTORY, smb_dname, parent_dir);
```

The first call site leads to `open_file()`:

```
source3/smbd/open.c:2467:               unx_mode = unix_mode(conn, new_dos_attributes | FILE_ATTRIBUTE_ARCHIVE,
source3/smbd/open.c-2468-                                    smb_fname, parent_dir);
...
source3/smbd/open.c-2729-       fsp_open = open_file(fsp, conn, req, parent_dir,
source3/smbd/open.c-2730-                            flags|flags2, unx_mode, access_mask,
source3/smbd/open.c-2731-                            open_access_mask, &new_file_created);
```

The second call site leads to `mkdir()`:

```
source3/smbd/open.c:3279:               mode = unix_mode(conn, FILE_ATTRIBUTE_DIRECTORY, smb_dname, parent_dir);
...
source3/smbd/open.c-3294-       if (SMB_VFS_MKDIR(conn, smb_dname->base_name, mode) != 0) {
```

If `inherit acls = no`, Samba applies `FCHMOD_ACL()` to modify the file mode
after the Kernel propagated default POSIX ACLs:

```
$ git grep -n -C 5 _FCHMOD_ACL -- source3/smbd/open.c
source3/smbd/open.c-3196-       if (!posix_open && new_file_created && !def_acl) {
source3/smbd/open.c-3197-
source3/smbd/open.c-3198-               int saved_errno = errno; /* We might get ENOSYS in the next
source3/smbd/open.c-3199-                                         * call.. */
source3/smbd/open.c-3200-
source3/smbd/open.c:3201:               if (SMB_VFS_FCHMOD_ACL(fsp, unx_mode) == -1 &&
source3/smbd/open.c-3202-                   errno == ENOSYS) {
source3/smbd/open.c-3203-                       errno = saved_errno; /* Ignore ENOSYS */
source3/smbd/open.c-3204-               }
source3/smbd/open.c-3205-
source3/smbd/open.c-3206-       } else if (new_unx_mode) {
--
source3/smbd/open.c-3210-               /* Attributes need changing. File already existed. */
source3/smbd/open.c-3211-
source3/smbd/open.c-3212-               {
source3/smbd/open.c-3213-                       int saved_errno = errno; /* We might get ENOSYS in the
source3/smbd/open.c-3214-                                                 * next call.. */
source3/smbd/open.c:3215:                       ret = SMB_VFS_FCHMOD_ACL(fsp, new_unx_mode);
source3/smbd/open.c-3216-
source3/smbd/open.c-3217-                       if (ret == -1 && errno == ENOSYS) {
source3/smbd/open.c-3218-                               errno = saved_errno; /* Ignore ENOSYS */
source3/smbd/open.c-3219-                       } else {
source3/smbd/open.c-3220-                               DEBUG(5, ("open_file_ntcreate: reset "
```

If `inherit acls = no`, Samba applies `CHMOD_ACL()` to modify the mode for new
directories:

```
$ git grep -n -C 30  _CHMOD_ACL source3/modules/vfs_default.c
source3/modules/vfs_default.c-437-static int vfswrap_mkdir(vfs_handle_struct *handle, const char *path, mode_t mode)
source3/modules/vfs_default.c-438-{
source3/modules/vfs_default.c-439-      int result;
source3/modules/vfs_default.c-440-      bool has_dacl = False;
source3/modules/vfs_default.c-441-      char *parent = NULL;
source3/modules/vfs_default.c-442-
source3/modules/vfs_default.c-443-      START_PROFILE(syscall_mkdir);
source3/modules/vfs_default.c-444-
source3/modules/vfs_default.c-445-      if (lp_inherit_acls(SNUM(handle->conn))
source3/modules/vfs_default.c-446-          && parent_dirname(talloc_tos(), path, &parent, NULL)
source3/modules/vfs_default.c-447-          && (has_dacl = directory_has_default_acl(handle->conn, parent)))
source3/modules/vfs_default.c-448-              mode = (0777 & lp_directory_mask(SNUM(handle->conn)));
source3/modules/vfs_default.c-449-
source3/modules/vfs_default.c-450-      TALLOC_FREE(parent);
source3/modules/vfs_default.c-451-
source3/modules/vfs_default.c-452-      result = mkdir(path, mode);
source3/modules/vfs_default.c-453-
source3/modules/vfs_default.c-454-      if (result == 0 && !has_dacl) {
source3/modules/vfs_default.c-455-              /*
source3/modules/vfs_default.c-456-               * We need to do this as the default behavior of POSIX ACLs
source3/modules/vfs_default.c-457-               * is to set the mask to be the requested group permission
source3/modules/vfs_default.c-458-               * bits, not the group permission bits to be the requested
source3/modules/vfs_default.c-459-               * group permission bits. This is not what we want, as it will
source3/modules/vfs_default.c-460-               * mess up any inherited ACL bits that were set. JRA.
source3/modules/vfs_default.c-461-               */
source3/modules/vfs_default.c-462-              int saved_errno = errno; /* We may get ENOSYS */
source3/modules/vfs_default.c:463:              if ((SMB_VFS_CHMOD_ACL(handle->conn, path, mode) == -1) && (errno == ENOSYS))
source3/modules/vfs_default.c-464-                      errno = saved_errno;
source3/modules/vfs_default.c-465-      }
source3/modules/vfs_default.c-466-
source3/modules/vfs_default.c-467-      END_PROFILE(syscall_mkdir);
source3/modules/vfs_default.c-468-      return result;
source3/modules/vfs_default.c-469-}
```

Again, `CHMOD_ACL()` can be disabled by returing `ENOSYS`.

There are further `*CHMOD_ACL()` and `*CHMOD()` call sites.  Suitable `map ...
= no` options may disable them.  But it seems safer to simply disable all chmod
operations in the VFS layer.

Furthermore, ACLs are directly manipulated as a result of permission changes
through the Windows security dialog.  To prevent those changes, VFS calls
`SYS_ACL_SET_*` and `SYS_ACL_DELETE_*` should be disabled.

### Kernel setgid patch

The kernel contains logic that drops setgid during change mode operations,
like `fs/posix_acl.c`:

```c
/**
 * As with chmod, clear the setgit bit if the caller is not in the owning group
 * or capable of CAP_FSETID (see inode_change_ok).
 */
int posix_acl_update_mode(struct inode *inode, umode_t *mode_p,
			  struct posix_acl **acl)
{
	// ...
	if (!in_group_p(inode->i_gid) &&
	    !capable_wrt_inode_uidgid(inode, CAP_FSETID))
		mode &= ~S_ISGID;
	// ...
}
```

The code path was unintentionally active also during subdirectory setgid
propagation, where setgid should not be dropped.  The issues has been fixed
with the following patch, which landed in 4.13.  Older kernels have been fixed
or are likely to be fixed soon:

 * stable 4.12.6:
   <https://cdn.kernel.org/pub/linux/kernel/v4.x/ChangeLog-4.12.6>,
   <https://patchwork.kernel.org/patch/9891301/>.
 * Debian: <https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=873026>.

```
commit a3bb2d5587521eea6dab2d05326abb0afb460abd
Author: Jan Kara <jack@suse.cz>
Date:   Sun Jul 30 23:33:01 2017 -0400

    ext4: Don't clear SGID when inheriting ACLs

    When new directory 'DIR1' is created in a directory 'DIR0' with SGID bit
    set, DIR1 is expected to have SGID bit set (and owning group equal to
    the owning group of 'DIR0'). However when 'DIR0' also has some default
    ACLs that 'DIR1' inherits, setting these ACLs will result in SGID bit on
    'DIR1' to get cleared if user is not member of the owning group.

    Fix the problem by moving posix_acl_update_mode() out of
    __ext4_set_acl() into ext4_set_acl(). That way the function will not be
    called when inheriting ACLs which is what we want as it prevents SGID
    bit clearing and the mode has been properly set by posix_acl_create()
    anyway.

    Fixes: 073931017b49d9458aa351605b43a7e34598caef
    CC: stable@vger.kernel.org
    Signed-off-by: Theodore Ts'o <tytso@mit.edu>
    Signed-off-by: Jan Kara <jack@suse.cz>
    Reviewed-by: Andreas Gruenbacher <agruenba@redhat.com>

diff --git a/fs/ext4/acl.c b/fs/ext4/acl.c
index 2985cd0a640d..46ff2229ff5e 100644
--- a/fs/ext4/acl.c
+++ b/fs/ext4/acl.c
@@ -189,18 +189,10 @@ __ext4_set_acl(handle_t *handle, struct inode *inode, int type,
 	void *value = NULL;
 	size_t size = 0;
 	int error;
-	int update_mode = 0;
-	umode_t mode = inode->i_mode;

 	switch (type) {
 	case ACL_TYPE_ACCESS:
 		name_index = EXT4_XATTR_INDEX_POSIX_ACL_ACCESS;
-		if (acl) {
-			error = posix_acl_update_mode(inode, &mode, &acl);
-			if (error)
-				return error;
-			update_mode = 1;
-		}
 		break;

 	case ACL_TYPE_DEFAULT:
@@ -224,11 +216,6 @@ __ext4_set_acl(handle_t *handle, struct inode *inode, int type,
 	kfree(value);
 	if (!error) {
 		set_cached_acl(inode, type, acl);
-		if (update_mode) {
-			inode->i_mode = mode;
-			inode->i_ctime = current_time(inode);
-			ext4_mark_inode_dirty(handle, inode);
-		}
 	}

 	return error;
@@ -240,6 +227,8 @@ ext4_set_acl(struct inode *inode, struct posix_acl *acl, int type)
 	handle_t *handle;
 	int error, credits, retries = 0;
 	size_t acl_size = acl ? ext4_acl_size(acl->a_count) : 0;
+	umode_t mode = inode->i_mode;
+	int update_mode = 0;

 	error = dquot_initialize(inode);
 	if (error)
@@ -254,7 +243,20 @@ ext4_set_acl(struct inode *inode, struct posix_acl *acl, int type)
 	if (IS_ERR(handle))
 		return PTR_ERR(handle);

+	if ((type == ACL_TYPE_ACCESS) && acl) {
+		error = posix_acl_update_mode(inode, &mode, &acl);
+		if (error)
+			goto out_stop;
+		update_mode = 1;
+	}
+
 	error = __ext4_set_acl(handle, inode, type, acl, 0 /* xattr_flags */);
+	if (!error && update_mode) {
+		inode->i_mode = mode;
+		inode->i_ctime = current_time(inode);
+		ext4_mark_inode_dirty(handle, inode);
+	}
+out_stop:
 	ext4_journal_stop(handle);
 	if (error == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
 		goto retry;
```

### Diff from NOE-2 to NOE-10 ACLs

This section illustrates the new ACL design as a diff between the old NOE-2
design and the new NOE-10 proposal.

#### Service toplevel directory

`/srv/<service>/` diff NOE-2 NOE-10:

```
diff --git a/noe-2 b/noe-10
--- a/noe-2
+++ b/noe-10
@@ -4,12 +4,14 @@ $ getfacl /ou_srv/data/srv/em-titan
  # owner: root
  # group: srv_em-titan
  # NO SGID.
 user::rwx
-group::r-x
+group::---
+group:srv_em-titan:r-x
 group:srv_em-ops:r-x
 mask::r-x
 other::---
 default:user::rwx
-default:group::r-x
+default:group::---
+default:group:srv_em-titan:r-x
 default:group:srv_em-ops:r-x
 default:mask::r-x
 default:other::---
```

`/srv/<service>/` NOE-2:

```
$ getfacl /ou_srv/data/srv/em-titan
 # owner: root
 # group: srv_em-titan
 # NO SGID.
user::rwx
group::r-x
group:srv_em-ops:r-x
mask::r-x
other::---
default:user::rwx
default:group::r-x
default:group:srv_em-ops:r-x
default:mask::r-x
default:other::---
```

`/srv/<service>/` NOE-10:

```
$ getfacl /ou_srv/data/srv/em-titan
 # owner: root
 # group: srv_em-titan
 # NO SGID.
user::rwx
group::---
group:srv_em-titan:r-x
group:srv_em-ops:r-x
mask::r-x
other::---
default:user::rwx
default:group::---
default:group:srv_em-titan:r-x
default:group:srv_em-ops:r-x
default:mask::r-x
default:other::---
```

#### Service OU directory

`/srv/<service>/<org-unit>/` diff NOE-2 NOE-10:

```
diff --git a/noe-2 b/noe-10
--- a/noe-2
+++ b/noe-10
@@ -3,12 +3,14 @@ $ getfacl /ou_srv/data/srv/em-titan/ag-foo
  # owner: root
  # group: ou_ag-foo
  # flags: -s-
 user::rwx
-group::rwx
+group::---
+group:ou_ag-foo:rwx
 group:srv_em-ops:rwx
 mask::rwx
 other::---
 default:user::rwx
-default:group::rwx
+default:group::---
+default:group:ou_ag-foo:rwx
 default:group:srv_em-ops:rwx
 default:mask::rwx
 default:other::---
```

`/srv/<service>/<org-unit>/` NOE-2:

```
$ getfacl /ou_srv/data/srv/em-titan/ag-foo
 # owner: root
 # group: ou_ag-foo
 # flags: -s-
user::rwx
group::rwx
group:srv_em-ops:rwx
mask::rwx
other::---
default:user::rwx
default:group::rwx
default:group:srv_em-ops:rwx
default:mask::rwx
default:other::---
```

`/srv/<service>/<org-unit>/` NOE-10:

```
$ getfacl /ou_srv/data/srv/em-titan/ag-foo
 # owner: root
 # group: ou_ag-foo
 # flags: -s-
user::rwx
group::---
group:ou_ag-foo:rwx
group:srv_em-ops:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:ou_ag-foo:rwx
default:group:srv_em-ops:rwx
default:mask::rwx
default:other::---
```

#### OU toplevel directory

`/ou/<org-unit>/` diff NOE-2 NOE-10:

```
diff --git a/noe-2 b/noe-10
--- a/noe-2
+++ b/noe-10
@@ -3,5 +3,12 @@ $ getfacl /ou_srv/data/ou/ag-foo
  # owner: root
  # group: ou_ag-foo
- # NO SGID.
+ # flags: -s-
 user::rwx
-group::r-x
+group::---
+group:ou_ag-foo:r-x
+mask::r-x
 other::---
+default:user::rwx
+default:group::---
+default:group:ou_ag-foo:r-x
+default:mask::r-x
+default:other::---
```

`/ou/<org-unit>/` NOE-2:

```
$ getfacl /ou_srv/data/ou/ag-foo
 # owner: root
 # group: ou_ag-foo
 # NO SGID.
user::rwx
group::r-x
other::---
```

`/ou/<org-unit>/` NOE-10:

```
$ getfacl /ou_srv/data/ou/ag-foo
 # owner: root
 # group: ou_ag-foo
 # flags: -s-
user::rwx
group::---
group:ou_ag-foo:r-x
mask::r-x
other::---
default:user::rwx
default:group::---
default:group:ou_ag-foo:r-x
default:mask::r-x
default:other::---
```

#### OU subdirectory

`/ou/<org-unit>/projects/` diff NOE-2 NOE-10:

```
diff --git a/noe-2 b/noe-10
--- a/noe-2
+++ b/noe-10
@@ -3,12 +3,12 @@ $ getfacl /ou_srv/data/ou/ag-foo/people
  # owner: root
  # group: ou_ag-foo
  # flags: -s-
 user::rwx
-group::rwx
+group::---
 group:ou_ag-foo:rwx
 mask::rwx
 other::---
 default:user::rwx
-default:group::rwx
+default:group::---
 default:group:ou_ag-foo:rwx
 default:mask::rwx
 default:other::---
```

`/ou/<org-unit>/projects/` NOE-2:

```
$ getfacl /ou_srv/data/ou/ag-foo/people
 # owner: root
 # group: ou_ag-foo
 # flags: -s-
user::rwx
group::rwx
group:ou_ag-foo:rwx
mask::rwx
other::---
default:user::rwx
default:group::rwx
default:group:ou_ag-foo:rwx
default:mask::rwx
default:other::---
```

`/ou/<org-unit>/projects/` NOE-10:

```
$ getfacl /ou_srv/data/ou/ag-foo/people
 # owner: root
 # group: ou_ag-foo
 # flags: -s-
user::rwx
group::---
group:ou_ag-foo:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:ou_ag-foo:rwx
default:mask::rwx
default:other::---
```

### How to fix existing files and directories?

The ACLs of existing files and directories should be fixed using a one-time
script.  The rest of this section contains building blocks that could be useful
in a deployment-specific script.  The actual script is not discussed here.

A direct way is to read the desired permissions from the toplevel directory and
recursively apply them:

```bash
toplevel='/ou_srv/data/ou/ag-foo/people'

dirAcl="$(mktemp -t 'dir.acl.XXXXXXXXX')" && echo "${dirAcl}"
fileAcl="$(mktemp -t 'file.acl.XXXXXXXXX')" && echo "${fileAcl}"

getfacl --absolute-names --omit-header "${toplevel}" >"${dirAcl}"
echo '# dir acl:' && cat "${dirAcl}"

 # Drop default entries and x-bit for file ACL.
(
    grep -v '^default:' "${dirAcl}" \
    | sed --regexp-extended -e '/^(user|mask|other)/ s/x$/-/'
) >"${fileAcl}"
echo '# file acl:' && cat "${fileAcl}"

 # sudo='' or sudo='echo' can be useful for testing.
sudo='sudo'

 # Owning group.
${sudo} chgrp --reference="${toplevel}" -R "${toplevel}"

 # SGID.
find "${toplevel}" -type d -print0 \
| xargs -0 --no-run-if-empty ${sudo} chmod g+s --

 # Dir ACLs.
find "${toplevel}" -type d -print0 \
| xargs -0 --no-run-if-empty ${sudo} setfacl --set-file="${dirAcl}" --

 # File ACLs.
find "${toplevel}" -type f -print0 \
| xargs -0 --no-run-if-empty ${sudo} setfacl --set-file="${fileAcl}" --
```

The full implementation is
`bcpfs_fuimages_2017/bcpfs/bin/bcpfs-propagate-toplevel-acls`.

Find files and directories with the wrong owning group in the srv tree:

```bash
srvroot='/ou_srv/data/srv'
device='em-titan'
ouPrefix='srv_'
ou='ag-foo'

sudo \
find "${srvroot}/${device}/${ou}" \! -group "${ouPrefix}${ou}" -print
```

Find files and directories with the wrong owning group in the ou tree:

```bash
ouroot='/ou_srv/data/ou'
ouPrefix='ou_'
ou='ag-foo'
subdir='people'

sudo \
find "${ouroot}/${ou}/${subdir}" \! -group "${ouPrefix}${ou}" -print
```

List all owning groups that are used somewhere in a directory tree:

```bash
toplevel='/ou_srv/data/ou/ag-foo/people'

sudo \
getfacl -R -p "${toplevel}" \
| grep '^# group:' \
| sort -u
```

List all named user and named group ACL entries that are used somewhere in
a directory tree:

```bash
toplevel='/ou_srv/data/ou/ag-foo/people'

sudo \
getfacl -R -p "${toplevel}" \
| egrep '^(default:)?(user|group):[^:]' \
| sort -u
```

Find files and directories that have a named user ACL:

```bash
toplevel='/ou_srv/data/ou/ag-foo/people'

sudo \
getfacl -R -p "${toplevel}" \
| python3 -c '
import sys;
import re;
s = sys.stdin.read();
acls = re.split(r"\n\s*\n", s)
acls = [a for a in acls if re.search("^user:[^:]", a, re.MULTILINE)]
print("\n\n".join(acls))
' \
| grep '# file:'
```

Find files and directories that have a specific named group ACL:

```bash
toplevel='/ou_srv/data/ou/ag-foo/people'
namedGroup='ou_ag-bar'

sudo \
getfacl -R -p "${toplevel}" \
| python3 -c '
import sys;
import re;
s = sys.stdin.read();
acls = re.split(r"\n\s*\n", s)
acls = [a for a in acls if re.search("^group:'${namedGroup}'", a, re.MULTILINE)]
print("\n\n".join(acls))
' \
| grep '# file:'
```

## How we introduce this

We update the permissions and move on.  It is unlikely that users will notice
the difference.

Developers should remind each other to check how Samba uses POSIX ACLs when
designing ACLs for further use cases.

## Limitations

We deviate from the Samba recommendation how to configure POSIX ACLs.  This may
cause confusion.  There is also the risk that the Samba implementation changes
with unexpected impact on our design.

## Alternatives

We could keep `inherit acls = yes` and accept that Samba creates files with an
undesired owning group and additional named ACL entries.  The group could be
fixed in a separate step by a service daemon with root privileges.  It seems
preferable to get the desired ACLs right away and accept the risk that comes
from deviating from the Samba recommendation how to configure POSIX ACLs.

## Unresolved questions

The questions in this section seem relevant but will not be answered in this
NOE.  They are left for future work.

No unresolved questions.

## Supplementary information

The patched Samba source and the VFS modules `disable_chmod` and
`disable_setfacl` are available as the commits up to Git tag
`samba-inherit-posix-acls-2-4.2.14-dfsg-0-deb8u7-bcpfs3`, GitHub
<https://github.com/nogproject/samba/commits/samba-inherit-posix-acls-2-4.2.14-dfsg-0-deb8u7-bcpfs3>.

The main commits are:

* `samba@5ccccc48fa5370b69ffae1d3236f52f05006e361 'vfs_disable_chmod: Module to
  disable mode changes'`,
  <https://github.com/nogproject/samba/commit/5ccccc48fa5370b69ffae1d3236f52f05006e361>
* `samba@97c72c49a997546f387d64ee25108450132165de 'vfs_disable_setfacl: Module
  to disable POSIX ACL changes'`
  <https://github.com/nogproject/samba/commit/97c72c49a997546f387d64ee25108450132165de>
* `samba@9b862733ee41817ca84281d17d4d2e3f00d13f86 'inherit posix acls: New
  option to inherit default POSIX ACLs'`
  <https://github.com/nogproject/samba/commit/9b862733ee41817ca84281d17d4d2e3f00d13f86>

The other commits modify the Debian package.  All commits are also included as
patches in the supplementary tar file.

Supplementary tar:

```
noe-10-suppl.tar.gz
noe-10-suppl.tar.gz.asc
```

With the following content:

Source to build a patched Samba and the VFS modules is based on the Debian
source package:

```
noe-10/samba-inherit-posix-acls/deb/
noe-10/samba-inherit-posix-acls/deb/build-deb
noe-10/samba-inherit-posix-acls/deb/patches/0001-inherit-posix-acls-New-option-to-inherit-default-POS.patch
noe-10/samba-inherit-posix-acls/deb/patches/0002-vfs_disable_chmod-Module-to-disable-mode-changes.patch
noe-10/samba-inherit-posix-acls/deb/patches/0003-debian-Set-maintainer-to-Steffen-Prohaska.patch
noe-10/samba-inherit-posix-acls/deb/patches/0004-debian-Add-package-samba-vfs-disable-chmod.patch
noe-10/samba-inherit-posix-acls/deb/patches/0005-debian-Bump-to-Debian-source-suffix-2-4.2.14-dfsg-0-.patch
noe-10/samba-inherit-posix-acls/deb/patches/0006-vfs_disable_setfacl-Module-to-disable-POSIX-ACL-chan.patch
noe-10/samba-inherit-posix-acls/deb/patches/0007-debian-Add-package-samba-vfs-disable-setfacl.patch
noe-10/samba-inherit-posix-acls/deb/patches/0008-debian-Bump-to-Debian-source-suffix-2-4.2.14-dfsg-0-.patch
```

Pre-built binary package for Debian Jessie:

```
noe-10/samba-vfs-disable-chmod_4.2.14+dfsg-0+deb8u7+bcpfs3_amd64.deb
noe-10/samba-vfs-disable-chmod_4.2.14+dfsg-0+deb8u7+bcpfs3_amd64.deb.asc
noe-10/samba-vfs-disable-setfacl_4.2.14+dfsg-0+deb8u7+bcpfs3_amd64.deb
noe-10/samba-vfs-disable-setfacl_4.2.14+dfsg-0+deb8u7+bcpfs3_amd64.deb.asc
```

Docker environment to build and test the patched Samba and the VFS modules:

```
noe-10/samba-inherit-posix-acls/docker-compose.yml
noe-10/samba-inherit-posix-acls/Dockerfile
noe-10/samba-inherit-posix-acls/init-smbd
noe-10/samba-inherit-posix-acls/README.md
```

Build and test with:

```
cd noe-10/samba-inherit-posix-acls
docker-compose build
docker-compose up
```

Connect to Samba `localhost:9139`, users `alice`, `bob`, `charly`, password
always `test`.  Navigate to the shares `testdata-disable-chmod` and
`testdata-inherit-posix-acls`.  Create files and check permissions with:

```bash
docker exec -it samba bash
cd /samba/...
getfacl ...
```

## CHANGELOG

* 2019-11-01: polishing
* 2019-10-28: frozen
* v3.1.2, 2018-03-15: Refer to NOE-17 for `allOrgUnits` access rule
* v3.1.1, 2017-09-06: Added links to SGID Kernel patch backport to 4.12.6 and
  Debian kernel issue
* v3.1, 2017-08-23: New VFS module `disable_setfacl` to prevent changes via
  Windows security dialog
* v3, 2017-08-11: Updated analysis, Samba `map` options, Linux setgid
  propagation bug, `disable_chmod` VFS module
* v2.1, 2017-08-10: Clarified Debian packaging to use `+bcpfs<serial>` suffix
* v2, 2017-08-09: Substantially updated analysis.  New custom Samba VFS module
  `vfs_disable_chmod_acl` to disable Samba code paths that modified default
  POSIX ACLs
* v1.0.4, 2017-07-21: Refer to `bcpfs-propagate-toplevel-acls`
* v1.0.3, 2017-07-21: Fixed shell code to fix existing files and directories
* v1.0.2, 2017-07-21: Polishing
* v1, 2017-07-18: Complete design, as accepted after discussion
* 2017-07-15: Initial version
