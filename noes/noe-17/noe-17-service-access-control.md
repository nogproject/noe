# NOE-17 -- BCPFS access rules: Service-OrgUnits directories
By Ulrike Homberg
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2018-03-15

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-17 describes how to support two ways of controlling access to
organizational unit subdirs of services:

* Controlled by facility and organizational unit
* Controlled by only the organizational unit

## Motivation

As described in NOE-2 and NOE-10, access to organizational unit subdirs of
services like `<device>/ag-<orgunit>` is controlled in two steps. The facility
manager as well as the organizational unit manager have to permit access for
each user by adding them to the `srv_<device>` Unix group of the `<device>` dir
and `org_ag-<orgunit>` Unix group of the `ag-<orgunit>` dir, respectively.

Some facilities operate for a lot of research groups and users and would like
to simplify the access control in a one-way direction, so that the permissions
are only controlled by the organizational unit.

## Design

The service dirs can have restricted permissions to control the access to the
organizational unit subdirs per device or service. Or, they can have more
general permissions that allow members of all organizational units to traverse
the service dirs, while the final access to the organizational unit subdirs
stays controlled by the corresponding Unix groups.  (Access for Operators is
controlled by the facility via the service ops Unix groups.)

The permissions of the organizational unit subdirs will be kept as before.
All permissions will then be mapped to ACLs by `bcpfs-perms`.

A facility must only use one of the two access policies for all its services.

For a first implementation, see
`fuimages_bcpfs_2017/bcpfs/p/bcpfs-perms-service-access-policy@e9e6226c19c89b5cbc916b78255484c730f88e0f`.

The first polished implementation is `bcpfs-perms-1.2.0`.

### Access restricted per service

Per-service permissions `perService` is the 2-step access control, which have
been introduced in NOE-2 and NOE-10.

Access for organizational unit members is controlled by the facility via the
Unix group of the service in a first step:

```
# file: /srv/device1
# owner: root
# group: srv_device1
user::rwx
group::---
group:srv_device1:r-x
group:srv_ms-ops:r-x
mask::r-x
other::---
default:user::rwx
default:group::---
default:group:srv_device1:r-x
default:group:srv_ms-ops:r-x
default:mask::r-x
default:other::---
```

and by the organizational unit via its Unix group:

```
# file: /srv/device1/ag-foo
# owner: root
# group: org_ag-foo
# flags: -s-
user::rwx
group::---
group:org_ag-foo:rwx
group:srv_ms-ops:rwx
mask::rwx
other::---
default:user::rwx
default:group::---
default:group:org_ag-foo:rwx
default:group:srv_ms-ops:rwx
default:mask::rwx
default:other::---
```


### Access for all orgUnits

The `allOrgUnits` strategy relies on the access control by the particular
organizational unit managements.  It requires the existence of a super group
`ag_org` that contains all members of all organizational units.  The access to
the service dir `<device>` is then opened to all organizational units:

```
# file: /srv/device1
# owner: 0
# group: srv_ms-data
user::rwx
group::---
group:ag_org:r-x
mask::r-x
other::---
default:user::rwx
default:group::---
default:group:ag_org:r-x
default:mask::r-x
default:other::---
```

The permissions of organizational unit subdir will be set the same way as in
the `perService` strategy, see above.

### Configuration

The super group must be configured in `bcpfs.hcl`.

```
superGroup = "ag_org"
```

Each facility must be configured with its access policy in the field `access`,
which is either set to `perService` or `allOrgUnits`.

```
facility {
    name = "ms"
    services = [
        "device1",
        "device2",
    ]
    access = "perService"
}

facility {
    name = "lm"
    services = [
        "mic1",
    ]
    access = "allOrgUnits"
}
```

## How we introduce this

The `perService` policy is the current default policy and will not affect most
of the facilities.  We coordinate the change from `perService` policy to
`allOrgUnits` policy together with the corresponding facility.

## Limitations

We introduce the `allOrgUnits` policy on request.  But we do not proactively
propose it, because it only relies on group management of the organizational
units alone without control through the facility.  We are unsure whether that
is compliant with DFG best practices.

We provide the policies only for an entire facility and not per device in order
to keep the maintainability of filesystem configuration as simple as possible.

The proposed approach sets ACLs for the toplevel directories depending on the
configured access policy.  The `bcpfs-propagate-toplevel-acls` script changes
ACLs of subdirs depending on the service-orgunit combinations.  The script is
unaffected by the changes proposed in this NOE.

## Alternatives

We could do nothing and ignore the request for simpler access control.
Facilities operating for many users of many groups would then have to tediously
manage every single user membership.

## CHANGELOG

* v1, 2018-03-15
* 2018-03-15: refer to implementation in `bcpfs-perms-1.2.0`
* 2018-03-02: Replace `perDevice` by `perService` according to the renaming in
  `bcpfs-perms`
* 2018-02-27: Initial version with policies `perDevice` and `allOrgUnits`
