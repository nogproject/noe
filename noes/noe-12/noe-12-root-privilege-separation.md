# NOE-12 -- Root Privilege Separation
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1.1.1, 2019-11-01

2019-10-28: NOE-12 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

See alternative in:

* [NOE-22](./../noe-22/noe-22-fso-udo.md) -- Filesystem Observer User Privilege
  Separation

## Summary

NOE-12 describes a root daemon for privilege separation.

## Motivation

We need a mechanism for services to execute some operations with root
privileges.

## Design

Privileged operations are executed by a GRPC server that runs as root and
listens on a Unix domain socket.

Client access is restricted in the following way:

* The client needs access to the Unix domain socket.
* The server uses `getsockopt(SO_PEERCRED)` to retrieve the client `ucred`,
  which contains pid, uid, and gid of the connecting process.
* Connections are authorized based on uid and gid.
* Individual operations are authorized based on uid and gid.

The root server is either deployed as a Docker container or as a Systemd
service.  We prefer a Docker container with privilege restrictions using
capabilities, bind-mounts, and seccomp-bpf unless it turns out to be extremely
difficult to maintain.  See Docker examples below for potential pitfalls.

The root server does not directly listen on the Internet.  If remote access is
needed, a separate non-root server will listen on the Internet.  The non-root
server handles most of the work, such as TLS termination, token-based auth, and
input validation.  It calls the root server only for privileged operations.

The pid is not used for authorization, since it is meaningless in containers
that use a pid namespace; see man `pid_namespaces(7)`.  Containers must use the
host user namespace to avoid uid and gid translation; see man
`user_namespaces(7)`.

We postpone using seccomp until Docker by default supports seccomp for the host
Linux distribution.  Docker by default does not support seccomp on Debian
Jessie.  See <https://docs.docker.com/engine/security/seccomp/>.

### Example server code

Go GRPC `TransportCredentials` that uses `SO_PEERCRED` to store `ucred` as peer
info during the `ServerHandshake`:

```go
// `UcredAuthInfo.Ucred` is a field, so that `UcredAuthInfo` could be extended
// with other information, such as TLS or IP address.
//
// The real implementation may user a shorter type name.
type UcredAuthInfo struct {
    Ucred syscall.Ucred
}

func (UcredAuthInfo) AuthType() string {
    return "ucred"
}

func (creds *SoPeerCred) ServerHandshake(
    conn net.Conn,
) (net.Conn, credentials.AuthInfo, error) {
    uconn, ok := conn.(*net.UnixConn)
    if !ok {
        return nil, nil, fmt.Errorf("not a Unix connection")
    }

    fp, err := uconn.File()
    if err != nil {
        return nil, nil, fmt.Errorf("failed to get fd: %s", err)
    }
    defer fp.Close()

    cred, err := syscall.GetsockoptUcred(
        int(fp.Fd()), syscall.SOL_SOCKET, syscall.SO_PEERCRED,
    )
    if err != nil {
        return nil, nil, fmt.Errorf("failed to SO_PEERCRED: %s", err)
    }

    if conn not authorized {
        return nil, nil, error...
    }

    auth := UcredAuthInfo{Ucred: *cred}
    return conn, auth, nil
}
```

Go code that authorizes a GRPC operation based on the `AuthInfo` stored above:

```go
func InfoFromContext(ctx context.Context) (*UcredAuthInfo, bool) {
    pr, ok := peer.FromContext(ctx)
    if !ok {
        return nil, false
    }
    info, ok := pr.AuthInfo.(UcredAuthInfo)
    if !ok {
        return nil, false
    }
    return &info, true
}

type UidAuthorizer struct {
    uids map[uint32]bool
}

func (a *UidAuthorizer) Authorize(ctx context.Context) error {
    info, ok := InfoFromContext(ctx)
    if !ok {
        return ErrMissingAuth
    }
    if !a.uids[info.Ucred.Uid] {
        return ErrDenyUid
    }
    return nil
}

func (s *Server) Op(
    ctx xcontext.Context, request *pb.OpRequest,
) (*pb.OpResponse, error) {
    if err := s.authorizer.Authorize(ctx); err != nil {
        return nil, err
    }
    // ...
}
```

### Docker examples

Individual `getent group x` requests resolve as on the host when bind-mounting
the directory `/var/run/nscd` to allow the container to access the host
`/var/run/nscd/socket`:

```
docker run \
    -v /var/run/nscd:/var/run/nscd/:ro \
    -it --rm debian:jessie getent group bcp_ag-prohaska
```

Glibc automatically uses the socket:

```
$ strace getent group bcp_ag-prohaska
...
connect(3, {sa_family=AF_LOCAL, sun_path="/var/run/nscd/socket"}, 110) = 0
...
```

But listing groups does not work:

```
docker run \
    -v /var/run/nscd:/var/run/nscd:ro \
    -it --rm debian:jessie getent group
```

Listing seems to take a different code path that requires a full LDAP config:

```
host$ strace getent group
...
open("/etc/libnss-ldap.conf", O_RDONLY) = 5
...
```

Listing works with additional bind mounts as follows:

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

## How we introduce this

We implement the initial root server for a simple quota service that allows
selected manager accounts to query and modify quota without sudo permission.
The intention is not to create a replacement for sudo.  But we want a simple
initial use case that already creates some practical value.

We add root operations as needed when implementing non-root servers.

## Limitations

The concept is flexible without obvious limitations.

## Alternatives

### sudo

Sudo could be used to control privileged operations.  Possible drawbacks:

* It requires more coordination between IT managers.
* We probably would have to invent ad-hoc protocols between the client and the
  sudo commands.
* Probably many more subprocesses, which could have a negative performance
  impact.

### Abstract Unix domain socket

The root server could listen on an abstract socket address instead of
a pathname; see man unix(7).  It would avoid a bit of path ceremony.  An
abstract socket, however, lives in the network namespace; see, for example,
<https://github.com/moby/moby/issues/14767>.  Access to a pathname is easier to
control for multiple clients.  The socket can be bind-mounted into containers.
Controlling the abstract socket, on the other hand, would require that all
containers use the same network namespace.

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* v1.1.1, 2019-11-01: polishing
* 2019-10-28: frozen
* v1.1.0, 2019-06-27: Description how to `getent group`
* v1.0.4, 2019-02-22: Nscd bind mount uses directory to allow Nscd restarts.
* 2018-11-08: Link to NOE-22, keeping status line
* v1.0.3, 2017-08-05: We will not use Docker seccomp on Debian Jessie.
* v1.0.2, 2017-08-01: Translation to Docker pid namespace
* v1.0.1, 2017-08-01: Fixed missing defer in Go example
* v1, 2017-08-01: Complete initial design
