# NOE-22 -- Filesystem Observer User Privilege Separation
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1.1.0, 2019-05-14

2019-10-28: NOE-13 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

## Summary

This NOE describes a design that uses one daemon per user to allow the
filesystem observer daemon Nogfsostad to perform operations on behalf of
a user.  It is an alternative to NOE-12, which describes a design with a single
daemon for root privilege separation.  The general filesystem observer
architecture is described in NOE-13.

See proof of concept implementation in supplementary material.

Related NOEs:

* [NOE-12](./../noe-12/noe-12-root-privilege-separation.md) -- Root Privilege
  Separation
* [NOE-13](./../noe-13/noe-13-git-fso.md) -- Git Filesystem Observer

## Motivation

We need a mechanism to perform operations on behalf of individual users, for
example create a new directory.  Following the principle of least privilege, it
seems desirable to do that without running a root daemon.

## Design

### Design summary

Nogfsostad executes operations on behalf of a specific user by calling a gRPC
server that runs as the specific user via a Unix domain socket.  The user
server is called Nogfsostaudod, where "udo" stands for "user do" or "sudo
without s".

There are two variants of the Nogfsostaudod user server:

* `nogfsostaudod-fd`: Nogfsostad uses Sudo to start Nogfsostaudod daemons as
  needed, passing them a file descriptor to one end of a `socketpair()` for
  communication.
* `nogfsostaudod-path`: Nogfsostad expects that Nogfsostaudod daemons are
  already running and listening on a Unix domain socket whose pathname includes
  the UID.  Access is mutually authorized by the client and the server using
  `SO_PEERCRED` as described in NOE-12.

There is an additional variant for root operations:

* `nogfsostasuod-fd` is like `nogfsostaudod-fd` but for root operations.  The
  separation into two programs can be used to completely disabled code paths in
  the root or non-root variants, which provides an additional layer of
  security.

If the host password database cannot be directly used in the Nogfsostad
container, Nogfsostad uses Sudo indirectly via a helper daemon Nogfsostasududod
that runs in a separate container.

The Sudo variant is usual used in a production setup.  The alternative path
variant may be useful for testing or in situations where Sudo cannot be used,
for example due to lack of admin privileges to configure Sudo.

### Sudo

Assuming `nogfsostad` runs as user `stad`, the following `sudoers`
configuration is required to run `nogfsostaudod-fd` as user `alice` as a direct
child process of `nogfsostad`, that is `sudo` execs `nogfsostaudod-fd`:

```
Defaults:stad closefrom_override, !pam_session, !pam_setcred
stad ALL=(alice) NOPASSWD: /usr/local/bin/nogfsostaudod-fd
```

* `closefrom_override` allows `sudo -C 4` to keep file descriptor 3 open.
* `!pam_session` and `!pam_setcred` allow `sudo` to exec the child command.

Similarly with Sudo group syntax, the following allows the service group `stad`
to run as users in the group `orgfs`:

```
Defaults:%stad closefrom_override, !pam_session, !pam_setcred
%stad ALL=(%orgfs) NOPASSWD: /usr/local/bin/nogfsostaudod-fd
```

### Docker host vs container password database

Sudo requires users to be present in the password database.  In order for
Nogfsostad to use Sudo to execute Nogfsostaudod as a different user, both the
Nogfsostad user and Nogfsostaudod user must be in the password database.

For production, all service users and groups would ideally be added to the
Docker host password database.  For testing, it seems obviously useful to use
only a container password database and add only selected users to it,
duplicating information from LDAP.  Even for production, it might be useful to
use a container password database for service users and the host password
database to access LDAP users.

If service users can be managed in LDAP, we use the host password database in
the container.  The host password database can be accessed from a container by
bind-mounting the Nscd socket, as described below.  With this setup, there is,
however, no obvious way to mix the host and the container password databases.
The Nogfsostad user and related service groups, therefore, must be present in
the host password database.

If the service users and groups cannot be managed in LDAP, for example due to
organizational account policies, two separate containers can be used instead.
Nogfsostad runs in a container without access to the host password database.
Instead of using Sudo directly to start `nogfsostaudod-fd`, it connect to
a helper daemon Nogfsostasududod via a Unix domain socket.  Nogfsostasududod
runs in a different container that has access to the host password database.
Nogfsostasududod uses Sudo to start `nogfsostaudod-fd` and passes a file
descriptor that is connected to `nogfsostaudod-fd` back to Nogfsostad.  See
separate section for details.

Example of accessing the Docker host password database from a container:
`getent` returns individual entries from the host password database when
bind-mounting the directory `/var/run/nscd` to allow the container to access
the host `/var/run/nscd/socket`:

```
docker run \
    -v /var/run/nscd:/var/run/nscd:ro \
    -it --rm debian:jessie getent user alice
```

But listing entries, for example with `getent passwd`, does not work.  See
NOE-12 for details.  Individual entry lookup seems sufficient for Sudo.

The bind mount uses the directory `/var/run/nscd`, and not the socket, in order
to continue working when the host Nscd creates a new socket after a restart.

### Nogfsostasududod

`nogfsostasududod` listens on the Unix domain socket `--sududo-socket` for
requests by `nogfsostad` to start `nogfsostaudod-fd` via Sudo.  If
`nogfsostad` asks to run the daemon as user `root`, `nogfsostasududod`
starts a special variant `nogfsostasuod-fd`.

Assuming `nogfsostasududod` runs as user `daemon`, for example, the
following `sudoers` configuration is required to run `nogfsostaudod-fd` as
a direct child process, that is `sudo` execs `nogfsostaudod-fd`, as user
`alice` or users in group `ag_bob`, for example:

```
Defaults:daemon closefrom_override, !pam_session, !pam_setcred
daemon ALL=(alice) NOPASSWD: /usr/local/bin/nogfsostaudod-fd
daemon ALL=(%ag_bob) NOPASSWD: /usr/local/bin/nogfsostaudod-fd
```

To run `nogfsostasuod-fd` as user `root`, the following additional
`sudoers` configuration is required:

```
daemon ALL=(root) NOPASSWD: /usr/local/bin/nogfsostasuod-fd
```

Access to the `nogfsostasududod` socket is restricted by file permissions on
the Unix domain socket path and, in addition, by checking `SO_PEERCRED` as
described in NOE-12.  The relevant command line options are:

```
$ nogfsostasududod --help
  --stad-uids=<uids>
        `nogfsostad` UIDs that are allowed to connect; comma-separated list.
  --stad-gids=<gids>
        `nogfsostad` GIDs that are allowed to connect; comma-separated list.
        If neither `--stad-uids` nor `--stad-gids` is specified, the access
        check is disabled.  If both `--stad-uids` and `--stad-gids` are
        specified, permission is granted if either the UID or the GID matches.
```

### Determining the Nogfsostaudod user

Unix users are passed in the JWT `xcrd` claim as a list of objects that contain
the Unix username, domain name, and groups.  For example user `alice@EXAMPLE`
with groups `foo, bar` is encoded as:

```
  "xcrd": [
    {
      "d": "EXAMPLE",
      "u": "alice",
      "g": ["foo", "bar"]
    }
  ],
```

If Nogfsostad wants to perform an operation on behalf of the request user, it
selects a specific domain from `xcrd` and uses `getpwnam(3)` to resolve the
UID.

Alternatively, Nogfsostad may determine a UID in a different way, for example
a privileged admin operation may explicitly specify a user.

Nogfsostad then either uses Sudo to start `nogfsostaudod-fd` for that UID or
connects to `nogfsostaudod-path` via a Unix socket pathname that encodes the
UID.  The relevant options are:

```
$ nogfsostad --help
  --jwt-unix-domain=<domain>
        The domain that is expected in a JWT `xcrd` claim.  If unset,
        services that require a local Unix user will be disabled.
  --udod-socket-dir=<dir>
        If set, `nogfsostad` connects to `nogfsostaudod-path' daemons via
        sockets in the specified directory, instead of starting
        `nogfsostaudod-fd` processes when needed.
  --sududod-socket=<path>
        If set, `nogfsostad` connects to `nogfsostasududod` at the
        specified `<path>` to start `nogfsostaudod-fd` processes when
        needed, instead of starting them directly via `sudo`.

$ nogfsostaudod-path --help
  --stad-socket-dir=<dir>
        Directory for the Unix socket that `nogfsostad` will connect to.
  --stad-users=<usernames>
        `nogfsostad` users that are allowed to connect; comma-separated list.
```

### Amortizing daemon startup cost

Nogfsostad keeps unused Nogfsostaudod processes and connections alive for
a configurable amount of time to amortize the startup cost.

See supplementary material Go package
`backend/internal/nogfsostad/privileges/daemons`.

## How we introduce this

We have deployed and tested the proof of concept from the supplementary
information.

We should soon add service users and groups to the host password database and
switch the staging setup to using the host password database.

We will add privileged operations as needed.

## Limitations

There are no obvious limitations.  It should be possible to implement all
desired operations with least privileges.  gRPC between Nogfsostad and
Nogfsostaudod requires some implementation effort for each privileged
operation; but the effort seems acceptable.

## Alternatives

Nogfsostad could run individual commands with Sudo.  Managing the Sudo config
and/or command wrappers may become tedious.  The overhead of executing separate
commands may noticeably slow down operations.

Nogfsostad could delegate work to a single server that runs as root, as
described in NOE-12, and performs privileged operations as root.  Performing
all privileged operations as root violates the principle of least privilege.

A server could run with saved set-user-ID root and temporarily change
per-thread UIDs as needed.  This approach could be used either with Nogfsostad
directly or by communicating with a privileged server.  Changing per-thread
UIDs is not obvious in Go.  It may be feasible with `runtime.LockOSThread()`.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

The individual privileged operations have not been determined.  They will be
added as needed.

## Supplementary information

* Proof of concept implementation
  `fuimages_nog_2017/nog@p/udo-proof-of-concept@8579a9eb2afb0938c3e8b0056638eec299c9aebf`
  2018-11-08 "Tie p/udo-proof-of-concept: udo as"

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v1.1.0, 2019-05-14
* 2019-05-14: New nogfsostaudod variant `nogfsostasuod-fd` for root operations
* 2019-05-14: Additional two-container setup with Nogfsostasududod to separate
  container password database for service accounts and host password database
  for user accounts
* v1.0.1, 2019-02-22: Nscd bind mount uses directory to allow Nscd restarts
* 2018-11-09: Minor polishing
* v1, 2018-11-08: Initial version
