# Contributing

Locations:

* Upstream: <https://github.com/samba-team/samba>
* Debian: <https://anonscm.debian.org/cgit/pkg-samba/samba.git/>
* Fork: <https://github.com/nogproject/samba>

Git tag:

```
debver="$(head -n 1 debian/changelog | cut -d '(' -f 2 | cut -d ')' -f 1)" && echo "debver: ${debver}"
tagver=$(tr ':+' '-' <<< "${debver}") && echo "tagver: ${tagver}"

git tag -a -s \
    -m "samba-inherit-posix-acls-${debver}" \
    samba-inherit-posix-acls-${tagver} \
    samba-inherit-posix-acls^

git push origin samba-inherit-posix-acls samba-inherit-posix-acls-${tagver}
```

Format patches:

```
base='debian/jessie'
 # Tie branch 'samba-inherit-posix-acls^'.
tip=samba-inherit-posix-acls-2-4.2.14-dfsg-0-deb8u7-bcpfs3
fork=~/github/samba-team/samba

rm -rf deb/patches/
git -C "${fork}" format-patch -o "$(pwd)/deb/patches" "${base}..${tip}"
```

## Example

Samba seems to create the same permissions for `testdata-inherit-posix-acls`
and `testdata-disable-chmod`.

Result of `mkdir` followed by `echo a >file` through Samba mount:

```
root@samba:/samba# getfacl testdata-inherit-posix-acls/device/alice
 # file: testdata-inherit-posix-acls/device/alice
 # owner: alice
 # group: ag-foo
 # flags: -s-
user::rwx
group::---
group:ag-foo:rwx
group:bar-ops:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:ag-foo:rwx
default:group:bar-ops:rwx
default:mask::rwx
default:other::---

root@samba:/samba# getfacl testdata-inherit-posix-acls/device/alice/a
 # file: testdata-inherit-posix-acls/device/alice/a
 # owner: alice
 # group: ag-foo
user::rw-
group::---
group:ag-foo:rwx		#effective:rw-
group:bar-ops:rwx		#effective:rw-
mask::rw-
other::---

root@samba:/samba# getfacl testdata-disable-chmod/device/alice/
 # file: testdata-disable-chmod/device/alice/
 # owner: alice
 # group: ag-foo
 # flags: -s-
user::rwx
group::---
group:ag-foo:rwx
group:bar-ops:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:ag-foo:rwx
default:group:bar-ops:rwx
default:mask::rwx
default:other::---

root@samba:/samba# getfacl testdata-disable-chmod/device/alice/a
 # file: testdata-disable-chmod/device/alice/a
 # owner: alice
 # group: ag-foo
user::rw-
group::---
group:ag-foo:rwx		#effective:rw-
group:bar-ops:rwx		#effective:rw-
mask::rw-
other::---

root@samba:/samba# getfacl testdata-inherit-posix-acls/device/bob/
 # file: testdata-inherit-posix-acls/device/bob/
 # owner: bob
 # group: ag-foo
 # ************* NOTE: No SGID due to Kernel bug; bob is not in group ag-foo.
user::rwx
group::---
group:ag-foo:rwx
group:bar-ops:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:ag-foo:rwx
default:group:bar-ops:rwx
default:mask::rwx
default:other::---

root@samba:/samba# getfacl testdata-inherit-posix-acls/device/bob/b
 # file: testdata-inherit-posix-acls/device/bob/b
 # owner: bob
 # group: bob
user::rw-
group::---
group:ag-foo:rwx		#effective:rw-
group:bar-ops:rwx		#effective:rw-
mask::rw-
other::---

root@samba:/samba# getfacl testdata-disable-chmod/device/bob
 # file: testdata-disable-chmod/device/bob
 # owner: bob
 # group: ag-foo
user::rwx
group::---
group:ag-foo:rwx
group:bar-ops:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:ag-foo:rwx
default:group:bar-ops:rwx
default:mask::rwx
default:other::---

root@samba:/samba# getfacl testdata-disable-chmod/device/bob/b
 # file: testdata-disable-chmod/device/bob/b
 # owner: bob
 # group: bob
user::rw-
group::---
group:ag-foo:rwx		#effective:rw-
group:bar-ops:rwx		#effective:rw-
mask::rw-
other::---

root@samba:/samba# getfacl testdata-inherit-posix-acls/
device/   people/   projects/
root@samba:/samba# getfacl testdata-inherit-posix-acls/people/charly
 # file: testdata-inherit-posix-acls/people/charly
 # owner: charly
 # group: ag-foo
 # flags: -s-
user::rwx
group::---
group:ag-foo:r-x
mask::r-x
other::---
default:user::rwx
default:group::---
default:group:ag-foo:r-x
default:mask::r-x
default:other::---

root@samba:/samba# getfacl testdata-inherit-posix-acls/people/charly/c
 # file: testdata-inherit-posix-acls/people/charly/c
 # owner: charly
 # group: ag-foo
user::rw-
group::---
group:ag-foo:r-x		#effective:r--
mask::r--
other::---

root@samba:/samba# getfacl testdata-disable-chmod/people/charly
 # file: testdata-disable-chmod/people/charly
 # owner: charly
 # group: ag-foo
 # flags: -s-
user::rwx
group::---
group:ag-foo:r-x
mask::r-x
other::---
default:user::rwx
default:group::---
default:group:ag-foo:r-x
default:mask::r-x
default:other::---

root@samba:/samba# getfacl testdata-disable-chmod/people/charly/c
 # file: testdata-disable-chmod/people/charly/c
 # owner: charly
 # group: ag-foo
user::rw-
group::---
group:ag-foo:r-x		#effective:r--
mask::r--
other::---
```
