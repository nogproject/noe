# NOE-13 -- Git Filesystem Observer
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v3.0.1, 2019-11-01

2019-10-28: NOE-13 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-13 describes a design to observe a filesystem using Git without tracking
the actual content.  Git is used to maintain a history of file stat
information, checksums, and metadata.  The actual file content is only
available at the original filesystem location.

Related NOEs:

* [NOE-4](./../noe-4/noe-4-alt-nog-storage-git.md) -- Alternative Nog Storage
  Backends, Specifically Git: NOE-4 describes a design to map Git history to
  Nog.  NOE-13 uses the general idea, but it is more limited.  It does not
  provide general Nog storage.
* [NOE-6](./../noe-6/noe-6-git-shadow-repo.md) -- Shadow git repository for BCP
  file system: NOE-6 addresses a similar question as NOE-13 but with a broader
  scope.  NOE-6, however, was work in progress without a specific design choice
  when NOE-13 was started.
* [NOE-18](./../noe-18/noe-18-fso-nesting.md) -- Git Filesystem Observer
  Nesting

## Motivation

We decided in fall 2016 to modify Nog to be useful with traditional
filesystems.  [NOE-2](./../noe-2/noe-2-filesystem-repos.md) mentions
repositories as a concept to organize data and permissions, but without
describing details.  [NOE-4](./../noe-4/noe-4-alt-nog-storage-git.md) to
[NOE-6](./../noe-6/noe-6-git-shadow-repo.md) describe building blocks towards
a general solution.  None of the NOEs, however, describes a solution that would
be immediately useful.

The goal of NOE-13 is to describe a limited solution that delivers some
practical value with traditional filesystem right away.

## Design

### High-level architecture

Several services work together to maintain a history of filesystem state and
related metadata.  The overall service is called filesystem observer or FSO,
with variants to denote the individual services.

The Git filesystem observer service maintains a history of stat and SHA
information for filesystem objects.  The service runs with direct access to the
filesystem.  Updates are triggered on demand or according to a time-based
schedule.  Different update periods can be used for stat and SHA.

The filesystem observer may push the Git history to a Git repo storage server
for long-term storage.  A UUID is assigned for each filesystem location and
used as the repo name in Git hosting services that do not support hierarchical
paths.  Examples are GitHub and GitLab, whose naming schemes are
`<owner>/<repo>` with a limited character set for `<repo>`.

The Git Nog GRPC service translates between Git history and the Nog web
application.  There are multiple alternatives how the service could be
implemented: it could run on a separate host and pull repos from the storage
server; it could use the GitLab or GitHub REST API; or it could run together
with the filesystem observer.  The initial implementation runs it in the same
server together with the filesystem observer.

Some of the principles for lifting Git history as described in NOE-4 are
applied, but in a simpler manner.  The Git to Nog translation provides
different levels of service:

* Read-only summaries like the number of files in a repo
* Storing metadata by writing Git commits to the repo storage server: Per-repo
  metadata or also per-path metadata
* Listing file trees with stat information
* Content for selected files, like a toplevel README

The individual services are coordinated through the FSO registry service.  The
registry maintains event-sourcing entities.  The registry also acts as
a reverse GRPC proxy for the filesystem observer servers, which need not have
a reachable IP address.  The filesystem observer servers open TCP connections
to the registry.  The GRPC roles of the TCP connection are reversed right after
establishing the connection, so that the registry is the GRPC client and the
filesystem observer is the GRPC server.  The Nog web application contacts the
registry, which selects the suitable filesystem observer connection and
forwards the call.

### Git filesystem observer

The Git filesystem observer is a shadow Git repo that contains a history of
stat and SHA information for an observed real directory.  Nothing is added to
the real directory.  The shadow Git repo is placed in a separate directory tree
that is hidden from normal users.  Users only see an traditional filesystem.

The Git filesystem observer maintains information about filesystem objects.
The information is partitioned into several structs that can be updated
independently and combined when needed.  The structs are: stat, sha, metadata.

The shadow Git repo furthermore contains the full content for selected files,
such as the toplevel `README.md`.

Stat and SHA information is stored as a line-based format that is valid YAML.
Strings are always quoted to avoid potential confusion with numbers.

<div class="alert alert-warning"><p>
See extended stat information for trees in
[NOE-18](./../noe-18/noe-18-fso-nesting.md) -- Git Filesystem Observer Nesting.
</p></div>

Per-directory stat example:

```
$ git show master-stat:.nogtree
name: "root"
mtime: 1499773041

$ git show master-stat:PSA/.nogtree
name: "PSA"
mtime: 1499773041
```

Per-file stat example:

```
$ git show master-stat:README.md
name: "README.md"
size: 160
mtime: 1499768794
```

Per-file SHA example:

```
$ git show master-sha:README.md
name: "README.md"
size: 160
sha1: "07a17249efe4495cef75539a755eaf6b3311fa04"
sha256: "bab4c3c82b54272ed585bd20adda364a9625806c48ee36dd888ce667b0925f7b"
```

Git is used with small custom Git filters to track stat and SHA information.
Each struct uses a separate index and branch, so that incremental updates
happen with the usual Git speed.

Preliminary tests confirmed that the stat Git filter can return early without
reading the full content from Git.  The main Git process will ignore the broken
pipe.  The situation for long running filter processes is unclear.  The
specification, see `git --help gitattributes`, describes that the child process
must read the entire content before responding.

See `git-fso` proof of concept for details.

`git fso init --observe <realdir>` initializes a filesystem observer shadow git
repo in the current directory, which must be empty.  It runs `git init` and
configures the repo to observe `<realdir>`.

Shadow repo branches:

* `master-stub`: Repo configuration; currently only `.gitignore`
* `master-stat`: Realfile stat struct
* `master-sha`: Realfile sha struct
* `master-meta`: Metadata
* `master-content`: Realfile content for selected files

`git fso stat` records the stat information for `<realdir>` and commits the
changes if there are any.

`git fso sha` records secure hashes for `<realdir>` and commits the changes if
there are any.

`git fso content` adds the content of selected `<realdir>` files and commits if
there are changes.

`git fso apply-stub` applies changes from `master-stub` to the stat and sha
branches.  Its primary purpose is to propagate `.gitignore` from `master-stub`.

#### Filesystem observer options

The SHAs could be updated in a separate shadow repo and then pushed to the main
shadow repo in order to support concurrent stat and SHA updates.

The trees for the struct partitions could be combined on a branch that contains
a struct of trees.  This struct-of-trees branch can be used by the Git Nog GRPC
server as a single branch that combines all information.

Additional branches that might be useful:

* `master-sot`: Struct of trees `stat/`, `sha/`, ...
* `master-tos`: Tree of structs `x.dat.__stat`, `x.dat.__sha`, ...

`git fso sot` updates the struct of trees branch if the stat or sha branch have
changes.  This is a history-only operation, which does not look at the
`<realdir>`.

`git fso tos` updates the tree of structs branch if the stat or sha branch have
changes.  This is a history-only operation, which does not look at the
`<realdir>`.

Instead of branches, FSO could use a different Git ref namespace, like
`refs/fso/...`, in order to hide FSO from normal Git operations.  But we do
want easy access to FSO information, for example in the GitLab UI.  So we use
branches, at least initially.

#### Rejected filesystem observer alternatives

A different naming scheme could be used for the FSO branches, like
`<base-branch>.<detail>`.  Using dot instead of dash might be clearer if the
base branch name contains dashes itself.  But the advantage is not universal,
since a base branch name could also contain dots.  We explicitly decided
against using slash like `<base-branch>/<detail>`, because it would prevent
using `<base-branch>` at the same time.  If we want an unambiguous naming
convention, we should revisit the idea of a separate Git ref namespace
`refs/fso/...`.

### Shadow repo filesystem layout

<div class="alert alert-warning"><p>
See layout that supports repo nesting in
[NOE-18](./../noe-18/noe-18-fso-nesting.md) -- Git Filesystem Observer Nesting.
</p></div>

The shadow tree mimics the real tree.

```
realdir: /usr/local/bin
shadow: /nogfso/shadow/usr/local/bin
```

Similar filesystem permissions conventions can be used for the shadow tree as
for the real tree.

Alternative: We could use a UUID-based naming scheme for the shadow tree.
A potential advantage is that directories that are nested in the real tree are
not nested in the shadow tree, which might allow us to handle nested
repositories, for example a Git repo and its Git submodules.  But filesystem
permissions for the shadow tree are less obvious.  A mixed approach might be
useful.  It could use hierarchical naming close to the top and UUID-based
naming close to the leafs.

### Git storage repo naming

Git services such as GitHub and GitLab use naming conventions that restrict
permitted repo names.  Hierarchical filesystem paths, therefore, cannot be used
directly as repo names.

The preferred option is to associate a unique id with every repo and use the id
as the Git storage repo name:

```
/example/data/projects/foo/  ->  <user>/<uuid>
```

Alternative: File paths could be mangled to create valid repo name.  Every
slash would be encoded as underscore-dot-underscore, `/ -> _._`, with a root
slash but without trailing directory slash.  Example:

```
/example/data/projects/foo/  ->  <user>/_._example_._data_._projects_._foo
```

The id-based solution seems more robust.

### Git Nog GRPC service

See supplementary information `fuimages_nog_2016_*` for a proof of concept that
uses the GitLab REST API to provide repo summary information and store
metadata.  Branch `fuimages_nog_2016/next` contains an implementation that uses
Git with direct filesystem acess to implement the same service.

The service uses separate branches `master-stat`, `master-sha`,
`master-content`, and `master-meta`.  It combines the branches to provide
a unified view to the outside:

```
$ nogfsoctl gitnog head 270e4d8f-2d4b-4acb-9f05-6fe0d5cc05b9
repo: "270e4d8f-2d4b-4acb-9f05-6fe0d5cc05b9"
gitNogCommit: "6993801852385cca71c85b913e21a881266d6db86b8adfa16d8fd66218b819c4"
statGitCommit: "b9d3c2a02da700a78e2404a04ecc6adccccac087"
shaGitCommit: "6cb3fd86591e42a2752ef7881344a177d30f1211"
metaGitCommit: "18c3d2f787ca8290da34846240a662f750079615"
contentGitCommit: "6cb3fd86591e42a2752ef7881344a177d30f1211"
statAuthor: {"name":"alovelace","email":"homberg@zib.de","date":"2017-11-19T16:54:51Z"}
statCommitter: {"name":"nogfsoregd","email":"nogfsoregd@sys.nogproject.io","date":"2017-11-19T16:54:51Z"}
shaAuthor: {"name":"A U Thor","email":"author@example.com","date":"2017-11-15T11:26:57Z"}
shaCommitter: {"name":"nogfsoregd","email":"nogfsoregd@sys.nogproject.io","date":"2017-11-15T11:26:57Z"}
metaAuthor: {"name":"a","email":"b@c","date":"2017-11-15T17:15:21Z"}
metaCommitter: {"name":"Administrator","email":"admin@example.com","date":"2017-11-15T17:15:21Z"}
contentAuthor: {"name":"A U Thor","email":"author@example.com","date":"2017-11-15T11:26:57Z"}
contentCommitter: {"name":"nogfsoregd","email":"nogfsoregd@sys.nogproject.io","date":"2017-11-15T11:26:57Z"}
```

`gitNogCommit` is the SHA512/256 of a Protobuf message that contains the
individual Git commits:

```
message HeadGitCommits {
    bytes stat = 1; // 20 bytes
    bytes sha = 2;
    bytes meta = 3;
    bytes content = 4;
}
```

The individual Git commits can be used to retrieve details using the Git Nog
tree service; see section below.

Alternative: See supplementary information `nog-store-fso_2017-08` for a proof
of concept that uses ideas from NOE-4 directly.  The proof of concept lifts Git
commits, tree, and blobs to corresponding Nog entries.  It combines stat and
SHA information from multiple Git trees, so that they appear as metadata on
a single Nog entry.

### Git Nog tree GRPC service

The Git Nog tree service provides information for individual paths in a repo.
It is implemented with direct filesystem access to Git using libgit2/git2go.
See branch `fuimages_nog_2016/next` for a proof of concept.

The Git Nog tree service provides operations similar to `git ls-tree`, but with
additional details.  It takes a different approach than NOE-4: it does not hide
the Git shadow details behind lifted Nog entries.  It instead exposes the Git
shadow details directly.  Clients need to be aware of some of the details how
the information is stored as Git data structures.

A client can retrieve the `HeadGitCommits` and then list the Git shadow stat
tree for a specific Git stat commit.  The Git Nog tree service translates the
Git stat tree to per-path stat information.  Example:

```
$ nogfsoctl ls-stat-tree <repo> <git-commit>
d 0 2017-11-18T14:08:58Z  .
f 2 2017-11-17T17:20:43Z  bar
l 0 1970-01-01T00:00:00Z  bar.lnk   -> bar
l 0 1970-01-01T00:00:00Z  bar.lnk22222   -> bar
d 0 2017-11-17T16:08:52Z  sub/
f 0 2017-11-17T16:08:52Z  sub/foo
g 0 2017-11-18T14:10:08Z  submodule   -> 1955a66758eebd158c6644568a5635b416d46486
```

The Git Nog tree service provides per-path metadata in a similar way.

Listing entire trees can be slow for large trees.  If we observe practical
performance problems, we will introduce mechanism to limit listing.  Obvious
mechanisms are limiting by depth, limiting by path, or pagination.

### Git Nog Metadata GRPC service

Metadata is stored as a separate branch `master-meta`.  Metadata is encoded as
YAML of the following format:

```
<string-identifier-key>: <JSON-value>
...
```

Keys are sorted.  Each key is stored on a separate line.  JSON values are
encoded as UTF-8 without whitespace.  Example:

```
foo: "bar"
baz: 42
arr: ["a","b","c"]
obj: {"a":1}
```

See supplementary information `fuimages_nog_2016_*` for a proof of concept in
two variants:

* metadata via GitLab REST API
* metadata in the shadow repo, without GitLab

The preferred approach is direct write access to the shadow repo.  See details
in the sections 'branch update responsibilities' and 'programs'.

Metadata writes can be conditioned on the old commit to protect against
concurrent updates.  The condition can either use the `gitNogCommit`, to
protect against concurrent updates of any shadow branch, or the
`metaGitCommit`, to limit protection to concurrent metadata updates but allow
concurrent stat updates, for example.

Metadata can be stored for files and directories at any path.  The API allows
modifying metadata for multiple paths in an atomic operation that creates
a single Git commit.  Metadata can be stored even if there is no corresponding
stat information.  This might be useful to prepare data acquisition before the
actual data exists.

Option: Metadata could be added as a toplevel tree `meta/` to `master-sot`.

Alternative: Metadata could be encoded as JSON.  It would be harder to read for
humans, but might be easier to use in programs.

### Branch update responsibilities

The responsibilities for branches must be separated to avoid merge conflicts or
cause only trivial conflicts that are automatically handled.  Avoiding merge
conflicts is the preferred solution.

Example (no conflicts): Only the filesystem observer pushes the stat, SHA, and
content branches.  Only the Git Nog metadata service writes to the meta branch.

Example (trivial conflict resolution): The filesystem observer and the metadata
service both update the struct-of-trees branch.  Both implement trivial
conflict resolution and pull-push retry to the Git storage server.

The initial implementation performs all writes through `gitnogstad` (see
section programs below) using direct access to the primary filesystem observer
shadow repo.  Changes are then pushed to Git storage if configured.

Note: The proof of concept as of 2017-11-20 on branch `fuimages_nog_2016/next`
implemented a different strategy.  Metadata writes used a separate Git Nog
service if Gitlab storage was configured and `gitnogstad` otherwise.  This
conditional write routing has been removed.

Alternative: A separate Git Nog service could provide read access based on the
information pushed to a separate Git storage, for example GitLab.  A separate
service could be useful to ensure availability if the primary filesystem
observer is down.  It could also be used to reduce load on the primary
filesystem observer, which may help to manage concurrency or improve
scalability.  Examples: The primary filesystem observer may lock the repo while
computing SHAs; read access would still be possible through the separate
servie.  The separate service would be directly accessible, avoiding reverse
GRPC proxying through `nogfsoregd` to `nogfsostad`.

### Registry service

The list of filesystem locations is maintained by a registry service.  The
service is partitioned by host and root path.  Several root paths can be
grouped into the same partition.

Filesystem locations can only be registered below known roots.  The registry
enforces invariants, for example that repos are not nested.

Each registry partition publishes a stream of events that can be observed by
services to trigger actions, such as initializing resources in an eventually
consistent way.

The registry service uses event sourcing.  Each registry partition is an
aggregate.  Event sourcing is implemented using Go and MongoDB.  The events can
be retrieved and watched as a stream GRPC.  We implement event-sourcing using
MongoDB as the event store without event-sourcing framework.  See details in
separate section.

Some entities are externally identified by name, some by UUID.  Internally, all
entities are identified by UUID:

 - main: An internal entity that contains the primary system configuration,
   currently the list of registries
 - registry: Identified by name
 - root: Part of a registry, identified by registry name and path
 - repo: Identified by UUID

Although the registry state will likely be considered permanent in practice, it
should in principle be possible to recreate a useful registry from scratch
using only the Git filesystem observer state.

If a repo UUID is not of persistent value, a new UUID could be assigned when
importing an existing shadow repo.  Otherwise, the old UUID must be recreated
during the import.  Examples: A shadow-only Git repo could be assigned a new
UUID.  But a Git repo that uses the UUID as its name in GitLab must be restored
with the same UUID.  It seems more reliable to store the UUID in the shadow
repo and always restore it when importing an existing shadow repo.

The shadow repo UUID is stored in a special file in `.git/fso/`.  An
alternative would be to store it on a special Git branch.  Storing it only in
the filesystem ties the UUID to the filesystem location.  The Git repo could be
forked to a new filesystem location with a new UUID without touching the Git
history.

Recreating a registry from scratch may be a difficult and relatively expensive
operation.  After recreating a registry, all caches and read models must be
reset.  For example, the Nog webapp FSO state must be deleted and rebuilt,
which may cause user-perceivable downtime.

Example:

```
$ nogfsoctl get registries
main: main
vid: 01BVPBSTXBTYTE81ZAHR4JQS43
registries:
- {"name":"exreg","confirmed":true}

$ nogfsoctl get roots exreg
registry: exreg
vid: 01BVRE7EPR27FGAVJN9VJEQFCQ
roots:
- {"globalRoot":"/example/files","host":"files.example.com","hostRoot":"/usr/local","gitlabNamespace":"localhost/root"}

$ nogfsoctl get repos exreg
registry: exreg
vid: 01BVRE7EPR27FGAVJN9VJEQFCQ
repos:
- {"id":"b1c5b2c7-a736-4dd1-b2af-0390bc3474ac","globalPath":"/example/files/bin","confirmed":true}
- {"id":"611e61f5-bfc9-40fe-bda8-da3d0fca03a6","globalPath":"/example/files/share","confirmed":true}

$ nogfsoctl get repo b1c5b2c7-a736-4dd1-b2af-0390bc3474ac
{
  "repo": "b1c5b2c7-a736-4dd1-b2af-0390bc3474ac",
  "vid": "01BVPBSXC426GD4ZAX9YKV3Y7V",
  "globalPath": "/example/files/bin",
  "file": "files.example.com:/usr/local/bin",
  "shadow": "files.example.com:/nogfso/shadow/usr/local/bin",
  "gitlab": "localhost:root/_._example_._files_._bin",
  "gitlabProjectId": 17
}
```

### Discovery service

The discovery service allows users to find untracked repo candidates, which
should then be added to the registry.

Each root has a separate configuration that determines how to locate repo
candidates.  The configuration consists of a naming rule, like `Stdtools2017`,
and optional config parameters.  The rule selects a codepath in `nogfsostad`
that inspects the filesystem and returns untracked repo candidates and ignored
paths.

The UI presents candidate lists to the user, who can then add repos to the
registry.

### Storage flexibility

There is some flexibility where to store the Git history:

Shadow and GitLab: Stat and SHA is tracked and pushed to GitLab.  Metadata is
also written to the shadow repo and pushed to GitLab.  This setup is a good
candidate for reliable long-term storage.  The shadow repo and the GitLab repo
are both self-contained and have the same information.

Shadow only, no GitLab: Stat and SHA is tracked but not pushed to GitLab.
Metadata is stored in the shadow repo.  The shadow repo is self-contained, but
there is no replication.  This setup may be useful when tracking local scratch
directories that have limited long-term value or to avoid the dependency on
GitLab.

Shadow only could be combined with a backup cronjob that synchronizes repos to
a different filesystem server for redundancy.  This would provide redundancy
for the Git history without adding a dependency on GitLab.

The storage choice may be configured per root or per repo.  Further details
will be decided as part of the implementation.

### Programs

Services are grouped into programs that communicate via GRPC.  We use separate
programs if distribution obviously makes sense.  But we uses as few programs as
reasonable, grouping several subsystems into a single program.  If subsystems
run in the same process, they use direct in-process calls without GRPC.  If
subsystems run in different processes, they use GRPC.

`nogfsoregd`:

* Registry and event store: It should not run on the file server, because it
  must be accessible from all other services, including Meteor.
* Reverse GRPC proxying to `nogfsostad`

`nogfsostad`:

* Observes registry, inits fso git shadow repos, inits GitLab repos, runs Git
  to observe filesystem, pushes to GitLab.  It obviously must run on the file
  server.  The file server must have access to push to GitLab, so it can as
  well initialize GitLab.
* Git Nog service, including metadata writes, so that the shadow repo is always
  self-contained; pushes to GitLab after each update
* variant: Deferred push to GitLab.  The shadow repo is self-contained.  We may
  tolerate some delay until updates propagate to GitLab.
* variant: Without push to GitLab

`nogfsog2nd` (not part of the preferred variant):

* No separate program.  `nogfsostad` provides a full Git Nog service with
  self-contained shadow repos, accessible through reverse GRPC via
  `nogfsoregd`.
* variant: Uses GitLab REST to provide Git Nog read service
* variant: Writes metadata to Gitlab via REST API
* variant: Clones from GitLab, Git to Nog lifting server, metadata to Git

`nogfsoctl`:

* Control command

Server naming convention `nogxxxyyyzzz...d`, where `xxx`, `yyy`, ... are
3 letter codes from general to specific; here `nog fso yyy d`, with `yyy=reg`
for registry, `yyy=sta` for stat tracking, `yyy=g2n` for Git to Nog.  Related
control command `nog fso ctl`.

See proof of concept in `fuimages_nog_2016/backend`, branch `p/fso`; also on
branch `next` and packed as supplementary information
`fuimages_nog_2016_backend.tar.bz2`.

### Access control

#### Access control in the Meteor app

The Meteor app uses `nog-access` statements to limit access to FSO resources
based on path matching.  Access checks pass an `options` object that contains
at least `path`, whose meaning depends on the access action.  The access
statements use `path` to conditionally grant permission.

Examples for access actions:

Discovering untracked repos:

* `fso/discover` aka `AA_FSO_DISCOVER`: `path` is a prefix of FSO roots that
  may be listed.  Individual roots are only listed if `fso/discover-root` is
  also allowed.
* `fso/discover-root` aka `AA_FSO_DISCOVER_ROOT`: `path` is an FSO root with
  trailing slash.  The action enables listing the root and all repos below.
* `fso/init-repo` aka `AA_FSO_INIT_REPO`: `path` is a repo path without
  trailing slash.  The repo may be initialized.

Listing repos:

* `fso/list-repos` aka `AA_FSO_LIST_REPOS`: `path` is a directory, like
  a filesystem directory.  A single directory level may be listed.
* `fso/list-repos-recursive` aka `AA_FSO_LIST_REPOS_RECURSIVE`: `path` is the
  same as in `fso/list-repos`, but all repos below may be listed recursively.

Accessing individual repos:

* `fso/read-repo` aka `AA_FSO_READ_REPO`: `path` is the FSO repo path.  The
  repo may be viewed.
* `fso/read-repo-tree` aka `AA_FSO_READ_REPO_TREE`: `path` is the FSO repo
  path.  The repo tree, i.e. stat and metadata for individual paths, may be
  viewed.
* `fso/refresh-repo` aka `AA_FSO_REFRESH_REPO`: `path` is the FSO repo path.
  `nogfsostad` may be told to refresh.
* `fso/write-repo` aka `AA_FSO_WRITE_REPO`: `path` is the FSO repo path.
  Metadata may be written.

Access statements are specified in the Meteor settings using predefined
building blocks, called rules.  Path patterns are specified like Express
routes, implemented with the NPM package `path-to-regexp`.  We will add further
rules as needed.

Examples settings:

```javascript
settings.fso.permissions: [{
    rule: 'AllowInsecureEverything',
    usernames: [
      'sprohaska',
    ],
  }, {
    rule: 'AllowPrincipalsPathPrefix',
    pathPrefix: '/example/',
    principals: [
      'ldapgroup:ag-alice',
    ],
    actions: [
      'fso/discover',
    ],
  }, {
    rule: 'AllowLdapGroupFromPath',
    pathPattern: '/example/orgfs/srv/:device/:group/(.*)',
    actions: [
      'fso/discover-root',
      'fso/init-repo',
      'fso/list-repos',
      'fso/list-repos-recursive',
      'fso/read-repo',
      'fso/read-repo-tree',
      'fso/refresh-repo',
      'fso/write-repo',
    ],
  }, {
    rule: 'AllowPrincipalsPathPattern',
    pathPattern: '/example/orgfs/srv/:device?',
    principals: [
      'ldapgroup:ag-alice',
    ],
    actions: [
      'fso/list-repos',
    ],
  }]
```

Gist of the implementation:

```javascript
// `Rule` enumerates the available permission rules.
const Rule = {
  // `AllowInsecureEverything` enables full, unchecked fso access for
  // individual users.  It should only be used during the preview phase.
  AllowInsecureEverything: 'AllowInsecureEverything',

  // `AllowPrincipalsPathPrefix` allows a list of `principals` to perform a
  // list of `actions` on paths that start with `pathPrefix`.  The principal
  // must be of type `username:` or `ldapgroup:`.  We add more types as needed.
  AllowPrincipalsPathPrefix: 'AllowPrincipalsPathPrefix',

  // `AllowPrincipalsPathPattern` allows a list of `principal` to perform a
  // list of `actions` on paths that match `pathPattern`.  The parameters of
  // `pathPattern` are ignored; it is sufficient that the path matches.
  AllowPrincipalsPathPattern: 'AllowPrincipalsPathPattern',

  // `AllowLdapGroupFromPath` uses a `pathPattern` that must have a named
  // parameter `:group` to extract an LDAP group name from the path and grant
  // access to an `ldapgroup:<group>` principal to perform a list of `actions`.
  AllowLdapGroupFromPath: 'AllowLdapGroupFromPath',
};

for (const p of perms) {
  switch (p.rule) {
    case Rule.AllowInsecureEverything:
      for (const u of p.usernames) {
        for (const action of AllFsoActions) {
          statements.push({
            principal: `username:${u}`,
            action,
            effect: 'allow',
          });
        }
      }
      break;

    case Rule.AllowPrincipalsPathPrefix: {
      const principals = new Set(p.principals);
      for (const action of p.actions) {
        statements.push({
          principal: /^(username|ldapgroup):[^:]+$/,
          action,
          effect: (opts) => {
            if (!principals.has(opts.principal)) {
              return Effect.ignore;
            }
            if (opts.path.startsWith(p.pathPrefix)) {
              return Effect.allow;
            }
            return Effect.ignore;
          },
        });
      }
      break;
    }

    case Rule.AllowPrincipalsPathPattern: {
      const matchPath = compilePathPattern0(p.pathPattern);
      const principals = new Set(p.principals);
      for (const action of p.actions) {
        statements.push({
          principal: /^(username|ldapgroup):[^:]+$/,
          action,
          effect: (opts) => {
            const m = matchPath(opts.path);
            if (m && principals.has(opts.principal)) {
              return Effect.allow;
            }
            return Effect.ignore;
          },
        });
      }
      break;
    }

    case Rule.AllowLdapGroupFromPath: {
      const matchPath = compilePathPattern1(p.pathPattern, 'group');
      for (const action of p.actions) {
        statements.push({
          principal: /^ldapgroup:[^:]+$/,
          action,
          effect: (opts) => {
            const pathGroup = matchPath(opts.path);
            if (!pathGroup) {
              return Effect.ignore;
            }
            const principalGroup = opts.principal.split(':')[1];
            if (pathGroup === principalGroup) {
              return Effect.allow;
            }
            return Effect.ignore;
          },
        });
      }
      break;
    }
  }
}
```

#### Access control in FSO daemons

See proof of concept:

```
fuimages_nog_2016/p/fso@caf3633cd1e6fad7ba07fe6b0bee1decc7f2416b 2018-01-03
'fso: use fso/auth API in `nogfsoctl {stat,sha} ...`'

Earlier version:
fuimages_nog_2016/p/fso@f63e1924659fa192f65b0c6f8832305377b7c2a6 2017-12-21
'fso: bump nogfso-0.0.28, log scope authz checks'
```

All services use TLS with both client and server certs.

The Meteor application issues JWTs that are scoped to specific operations.  The
scope is specified as a list of scope elements `{ actions, paths, names }`.
Access checks in `nogfsoregd` and `nogfsostad` authorize operations that are
specified as a combination of `{ action, path }` or `{ action, name }` and test
for each field individually whether it is in a scope element.  A scope element,
therefore, allows operations that are in the product sets `actions x paths` or
`actions x names`, assuming the access check uses either a path or a name but
not both.  An operation is authorized if it matches any of the scope elements.

Scope elements can use wildcards to match action groups, like `fso/*`, or path
prefixes, like `/orgfs/*`.  Asterisk `*` can be used to match any action, path,
or name.

The scope is encoded as a more compact representation for the JWT `sc` claim.
Briefly, `{ action, paths, names }` is encoded as `{ aa, p, n }`, where each
action is abbreviated according to a coding table that contains entries like
`fso/read-repo <-> frr` and `fso/* <-> f*`.  See `packages/nog-fso/fso-jwt.js`
and `backend/internal/grpcjwt/grpcjwt.go` for details.

The JWTs contain additional information that allow services to perform
redundant authorization checks.

User tokens contain the Meteor user in `sub` and the list of LDAP groups in
`xgrp`.

System tokens contain `sys:<username>+<subuser>` in `sub`, where `<username>`
is a Meteor username or a special name that is not in the `Meteor.users`
collection.  Special names start with `nog`, for example `nogapp`.  System
tokens may contain a `san` claim that is similar to a X.509 Subject Alternative
Name, but restricted to `DNS` values.  It can be used to check that the JWT
matches the TLS peer.

All JWTs that are forwarded to `nogfsostad` should have a narrow scope, so that
`nogfsostad` cannot misuse them to perform actions in other parts of the
system.  Broadly scoped JWTs should not be used to perform actions that might
involve a `nogfsostad`.  As an additional security measure, `nogfsoregd`
refuses to forward JWTs that contain wildcard scopes to `nogfsostad`.  This
should allow us to delegate running `nogfsostad` servers to a large group of
operators without granting too many privileges.  Some trust is needed though,
because the system could be easily spammed by `nogfsostad` operators.

The Meteor application issues narrowly scoped JWTs with a short lifetime (like
10 minutes) when making requests to FSO services.  Such JWTs are not stored and
cannot be revoked.

JWTs with a longer lifetime (like several weeks) are used for `nogfsostad` and
admins.  The IDs of those JWTs are stored in MongoDB.  The JWTs could be
revoked by maintaining a certificate revocation list (CRL) that would be
regularly distributed to all services.  We will not immediately implement the
CRL.  But we will maintain the necessary information in MongoDB, so that we
could add a CRL later.

Admin JWTs are usually broadly scoped and, therefore, should not be used for
operations that may involve `nogfsostad`, because the `nogfsostad` operator
could capture the admin JWT and use it to gain higher privileges.  To avoid
that, `nogfsoctl` contacts the Meteor application and exchanges the broadly
scoped long-term JWT for a narrowly scoped short-term JWT.

The JWT exchange is a REST endpoint using `nog-rest`.  A new code path will be
added to accept long-term JWTs in addition to Nog access key signatures.  The
details need to be decided as part of the implementation.

#### Access control options

Access control checks use an `options` object that has fields `path` or `name`
to specify details.  A single `uri` field could be used instead.  `path` and
`name` would be encoded in the URI, like `name:<name>` or `path:<path>`.  A URI
would be more flexible but less explicit about the field types that are
actually used.  We keep the explicit fields for now.  It should be feasible to
transition to a URI later.

#### Rejected access control alternatives

Fully trusted services with full permissions: The services authenticate to each
other with TLS client and server certs.  The services have full permissions to
perform any operation.  Access control happens only in `nog-app`.

OIDC-token-based access with GitLab permissions: The web app sends GitLab OIDC
tokens with the requests.  `nogfsog2nd` uses the token to access GitLab.  The
GitLab permissions apply on a per-request basis.  The GitLab permissions are
presented in `nog-app` somehow.

Some user-based access control in `nogfsostad`: `nog-app` sends information
that identifies the user that is requesting a `nogfsostad` operation.
`nogfsostad` uses the information to authorize operations.  A user could run
a personal `nogfsostad` that rejects operations of other users.

Hierachical access action naming like `fso/repo/read`: There might be some
hierarchical structure, but it is not always obvious.  For example, would
`fso/list-repo-tree` be `fso/repo/list/tree` or `fso/repo/tree/list`?  So we
use the two-level scheme `<subsystem>/<subsys-action>`.

### Reverse GRPC sessions

We assume that `nogfsostad` may run on file servers that cannot be directly
contacted on a public IP.

`nogfsostad` contacts `nogfsoregd` to start a reverse GRPC session (RGRPC) that
allows `nogfsoregd` to make GRPC calls to `nogfsoregd`.  A RGRPC session is
established as follows:

* `stad` sends a GRPC hello request to `regd`.  `regd` replies with a TCP
  address and a RGRPC slot number, after starting to listen on the TCP address.
  `regd` uses a special GRPC dialer to wait for a TCP connection by `stad`.
  `stad` and `regd` both create a token to identify the session during pings
  and exchange the tokens as part of the GRPC hello.
* `stad` initiates a TCP connection to the TCP address and sends a hello line
  with the slot number.  It then hands over the TCP connection to GRPC as if it
  originated from a `net.Listener.Accept()`.
* `regd` accepts the TCP connection and parses the hello line.  It then hands
  over the connection to the special GRPC dialer as if it originated from
  a `net.Dial()`.
* GRPC continues as usual, starting with the TLS handshake.  `regd` is the
  client.  `stad` is the server.
* `regd` makes regular ping calls on the RGRPC connection.  The pings are used
  on both sides to detect if the connection is down.  If so, it is closed and
  a new reverse GRPC connection is initiated by `stad`.

`regd` can use the connection to make parallel client calls.  GRPC uses HTTP2
multiplexing.  A single TCP connection is sufficient.

Rejected alternative: Assume that every file server is reachable via a public
IP.  We do not want to make this assumption, because it might require
substantial changes to existing firewall policies.

### Event-sourcing store

See proof of concept in supplementary information.

Events are encoded using Protocol Buffers.  Each event is identified by a ULID,
<https://github.com/alizain/ulid>, which is a 128-bit binary identifier that is
mostly ordered by time, except for time drift and leap seconds.  Time is not
used to establish event order.  The order is explicitly represented by parent
pointers, similar to a Git history without branches.

Each event history corresponds to an aggregate, whose current version is stored
as a head pointer.  Aggregates and thus event histories and head pointers are
identified by UUID.  We use two UUID versions as described in RFC 4122: random,
version 4 and name-based SHA-1, version 5.

For aggregates that are externally identified by name, the internal UUID is
a name-based version 5 UUID in the namespace with UUID
`ecb02ec6-006d-429f-a378-9392612f9c61`, called the UUID namespace of Nog names.
Names are further prefixed to qualified names before deriving the UUID:

 - main: `fsomain:<name>`
 - registry: `fsoreg:<name>`

Atomic append of several events is implemented similar to Git.  The immutable
events are written into a MongoDB collection, followed by a compare-and-swap of
the head pointer.  The serial order is created during the first subsequent read
and stored into a separate MongoDB collection for future use.

Three related MongoDB collections are used for storing event histories:

```
events: { _id: uuid, protobuf }
heads: { _id: uuid, ulid }
journal: { _id: uuid+ulid, serial, protobuf }
```

 - `uuid`: aggregate UUID, 16 bytes `BinData`
 - `ulid`: event ULID, 16 bytes `BinData`
 - `uuid+ulid`: 32 bytes `BinData`
 - `serial`: event order number, 1-based `NumberLong`
 - `protobuf`: encoded event data, var length `BinData`

The collections use no additional indexes.

The lookup of the `serial` for an event `ulid` in a history `uuid` is a direct
lookup by primary key `uuid+ulid`.  Finding event for a history is a range
query `{ $gte: <uuid>0000..., $lte: <uuid>ffff... }`.  Either MongoDB can be
instructed to sort by `serial` or the sequential order can be established at
the client from a scan that is sorted by id.  Most events should already be in
order, since ULIDs are mostly time-ordered.  If events arrive out of order,
they are buffered until the next sequential event arrives.  A small buffer
should be sufficient, because ULIDs are mostly ordered.

`protobuf` is duplicated in `journal` to allow scanning the journal without
indirect access to `events`.

Aggregate state is reconstructed by reading and applying events as a fold of
the pure function `advance(state, event) -> state`, which must not fail.
Commands are executed by calling the pure function `tell(state, command) ->
[events]`, which may return an error if the command is invalid.  The returned
events are stored.

In practice, temporary state can be avoided by using a stateful `Advancer` for
a batch of events.  `Advancer.advance(state, event) -> state` copies state
memory only once and updates the copied memory in later calls.  This
optimization may be relevant for large aggregates to avoid `O(n^2)` behavior
when applying a batch of events.  An alternative could be data structures that
use implicit structure sharing as known from functional languages.

Clients can subscribe to a GRPC stream of events.  New events are sent
immediately to subscribed clients if the events have been inserted by the same
GRPC server process, using a Go channel for signaling.  The server, in
addition, polls at regular intervals for events that may have been inserted by
other processed directly into MongoDB.

We use three sets of collections to separate events by aggregated type:

```
fsomain.events
fsomain.heads
fsomain.journal

fsoregistry.events
fsoregistry.heads
fsoregistry.journal

fsorepos.events
fsorepos.heads
fsorepos.journal
```

Rejected alternatives:

A single set of related collections could be use to store all events for all
aggregate types, since there are no conflicting UUIDs.

The event order could be established as a series of sequential document ids
when writing to MongoDB.  Multiple events can be stored in a single MongoDB
document to support atomic append of multiple events.

### Event-sourcing read models

We use several approaches to track event-sourcing entities.  The approaches are
described in the subsections.

#### Background tracking event-sourcing entity

The registry stores events for repo changes that need to be tracked in the
background, such as creating a repo or enabling GitLab.  An event is initially
stored on the registry and only afterwards added to the repo event history.
The registry and the repo will eventually both contain related events.
A background job can observe the registry to learn about all repo changes that
are relevant for background processing.  It needs not observe the individual
repo event histories.

This approach is used for the background job that creates a GitLab project when
GitLab is enabled for a repo.

Alternative: Events are added to the registry first in order to provide
a single event history that can be observed.  The registry events could perhaps
be replaced by a mechanism that allows a background processor to observe many
repos at once.  The registry could provide a separate broadcast channel to
which it publishes each repo update at least once.  The registry would need
a stateful processor that ensures at-least-once delivery.

#### On-demand event-sourcing client-side read model

The client reads events for a specific repo when it needs the repo state to
respond to a request.  It applies the events to create a client-side read
model.  The read model can be cached to reduce event loading operations.  The
client then watches the repo for additional events as long as it wants to
responsively update the state.

This approach is used when there is an external trigger to access an entity and
background processing is not needed otherwise.  An example is viewing a repo in
a web UI: the state is only needed when a user opens the repo URL.  The state
is responsively updated as long as the user is connected.  Nothing happens if
there is no user connected.

#### Reading event-sourcing entity from registry

Instead of reading events and applying them to construct a client-side read
model, a client can request the repo state from the registry in a CRUD-style
get operation.  The registry server loads and applies the events to return the
read-model state.

This approach may be useful to keep clients simple.  It is limited to clients
that need one-time access to state that is of general interest.  Clients that
need more specific information should use a client-side read model as described
in the previous section.

### Event broadcasting

In addition to event sourcing, we use event broadcasting.  Broadcasting
delivers events at most once.  Broadcast events are not used for event-sourcing
entities.  Clients must implement a different strategy to ensure eventual
consistency and use broadcasting only to improve responsiveness.

Example: Updates of Git refs are announced as a broadcast event.  Clients pull
right away, before the regular poll interval, if they observe the Git ref
update event.  The broadcast events are like an unreliable, distributed Git
reflog.

The event broadcasting history must not contain essential state.  It should be
possible to aggressively trim its history at any time.

Event broadcasting uses the event journal implementation as discussed in the
section on event sourcing.  Events are delivered on channels with well known
names.  Channels may be partitioned to shard reads and writes.  We use a single
channel unless we observe performance problems.

### Nog UI

Details of the FSO UI will be discussed in a future NOE.

The initial filesystem observer UI will be implemented as separate packages
that do not share logic with the exiting packages.  We may refactor towards
some code sharing.

The UI is implement using React.

The UI will be modified to allow hiding the traditional UI behind a feature
toggle and show only the FSO UI.  Details will be discussed in a future NOE.

### Catalog

We add a plugin mechanism to `nog-catalog` in order to integrate FSO into the
existing catalog implementation.

See proof of concept:

```
fuimages_nog_2016/p/fso@d12c99886261df06a33604c9512ce0894728c5d5 2017-12-11
'catalog: manage plugin versions to force catalog rebuild after plugin changes'
```

Plugins specify a `repoSelectorKey` that they handle.  If a `repoSelector`
contains that key, the plugin is called to add catalog content.  Otherwise the
default code path executes, which adds traditional Nog repos.  Every plugin
specifies a version that can be bumped to force a catalog rebuild after the
plugin implementation changes.

Example:

```javascript
fsoPlugin = {
  name: 'fso',
  version: 1,
  repoSelectorKey: '$fso',
  addContent() { ... },
};
```

Catalog config:

```yaml
preferredMetaKeys: []
contentRepoConfigs:
  - repoSelector: { $fso: { path: { $regex: '^/example/data/.*' } } }
    pipeline:
      - ...
```

The main app uses dependency injection to create and register the plugin:

```javascript
NogCatalog.registerPlugin(createFsoCatalogPlugin({
  registries: NogFso.registries,
  repos: NogFso.repos,
  testAccess: NogAccess.testAccess,
  registryConns: registryConns(),
}));
```

We modify `nog-catalog` to pass as many details to the plugin's `addContent()`
as needed to implement it.  We may also copy code from `nog-catalog` for the
initial implementation.

The FSO catalog plugin selects candidate repos based on the repo paths of the
FSO state that is cached in MongoDB.  It uses the GRPC `GitNog.Head()` to get
the latest GitNog commit and the individual Git commits.  If the GitNog commit
is unchanged, the plugin skips the repo.  Otherwise, it uses
`GitNogTree.ListMetaTree()` to list the path metadata for the head Git meta
commit.  It translates the path metadata to mimic traditional Nog content:

```javascript
content = { _id, path, name, meta }
```

`path` is constructed from the FSO repo path, joined with the tree path within
the repo.  The tree path `.` is used to indicate the repo root, which is used
to store per-repo metadata.

For the repo itself, `name` is constructed as a multi-level path suffix of of
the repo path.  For paths within the repo, `name` is its basename.

`meta` is the path metadata.

Traditional Nog content is immutable and has a content address.  To mimic it,
the FSO plugin mangles the GitNog commit with details that cannot be determined
from the Git commits and computes `_id` as a sha, like:

```javascript
_id = sha1Hex([
  'fso',
  repoPath,
  head.commitId.toString('hex'),
  path,
].join('\n'));
```

Alternative: This design allows us to create catalogs that contain traditional
Nog content and FSO content at the same time.  In practice, we may only be
interested in catalogs that contain either FSO or traditional Nog.  Two
separate implementations might be simpler.  We will reconsider when we have an
FSO catalog that is useful in practice.

Alternative: Use `GitNog.Meta()` for per-repo metadata instead of
`GitNogTree.ListMetaTree()`.  `Meta()` would be simpler to use for catalogs
that use only per-repo metadata.  But we want to support catalog entries that
point to individual paths within a repo, so we use the more general approach
`ListMetaTree()`.

Option: Use additional information, like per-path stat or per-path content,
in the catalog.  We start with metadata and reconsider later.

Alternative: When using additional information, the FSO catalog plugin may need
to merge per-path information from multiple `ListXTree()` calls.  Merging
per-path information could be implemented in `nogfsostad` and provided as
a new, more generic `ListFsoTree()` GRPC.

### Partitioning registries / roots / repos for an orgfs

Assuming an filesystem with `srv/<device>/<ou>` and `org/<ou>` trees, how
should we partition FSO state?

We start with:

* one registry for `srv/` and one for `org/`;
* roots `srv/<device>/<ou>/` and `org/<ou>/<subdir>/`.

We accept the risk that we may need to re-partition later.  Details below.

Assumptions / estimates:

* 100 organizational units.
* 10 devices.
* 10 acquisitions per day per device: 100 new repos per day: 10000 new
  repos per year: 100k - 1M repos in 10 years.
* Event size several 100 Bytes, reading 1M repos during startup: loading of 1GB
  event data during startup, which is not ideal but might be feasible.  Loading
  events for 10k repos in a single registry took less than 2 seconds during
  a quick test on 2017-11-29 using the Docker Mac dev setup.  Rechecking the
  repo initialization in `nogfsoregd`, however, was much slower.  But it should
  be straightforward to optimize.

Registries are organized in a hierarchy such that a tree is the union of the
registries below.  Crisscross responsibilities, like one registry per
organizational unit with roots `srv/<device>/<ou>` and `org/<ou>`, are
forbidden.  The `srv/` tree would not be the direct union of the registries
below.  Reasons: A hierarchy feels right.  Crisscross might cause confusion
later.

We start with two registries, one for `srv/` and one for `org/`.  The current
design uses one GRPC watch per registry.  Based on the assumptions above, other
ways of partitioning would require many GRPC watches:

* Each registry only for one organizational unit: 100 ous on 10 devices: more
  than 1000 registries.
* Each `org/<ou>` with a separate registry: more than 100 registries.

Rejected alternative: An aggregation service that watches many registries and
combines them into a few event streams, so that watching many registries would
be cheap.  We do not want to add an additional service now.

The roots are at the shortest prefix below which a uniform naming convention is
expected.  For devices, each OU is likely to use a different naming convention.
For OU dirs, each subdir, like `projects/` and `people/`, is likely to use
a different convention.

We should be prepared for re-partitioning later.  Sketch of a potential
procedure:

* Export state like mapping of repo ID to name or roots naming.
* Init new registries.
* Partition state and import into new registries.
* Configure Meteor app to use new registries behind a feature toggle.
* Switch from old to new registries.
* Archive old registries and remove state.

Most of theses building blocks seem useful anyway, like exporting or archiving
state, which could be useful for backup, too.

We may need some form of symlink that let users navigate from `org/<ou>` to
`srv/<device>/<ou>` in the repo listing UI, as they can on the filesystem.  We
would introduce an ad-hoc solution.  The general design would continue to
assume a single canonical repo name that corresponds to the filesystem
realpath.

### How to operate nogfsostad on an orgfs?

`nogfsostad` writes data into the shadow tree, whose quota should be allocated
to an OU.  Assuming we want to neither run it as root nor use special
capabilities, `nogfsostad` must run as a user that is member in the OU group,
so that it can create files as this group.  We could run one `nogfsostad` per
OU with a dedicated daemon user for that group, or we could run `nogfsostad`
daemons that manage data for multiple OUs, using daemon users that are in
several OU groups together with a suitable SGID convention.  We could run
a single `nogfsostad` with a daemon user that is in all OU groups.  Linux
allows up to 64k supplementary groups.

In order to avoid adding the daemon user to the OU groups in LDAP, we use
a supervisor daemon `nogfsostasvsd`.  It uses group prefixes to select groups
from `getent` and then runs `nogfsostad` with a list of numeric supplementary
GIDs.  `nogfsostasvsd` checks at regular intervals whether the groups have
changed and if so, restarts `nogfsostad` with updated GIDs.

Possible alternatives:

* Run a single daemon as a daemon user that is in all OU groups.
* Run one daemon per multiple OUs as a user that is in multiple OU groups.
* Run one daemon per OU.

Further alternatives if we adjust the assumptions:

* Run the daemon as root, use `chown()`.  Feels too insecure.
* Run one daemon per facility for `srv/<device>/`; use capability to allow
  `chown()`.  Seems reasonable.
* Use capability to change supplementary groups as needed, maybe for separate
  background processes that perform the actual work.

The following ideas have been rejected:

* Run a single sever with a special ACL that allows it to access the full
  filesystem, use capability to allow `chown()`.  We would need to manage more
  complex ACLs.

## How we introduce this

The initial goal is to use the filesystem observer to create a catalog of
existing data repositories at Visual and BCP.  The discovery service will be
used to find and add untracked directories.  The UI will support way to add
minimal metadata.  The repos can then be located in a catalog.

Creating directories and accessing data is out-of-scope.  Direct filesystem
access must be used as before.

We start with a ZIB filesystem.  We initially run the filesystem observer
manually.

We use local Git repos for initial testing and consider later whether to add
`git.zib` for permanent storage.  We try at least two storage options before we
decide when to use them:

 - shadow only, no GitLab
 - shadow and GitLab

The GRPC servers initially run on an OpenStack VM.

The first Nog users get a special role that allows them to view filesystem
observer repos.  The mechanism will soon be replaced by some kind of role
scheme that is based on LDAP information.

The initial Git Nog service only supports summary information and per-repo
metadata.

The initial discovery service supports stdrepo naming.  We incrementally
extended it to handle a variety of use cases on the ZIB filesystem.

Before we transition to the initial production setup, we try the following:

* We practice re-creating the FSO registry from scratch.
* We operate at least two separate `nogfsostad` servers that run with different
  Unix permissions in order to simulate a filesystem with multiple Unix groups

## Limitations

This is only the initial design.  More is needed to create useful services.
See future work.

The filesystem observer concept depends on an existing filesystem
infrastructure.  It cannot be used to create ad-hoc communities, like a group
of researchers from different organizations that want to work together.

## Alternatives

A fundamentally different approach could be to build upon a file managing
service, like iRODS or StrongLink.  It would manage the lowlevel details.  We
would wrap the service to provide a specific UI.  If we had access to such
a service, we would consider it.  But there is no such service readily
available to us.  Comparing the two approaches (1) establishing the lowlevel
service first and then building a UI on top of it and (2) implementing FSO as
described here, we believe that we can provide first useful services to users
earlier if choosing FSO.  We also hope that storing information in Git will be
more flexible in the long run and avoid vendor lock in.

Alternative details are discussed in the individual sections.

The Meteor app could access GitLab directly instead of indirectly through the
Git Nog service.  We prefer the indirection to avoid GitLab-specific code in
Nog app.  The Git Nog service that is implemented with GitLab REST could later
be replaced by an implementation that uses Git repos on a local filesystem.
The essential concept is independent of GitLab.

## Future work

The questions in this section seem relevant but will not be answered in this
NOE.  They are left for future work.

Details of the FSO UI.

How to extend the concept to integrate tape storage, file download, Git LFS,
and more?

A service to create new directories that use a location-dependent naming
convention.

Enforcing naming conventions within data repositories.

Evolving the Git filesystem observer concept to be more generally useful
without the FSO servers

## Supplementary information

* `git-stat-shadow_2017-08.tar.bz2`: `git-fso` proof of concept
* `nog-store-fso_2017-08.tar.bz2`: Git Nog GRPC proof of concept
* `nog-event-store_2017-08.tar.bz2`: Proof of concept event sourcing with Go,
  GRPC, MongoDB
* `fuimages_nog_2016_backend.tar.bz2`,
  `fuimages_nog_2016_packages_nog-fso.tar.bz2`,
  `fuimages_nog_2016_packages_nog-fso-ui.tar.bz2`: More complete proof of
  concept from `fuimages_nog_2016/`.  The source code is included only as
  a reference.  It will not compile by itself.  Use a full `fuimages_nog_2016`
  working copy to compile.
* `fuimages_nog_2016/next@83095f43412b45b15b9f5c93d5457f41385586ee` 'Tie next:
  p/fso shadow-only': Updated proof of concept that demonstrates shadow-only
  FSO repos and at-most-once event broadcasting
* `fuimages_nog_2016/next@75f42263fdc22951a0d0cbf2a42787354367e044` 'Tie next:
  p/fso nogfso 0.0.3': Reverse GRPC proof of concept
* `fuimages_nog_2016/p/fso@d12c99886261df06a33604c9512ce0894728c5d5` 2017-12-11
  'catalog: manage plugin versions to force catalog rebuild after plugin
  changes': FSO catalog proof of concept

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* v3.0.1, 2019-11-01: polishing
* 2019-10-28: frozen
* v3, 2019-08-05: nogfsostasvsd
* 2019-07-24: Fixed typo
* v2, 2019-07-24
* 2018-10-30: Clarify that a single `nogfsostad` daemon user can be in all OU
  groups
* 2018-02-06: Add section "shadow repo filesystem layout" with alternative
  UUID-based shadow naming
* 2018-02-06: Add alternative how to operate `nogfsostad` on an orgfs with
  a daemon user that is in multiple OU groups
* v1, 2018-01-08
* 2018-01-08: Decision to keep access control options `{ path, name }`
* 2018-01-08: Decision to keep two-level `fso/read-repo-tree` action naming
* 2018-01-08: Decision to keep dash names `master-stat`, ...
* 2018-01-08: Decision to use Git branches
* 2017-12-21: JWT-based access control in FSO daemons
* 2017-12-13: Discuss fundamental alternative
* 2017-12-13: Run one `nogfsostad` per OU
* 2017-12-13: How to partition roots?
* 2017-12-13: Re-creating registry from scratch.  How to handle repo UUIDs?
* 2017-12-13: General polishing
* 2017-12-11: FSO catalog plugin
* 2017-11-29: How to partition registries / roots / repos?
* 2017-11-27: Access control in Meteor app
* 2017-11-21: Details about per-path metadata
* 2017-11-20: All writes, including metadata, via `nogfsostad`
* 2017-11-20: Git Nog tree service
* 2017-11-13: Service to discovery untracked paths
* 2017-11-08: Tracking of selected realdir content
* 2017-11-06: Event-sourcing read models
* 2017-10-27: Reverse GRPC
* 2017-10-25: Shadow-only proof of concept
* 2017-10-25: Event broadcasting
* 2017-10-13: New storage flexibility and access control sections
* 2017-10-13: Polished
* 2017-10-06: More complete proof of concept
* 2017-09-04: Details about event sourcing registry implementation
* 2017-08-31: How to group services into programs?
* 2017-08-31: Registry service, event sourcing
* 2017-08-21: Initial version
