# NOE-11 -- Strategy for Organizational Unit Subdir ACLs
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2017-07-31

2019-10-28: NOE-11 contains ideas that are actively used on BCPFS.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-11 describes a strategy how to support different permission policies for
organizational unit subdirs, like `projects/`, `people/`, and `shared/`.

## Motivation

The directory names `projects/`, `people/`, and `shared/` suggest that they use
different permissions.  Data in `people/<me>` should be writable by me but only
readable by my research group.  Data in `projects/` should be readable and
writable by the research group.  `shared/` should be managed by root as
described in NOE-9.

## Design

Organizational unit subdirs can have permission policies that will be mapped to
appropriate ACLs by `bcpfs-perms`.

### Configuration syntax

The syntax of `bcpfs.hcl` is modified to support subdir permission policies.

Old:

```
orgUnit {
    name = "ag-prohaska"
    extraDirs = [
        "people",
        "projects",
        "shared",
    ]
}
```

New:

```
orgUnit {
    name = "ag-prohaska"
    subdirs = [
        {
            name = "people",
            policy = "owner",
        },
        {
            name = "projects",
            policy = "group",
        },
        {
            name = "shared",
            policy = "manager",
        },
    ]
}
```

The `subdirs` objects may contain additional parameters to customize the
policy.  This option is initially not used.

`extraDirs` is kept for backward compatibility, at least for a transition
period.  `extraDirs` are mapped to `{ name = "<dir>", policy = "group" }`.

### Initial policies

The following three policies are initially supported.

#### group

With policy `group`, the entire tree is writable by the organizational unit:

```
$ getfacl /ou_srv/data/ou/ag-foo/projects
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

#### owner

With policy `owner`, users can create subdirs that are writable by the owner
but only readable by the organizational unit:

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
default:group:ou_ag-foo:r-x
default:mask::r-x
default:other::---
```

#### manager

With policy `manager`, the organizational unit can read but not write:

```
$ getfacl /ou_srv/data/ou/ag-foo/people
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

## How we introduce this

We silently introduce policies and observe how they work in practice.

We need some support for per-subdir ACLs for NOE-9 sharing anyway.

## Limitations

The subdir ACLs need to be propagated if they are changed later.  The design
does not describe how this happens.  A naive approach could propagate them to
the entire subdir tree.  Sharing permissions would then need to be re-applied
right after.  For a short time window, ACLs would not be as expected.
A smarter implementation could modify only the ACL entries for which
`bcpfs-perms` is responsible.

## Alternatives

We could replace `bcpfs-perms` right away with a unified implementation of
sharing permissions.  But it seems too difficult, compared to the incremental
approach proposed here.

## Future work

The questions in this section seem relevant but will not be answered in this
NOE.  They are left for future work.

How to handle subdir and sharing permissions in a unified way?

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v1, 2017-07-31
* 2017-07-29: Initial version with three policies `owner`, `group`, `manager`
