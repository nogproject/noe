# NOE-7 -- Object Storage with User Permissions
By Marc Osterland
<!--@@VERSIONINC@@-->

## Status

Status: retired, v0, 2017-04-05

NOE-7 has been retired, because the initial ideas were not pursued further.

See [CHANGELOG](#changelog) at end of document.

## Summary

This NOE describes a potential design for an S3 object
storage with user permissions.

## Motivation

Usually S3 object storages are accessed via an URL like
`https://<endpoint>/<bucket>/<path-to-file>`. Authentication
happens by signing the URL with secret keys. The files are
accessed by the user which started the service. This assumes
read and write access for this user on the respective path.
Here an approach is presented, which allows access to a
path or filesystem as any given user by extending the URL
to `https://<endpoint>/<user>/<path-to-file>`.

## Assumptions

To realize this approach, following assumptions are made:

- User Authentication: The remote service, which uses the
  object storage, is in charge of verifying the user.

The design is intended to run in Docker containers.  Thus a working setup with
Docker and Docker Compose is required.  In theory, the design can be used
without Docker.  In this case, the containers described below can be seen as
service groups.  Furthermore, a set of users with access to a common filesystem
is necessary.

## Design

### Declarations

user source: The user source can be a simple list of users or
an authentication service such as LDAP or Dex. It provides
the necessary information for the controller.

controller: The controller is a container, which gets the
users from a user source. The controller creates the necessary
information and configuration for the other services.

s3-swarm: The s3-swarm is a container that runs many instances
of an object storage service (e.g. Minio).

proxy: The proxy is a container that runs a reverse proxy
service (e.g. HAproxy).

### Design Overview

The controller gets the users from a user source. It creates
a bijective mapping between usernames and port numbers.
The controller then creates configurations and startup scripts
for each user. These are used by the s3-swarm to create
individual object storage services for each user with its
assigned port number.
Based on the user-port mapping, the controller creates the
configuration for the proxy.

If changes in the user base are expected frequently, the
controller runs in an infinite loop and updates the
configurations. Subsequently it triggers a reload of the
services.

The effect of all this is, that the first part of the requested
URL is re-interpreted as username instead of bucket. The proxy
applies a rule based on the username, which will forward the
request to the user's object storage.

### Controller

The main task of the controller is the user-port mapping and the
configuration maintainance. For the functionality of the system
a the initial user-port mapping can be arbitrary. However, in
order to ensure an uninterupted service, the mapping should be
designed carefully. If the controller crashes, it should reload
an existing mapping. If the controller runs in an infinite loop,
new users should be added without reshuffling the mapping.

If the user base is very static, a simple text file might be
sufficient as user source. However, in real scenarios users
will be added or removed from time to time or frequently.
In this case, LDAP and Dex are recommended user sources. To
use these inside the controller container, the necessary
configurations need to be passed or mounted. If the controller
runs as a service on the Docker host, `getent group ...`
might work as a user source.

### Object storage

All individual object storage services use the same root directory.  It
contains links to the actual target filesystem, which is bind-mounted from the
Docker host.  The toplevel sub-directories indicate the user.  The structure
below needs to be determined during implementation; see section 'Mountpoints'
below.  Example:

```
/minio-root/
 |-bob/
 |  |-... --> /mnt/filesystem/...
 |-alice/
 |  |-... --> /mnt/filesystem/...
/mnt/filesystem
```

### Proxy

The reverse proxy service configuration contains a rule for every user-port
mapping.  The proxy can terminate HTTPS, or HTTPS is already terminated in
another ingress proxy layer.

### Implementation Details

See stdrepo `fuimages_multi-minio_2017-02` for an example implementation.

We use HAproxy as the reverse proxy, Minio as object storage service, Vault for
handling of secrets, and s6 as service supervisor. Alternatives that we
considered and rejected are documented below.

#### HAproxy

The HAproxy configuration defines a frontend as follows:

```
frontend s3
    bind *:80
    use_backend <user> if { path_beg /<user>/ }
```

There has to be a `use_backend` rule for every user.
Correspondingly, there is a backend definition for each user:

```
backend <user>
    server <user> localhost:<port>
```

#### Minio

The Minio services are recommended to be started with `su-exec` as
the respective user:

```
su-exec <uid>:<gid> minio server --address :<port> --config-dir <config_dir/user> <mount>
```

For service supervision, all containers share a common directory. This
directory contains the scan directories for s6. The HAproxy scan directory
contains a service directory for the HAproxy and service directory for
syslogd, which brings the HAproxy log messages to stdout.
The Minio scan directory contains a service dir for each user.

```
/service/
 |-haproxy/
    |-haproxy_srv/
    |-syslogd
 |-minio
    |-minio-user001
    |-minio-user002
    |-...
```

The secret keys can be passed as environment variable in the
`docker-compose.yml` or as parameter in the config file.

Currently, it seems like a restart is necessary for a key change and a key
rotation mechanism is not implemented yet.  If this is and remains true, key
rotation could be done by HAproxy.  A second Minio instance (per user) would be
started with the new keys and HAproxy would redirect requests based on the
access key that has been used to sign a URL.  We should also consider modifying
Minio to support two active keys at a time.

#### s6

The default maximum number of services is 500. But it can be specified
with `s6-svscan -c MAX`, where `MAX` is the number of services.

Although `s6-svscan` can be set to rescan the scan directory with the
paramter `-t ms`, `s6-svscanctl -a` should be used to trigger a rescan
in order to avoid race conditions after adding Minio services.


#### Controller

The controller should be able to recover after crashing. It needs to
save the user-port mapping in the config directory. The general cycle
should be implemented like this:

1. Check for user-port mapping
   - If present, load mapping
   - If not, generate mapping
2. Save mapping
3. Update Minio services
4. Update HAproxy config
5. Trigger s6 rescan of Minio services
6. Trigger HAproxy reload

A rule-based mapping would be preferred, since it could be stateless.  We could
not find an obvious robust rule for mapping users, since UIDs can be out of
port range.  A rule-base mapping seems possible for GIDs and could be used if
using one Minio per group instead of on per user.

#### Mountpoints

We're not all comfortable with mounting the BCP filesystem root. One
consideration is to link only single repositories to a mounting directory.
In principle, the path design can offer many possibilties. The details will be
decided during implementation.  Example:

```
/mount/
 |-alice/
 |   |-fs/    --> filesystem root
 |   |-repos/
 |   |   |-repo1/
 |   |   |   |-paths/  --> user-visible fs.
 |   |   |   |-lfs/    --> hidden Git LFS objects directory.
 |   |   |-repo2/
 |   |-sha1/
 |   |   |-ab/
 |   |   |-8e/
 |-chuck/
 |   |-fs/    --> filesystem root
 |   |-repos/
 |   |   |-repo3/
 |   |   |-repo4/
 |   |-sha1/
 |   |   |-1a/
 |   |   |-e8/
```

#### Performance

We've observed a RAM usage of approx. 5GB with 500 users.
We did not observe significant changes in memory consumption under load.
However, HAproxy showed CPU loads of 30%, when sending ~1000 requests
per second.

## Alternatives

### Proxy

Instead of HAproxy Nginx can be used. We've decided to use HAproxy
mostly because of homogeneity. A reverse proxy configuration with
Nginx might look like this:

```
location /<user>/ {
    proxy_pass http://localhost:<port>;
    proxy_set_header Host $host;
}
```

### Docker setup

Instead of running multiple instances of Minio in a single container, multiple
containers with single Minio instances could be launched.  This can produce
memory overhead.

The number of containers is limited by system resources as discussed in
<http://stackoverflow.com/a/37628520/1887896>.

### UNIX Domain Sockets

Unix domain sockets would probably be easier to manage than ports.  But Minio
currently does not support Unix domain sockets.

### One Minio per group

In a scenario with a huge number of users, it should be considered to
run one Minio instance per group. This can save resources. But it
makes the upload non-trivial. An 'Inbox' might be a solution.

### Running Minio as root

Obviously, one Minio instance startet as root user can simplify things.
We choose not to do so, because it makes the upload non-trivial and
it is very insecure.

## Example: BCP Filesystem

For the data management at BCP, we will user the Docker setup proposed in the
design section. We might consider having one Minio per group.  See also the
comments on mountpoints in the implementation section.

With per-group Minios, `bcpfs-perms describe groups` could be useful to
determine the relevant Unix groups.

## Example: ZIB

For access to a ZIB department filesystem, we will run the setup in an ITDS-VM.
We might consider running only one Minio for the Unix group of the department.
Otherwise access to LDAP is necessary in the controller.

## CHANGELOG

* 2019-10-28: retired
* v0, 2017-04-05: Initial complete design description
* 2017-03-31: Initial draft
* 2017-03-27: Started document
