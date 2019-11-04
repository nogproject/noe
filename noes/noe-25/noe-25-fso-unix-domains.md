# NOE-25 -- Tracking Unix Users and Groups in the Filesystem Observer Registry
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2019-07-02

2019-10-28: NOE-13 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

## Summary

This NOE describes a design to track Unix users and groups in the filesystem
observer registry.

The history of relevant users and groups is stored as a new aggregate *Unix
domain*, whose content is similar to the information that can be obtained by
`getent group` and `getent passwd`.  A new daemon *Nogfsodomd*, which runs on
the file server, synchronizes the Unix users and groups at regular intervals
from the file server to the registry.

The web application uses the filesystem observer registry to determine the
group membership of users if direct LDAP access cannot be used, for example due
to access restrictions.

## Motivation

Direct access from the web application to LDAP information was not immediately
possible due to general firewall rules and data access policy considerations.
Furthermore, in order to decide how to reassign data of former Unix users, we
would like to have a history of Unix users and groups.  We decided to track
a history of relevant Unix users and groups in the filesystem observer
registry.

## Design

See proof of concept in commits up to
`fuimages_nog_2018/nog@p/unix-domains@02b045072f8af6645e71b3d76318e33767d2e2ab`
2019-07-02 "Tie p/unix-domains: fix handling of registries without domains".

### Aggregate Unix domain

Information about Unix users and groups is stored in the new aggregate *Unix
domain*, which is independent of other aggregates, like registry or repos.

User and group information is accessed and updated through a gRPC service:

```
service UnixDomains {
    rpc CreateUnixDomain(CreateUnixDomainI) returns (CreateUnixDomainO);
    rpc GetUnixDomain(GetUnixDomainI) returns (GetUnixDomainO);
    rpc GetUnixUser(GetUnixUserI) returns (GetUnixUserO);

    rpc CreateUnixGroup(CreateUnixGroupI) returns (CreateUnixGroupO);
    rpc DeleteUnixGroup(DeleteUnixGroupI) returns (DeleteUnixGroupO);

    rpc CreateUnixUser(CreateUnixUserI) returns (CreateUnixUserO);
    rpc DeleteUnixUser(DeleteUnixUserI) returns (DeleteUnixUserO);

    rpc AddUnixGroupUser(AddUnixGroupUserI) returns (AddUnixGroupUserO);
    rpc RemoveUnixGroupUser(RemoveUnixGroupUserI) returns (RemoveUnixGroupUserO);

    rpc UnixDomainEvents(UnixDomainEventsI) returns (stream UnixDomainEventsO);
}
```

The aggregate events are:

```
// `UnixDomainEvent` is a subset of the full `nogevents.Event` message.
message UnixDomainEvent {
    enum Type {
        EV_UNSPECIFIED = 0;

        // reserved 220 to 230; // unixdomains
        EV_UNIX_DOMAIN_CREATED = 221;
        EV_UNIX_GROUP_CREATED = 222;
        EV_UNIX_USER_CREATED = 223;
        EV_UNIX_GROUP_USER_ADDED = 224;
        EV_UNIX_GROUP_USER_REMOVED = 225;
        EV_UNIX_USER_DELETED = 226;
        EV_UNIX_GROUP_DELETED = 227;
        EV_UNIX_DOMAIN_DELETED = 228;
    }
    ...
    // reserved 110 to 119; // unixdomains
    string unix_domain_name = 111;
    bytes unix_domain_id = 112;
    string unix_group = 113;
    uint32 unix_gid = 114;
    string unix_user = 115;
    uint32 unix_uid = 116;
}
```

Example query:

```
$ nogfsoctl get unix-domain EXDOM
domainId: cbf73e92-a17d-4ea1-93f7-94e7f444de6e
domainVid: 01DEES33RM99EV2BG7BYR025YH
domainName: EXDOM
users:
- {"user":"alice","uid":122,"gid":1001}
- {"user":"bob","uid":123,"gid":1002}
...
groups:
- {"group":"org_ag-alice","gid":1001,"uids":[102,103,122]}
- {"group":"org_ag-bob","gid":1002,"uids":[104,105,123]}
- {"group":"srv_spim-100","gid":1011}
...
```

Example events:

```
$ nogfsoctl events unix-domain EXDOM
{"event":"EV_UNIX_DOMAIN_CREATED","id":"01DECN54Z07KBJ9M1B7B1YK9VD","parent":"00000000000000000000000000","etime":"2019-06-27T14:24:14.304Z","domainName":"EXDOM"}
{"event":"EV_UNIX_GROUP_CREATED","id":"01DEEQ563VF37XFCRZSG8AQ0AM","parent":"01DEEGZHYB7S9CR50EB85ZD981","etime":"2019-06-28T09:37:41.499Z","group":"org_ag-alice","gid":1001}
...
{"event":"EV_UNIX_USER_CREATED","id":"01DEER0VGNMTWVKF70W2ZY16NH","parent":"01DEER0M8H7CNXDY8WBYPC85MA","etime":"2019-06-28T09:52:48.149Z","gid":1001,"user":"alice","uid":122}
...
{"event":"EV_UNIX_GROUP_USER_ADDED","id":"01DEERB1K0Y7FMYV62Q6JKPHYQ","parent":"01DEER0VH19CMNW5BVC4A56XA4","etime":"2019-06-28T09:58:22.048Z","gid":1013,"uid":122}
...
{"event":"EV_UNIX_GROUP_USER_REMOVED","id":"01DEERSSA8H0XR5NJT30QP7DDG","parent":"01DEERSATKSE03BV14DT8863NS","etime":"2019-06-28T10:06:25.096Z","gid":1022,"uid":124}
...
{"event":"EV_UNIX_GROUP_DELETED","id":"01DEES1WNXY9Y536NFVB997BBZ","parent":"01DEERTCV3HV824CKXPHZKYX6E","etime":"2019-06-28T10:10:50.685Z","gid":1}
...
```

### Daemon Nogfsodomd

A new daemon *Nogfsodomd* syncs information from the file server.  At regular
intervals, it compares the output of `getent group` and `getent passwd` with
the state of the Unix domain in the registry and updates the state if
necessary.  It selects the relevant groups from `getent group` based on a list
of group prefixes, like `org_` and `srv_`.  It then selects the relevant users
from `getent passwd`, keeping only users whose GID is in the selected groups.

Nogfsodomd can be deployed in a container, using the following bind mounts:

```
docker run \
    -v /var/run/nscd:/var/run/nscd:ro \
    -v /etc/nsswitch.conf:/etc/nsswitch.conf:ro \
    -v /etc/libnss-ldap.conf:/etc/libnss-ldap.conf:ro \
    -it --rm debian:jessie  \
    bash -c '
        apt-get update;
        DEBIAN_FRONTEND=noninteractive apt-get install -y libnss-ldap;
        getent group
    '
```

The deployment was tested with a Debian container on a Debian Docker host.  It
did not immediately work with an Ubuntu container on a Debian host.

### Web application

The web application uses gRPC `GetUnixUser()`, as a substitute for LDAP, to
retrieve the groups for a user.

The relevant web application settings are:

```
...
    fsoUnixDomains: [
      { # Instead of ldap: ...
        service: 'gitexample',
        domain: 'EXDOM',
      },
    ],
    registries: [
      {
        name: 'example',
        addr: 'nogfsoregd.example.org:7550',
        cert: '/example/ssl/certs/nogappd/combined.pem',
        ca: '/example/ssl/certs/nogappd/ca.pem',
        registries: ['exreg', 'exreg2'],
        domains: ['EXDOM'];
      }
    ],
...
```

## How we introduce this

We deploy the implementation to staging and soon after to production.

## Limitations

None.  The solution is a complete replacement for LDAP.

## Alternatives

We could use direct LDAP access for the web application and use the registry
Unix domain only for tracking the history of users and groups.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

How to use the history of users and groups to automate reassigning files of
former Unix users?

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v1, 2019-07-02: Initial version
