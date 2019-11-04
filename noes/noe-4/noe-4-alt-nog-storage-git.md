# NOE-4 -- Alternative Nog Storage Back Ends, Specifically Git
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2017-08-21

NOE-4 contains ideas that are not actively used anymore.  Its status is frozen
instead of retired, because the ideas seem still relevant for potential future
work.

See [CHANGELOG](#changelog) at end of document.

## Open questions

Questions that will be answered before the document graduates to final.

How to implicitly map Git LFS placeholders to Nog blobs?  When a Git store
detects LFS placeholders, they should somehow work like Nog blobs.

Should we use Git LFS directly for some use cases?  A compute job, for example,
could clone Git and fetch data via Git LFS and push results in the same way.
The Nog API would only provide the access information.  Instead of Nog REST
with a S3-like object storage, we would operate Git with Git LFS, which also
requires some kind of blob transfer via HTTP.

## Summary

This NOE proposes alternative storage back ends that could be used to store Nog
repository content instead of MongoDB.

The option to use Git as a back end is discussed in detail.

## Motivation

We decided in fall 2016 to use a traditional filesystem for BioSupraMol and
adapt Nog accordingly.  [NOE-2](./../noe-2/noe-2-filesystem-repos.md) mentioned
repositories to organize data and access.  NOE-2, furthermore, describes the
decision to use a system process with privileged access to freeze subtrees by
setting inodes to immutable using `chattr`.  NOE-2 left the details
unspecified.

We also decided to move the operations of [nog.zib.de](https://nog.zib.de) to
local infrastructure at ZIB and/or FU and abandon the use of commercial
services, at least for now.  Since there is no local, production-quality object
storage available, we need alternatives.  For ZIB cooperation projects, we
specifically need options to operate with filesystem storage available at ZIB.

In general, it seems advisable to modify the design of Nog such that it adapts
more flexibly to different infrastructure.  It should no longer assume that
object storage is available.  Instead, Nog should support options to operate on
traditional infrastructure that is typically available at research institutes.
Specifically, it should provide an option to use traditional filesystem
storage, maybe with reduced functionality.

Traditional infrastructure has many facets.  Some facets that we need to handle
are:

* Unix filesystem permissions without unified account management.  Example:
  BCP and ZIB use two separate LDAPs as identity providers.
* Network filesystem access, such as NFS for Unix and SMB for Windows.

A general question is how to maintain the illusion of a unified research data
management and analysis service with fragmented traditional infrastructure.
The first step, addressed here, is to support filesystem storage.

## Design

### High level service architecture

The architecture of Nog is modified such that Nog supports back ends to store
essential repository state on a filesystem.  For filesystem-backed
repositories, MongoDB may still be used as a cache, but all data and metadata
must be available from the filesystem alone, so that caches can be rebuilt from
scratch.

The filesystem back ends are implemented as multiple daemons to allow privilege
separation using Unix permissions.  Daemons should use root privileges only if
necessary.  Services would ideally degrade gracefully if some privileges are
not available.  For example, `chattr` may not be available in some contexts,
but most of the functionality would be available nonetheless.

nog-app stores a repo stub in MongoDB but not the repo content.  The repo stub
contains information how to contact a storage back end micro service.  Example:
repo `alice/foo` uses the GRPC service `NogStore` at service endpoint
`storage-server-01` with repo path `example/mapping/alice/something/foo`.

All operations are performed on a repository, so that nog-app can determine the
back end.  Example: A blob upload requires a repository.

nog-app forwards requests to the back end.  Auth is implemented with JSON Web
Tokens (JWT).  The JWT contains information that allows the back end to check
access without contacting a central authority.  The JWT could, for example,
contain the Unix id that the back end service should use to access the files on
disk.  The back end service would then start a separate process with the given
Unix id and redirect nog-app to directly contact the separate process, or the
back end proxies requests to the separate process.  Alternatively, the back end
could check access permissions as if it was running with the other Unix id and
then proceed as a privileged process, for example, when using `chattr`.

Connections are managed such that setup costs are amortized over multiple
requests.  Examples: A GRPC back end client could be instantiated per Meteor
connection and used for all subscriptions and method calls of this connection.
A GRPC stream could be used, so that JWT auth is checked only once for multiple
get operations.

The same general principle can be applied to develop several back end micro
services over time:

* Storage back end for active repositories.
* Repository discovery that scans known filesystem locations for new
  directories and creates corresponding repos.
* Freezing / unfreezing to control the immutable attribute.
* Replication to SAMFS for backup.
* Archival on SAMFS.
* Per-repo object storage micro service to read blobs.
* Per-repo object storage micro service to receive blobs.

Filesystem storage back ends use Git to store content and Git LFS for binary
data.  We will consider supporting Git Silo, at least for read-only access to
legacy repositories.

For BCP, Git repositories will be hidden in a shadow tree.  Example:
User-visible `/fs/bsmol/foo/bar` with shadow Git repo at
`/fs/git/bsmol/foo/bar/.git`.

For ZIB, Git repositories will be hidden only for some parts of the filesystem.
Projects that already use Git, can continue to do so.

### High level content storage design

Nog ids are generalized to support opaque ids that are not computed as
a content hash of a standard JSON layout.  Instead, most code treats ids as
opaque identifiers without meaning.

Nog content exists on two levels:

 - The high level is the Nog standard JSON layout.
 - The low level is the back-end-specific representation.

The relation between high level and low level is similar to Unicode and byte
streams.

The conversion of high-level Nog JSON to the back-end specific representation
is called lowering.  The reverse operation is called lifting.  Back ends may
use an intermediate representation to implement conversions efficiently.  See
section 'Git back end' below.

The conversion algorithm is represented by an enum called `Encoding`.

The full key to access content is a struct called `Entry`.  An entry consists
of three parts:

* `type`: `commit`, `tree`, `object`, and maybe `blob`.
* `id`: It can be a traditional Nog content SHA1 or an opaque id.
* `encoding`, often abbreviated as `enc`: The conversion algorithm.

Ideally, all code paths would be aware of the encoding.  For example, the
encoding of tree children would be passed to the UI as supplementary
information and the UI would pass the encoding back during requests for the
children.  In practice, not all clients can be modified at once.  The encoding
must instead be managed implicitly using heuristics, at least for a transition
period.

Specifically, tree entries will remain `{ type, sha1 }` for now, although
`sha1` may contain opaque ids that are not related to SHA1 at all.

Storage back ends may store ids explicitly or implicitly.  Examples:

* `_id` is explicitly stored in MongoDB.
* Standard JSON docs that use the Nog scheme could be stored without id; the
  ids could be re-computed as the content hash of the JSON.
* A Git back end can map content implicitly from Git to Nog.  The Nog commit id
  is constructed as `gid1:<sha>` with something like
  `sha=sha256({scheme:"gid1",commit:"<git-commit-id>"})`.  The Nog tree id is
  constructed as `gid1:<sha>` with something like
  `sha=sha256({scheme:"gid1",name:"root",tree:"<git-tree-id>"})>`.  The
  intermediate representation with the name is necessary, because the same Git
  tree may correspond to Nog trees of different name.
* A Git back end can implicitly map a full Git tree to a Nog tree and compute
  traditional Nog sha1s recursively, which has a time complexity of O(full tree
  size).
* A Git back end stores the Nog commit id as a footer line in the commit
  message in order to return the Nog commit id later without inspecting the
  parent Git history.

Encodings may be unable to represent all possible Nog content.  A back end
rejects content that it cannot store.  A back end may use indicators in the
high-level Nog meta and in the low-level representation to implicitly determine
the encoding.

Example: A Git back end uses an encoding `git-native` to map Nog content to
native Git content.  The encoding `git-native` does not support generic
metadata.  Instead, it requires that all Nog content uses
`meta.git.encoding="nat1"` and nothing else in `meta`, which is the indicator
for the back end to use `git-native` encoding.  If content uses different
`meta`, the back end decides to use the encoding `git-meta-subtree` and stores
metadata as JSON in separate Git blobs in a Git subtree `.nog` and a special
file `.noggitencoding` with content `mst1` in the Git toplevel tree to indicate
the encoding.  An error is reported if an assumption fails, for example if
a Git tree that should be `git-native` contains a file `.noggitencoding` or
a subtree `.nog`.  The construction ensures that copying a full history from
Git to Nog to Git will either cause an encoding error or yield identical Git
ids at the destination.

Id schemes may need to evolve over time.  Opaque ids are formatted as
`<scheme>:<content-id>`.  Traditional Nog ids continue to work.  An id is
traditional if it does not contain a colon.  Examples:

* `<sha1>`: A traditional Nog id.
* `gid1:<sha256>`: A mapping of Git content using a specific encoding scheme
  that is mangled into the SHA256.  See below for details.

Clients may control ids or leave id creation to the storage back end.  If
a client controls the id, it uses a traditional Nog sha1.  Examples:

* A client computes and posts the Nog sha1 content id.  A Git back end
  constructs a Git tree from which the Nog sha1 can be re-created.  The client
  can construct an entire multi-level Nog tree and post it at once.
* A Git back end stores the Nog commit id in the commit footer, so that it can
  later return it without using a recursive definition that would require
  a full Git history walk.
* A client posts a Nog commit without id to a repo that uses Git storage.  The
  back end does not create a commit message footer, but instead constructs an
  `gid1:<...>` id that is based on the Git commit id.  The client must post
  a commit and wait for the server to return the id before it can construct
  a dependent commit.

Encodings combine a conversion strategy and several customization points into
a coherent algorithm.

Examples of customization points are:

* `_idversion` in MongoDB.
* Id scheme prefix.
* Tag in `meta.git.encoding`.
* Tag in Git tree `.noggitencoding`.
* Tag in Git commit message footer.

Each of the customization points needs to be managed independently during
encoding evolution.  Tags should not naively use the encoding name.  The tag
naming conventions are:

* Stable tags use a few characters followed by a simple numeric version.  The
  first stable scheme version uses `1`.  Examples: `gid1`, `nat1`, `mst1`.
* Tags may be prototyped as version 0.  Examples `gid0`.
* Tags may use a suffix during early prototyping to indicate a pre-release and
  reserve the name for the stable scheme.  The suffix is dropped as soon as
  there is hope that the scheme will graduate to stable in a backward
  compatible way.  Example: `gid1a0` for scheme Git id 1-alpha-0 during early
  prototyping; but `gid1` as soon as the code feels stable enough.
* If tag changes are necessary, because an encoding break backwards
  compatibility, either the next full version is used, like `gid2`, or a dot
  version, like `gid1.1`.  Details will be decided later.

Encodings combine information about the back end, how metadata is represented,
and how ids are generated.  The tentative naming convention has been copied
below; see `nog*.proto` for latest information:

```
`Encoding` enumerates the Nog storage encodings.

- 0: encoding not available.
- xxx: reserved for stable encodings, assign sequentially.
- 1bxxx: beta encodings, xxx equals expected stable id, b equals X in betaX.
- YYMMSSxxx: alpha, YY-MM date of assignment, SS serial in YY-MM.
- 999999xxx: to be assigned.

Naming scheme for Git encodings: <type>_<class>_<meta>_<ids>_<version>.

<type> = C  # commit
       | T  # tree
       | O  # object

<class> = GIT

<meta> = NAT  # Native git w/o additional metadata.
       | MST  # Metadata from subtree `.nog`.
       | MOB  # Metadata from side-by-side object `.meta.json`.

<id> = GID  # Git id as `gidX:...`.
     | NID  # Traditional Nog id as SHA1 over canonical JSON.

<version> = 0ALPHA0  # and so on.
          | 0BETA0
          | 0
```

Encoding evolution may be based on commit date.  Example: V1 supports only
native Git content.  V2 supports metadata encoding or native Git content.  To
introduce V2 in a backwards compatible way, a transition date is used.  With
V2, any commit before the transition date implicitly uses V1 trees unless a Git
commit footer specifies otherwise.  Any commit after the transition date
implicitly uses V2 trees unless a commit footer specifies otherwise.  A V2
server rejects V1 commits after the transition date.  In other words, the
default changes at the transition date from V1 trees to V2 trees.

Strategy for managing transition dates:

* Alpha and beta encodings should specify a validity date.  Servers refuse to
  create commits after the validity date.  The validity date may be updated to
  extend the testing period.
* Stable encodings may or may not specify a validity date.  If they initially
  do not specify a validity date, a date may be added as part of a deprecation
  process in order to force migration to an updated encoding.

### GRPC

See `nogstorage_2017-02/pkg/nogstorage2/nog2.proto` for full information.

Messages:

```c
// Pseudo-Protobuf

enum Encoding {
    ENC_NA;
    ...
}

// `Entry` is a collapsed Nog entry.
message Entry {
    EntryType type;
    string id;
    Encoding enc;
}

// `Commit` is a Nog commit, using entry pointers to transfer types and
// encodings.
message Commit {
    Entry id;
    string subject;
    string message;
    Entry tree;
    repeated Entry parents;
    repeated string authors;
    string authorDate;
    string committer;
    string commitDate;
    string metaJson;
}

// `Tree` is a Nog tree with collapsed entries.
message Tree {
    Entry id;
    string name;
    string metaJson;
    repeated Entry entries;
}

// `Object` is a Nog object.
//
// How to distinguish empty text from `null` text?  Tentative decision:
// Either text or blob must be available.  If blob is available, text is
// `null`.
message Object {
    Entry id;
    string name;
    string metaJson;
    string text;
    Entry blob;
}
```

GRPC service:

```c
service NogStore { // Protobuf
    rpc GetRef(GetRefRequest) returns (GetRefResponse);
    rpc GetCommit(GetCommitRequest) returns (GetCommitResponse);
    ...
}

message GetCommitRequest {
    string repo;
    Entry id;
}

message GetCommitResponse {
    Commit commit;
}
```

The design should consider the number of open GRPC connections.  The details of
GRPC connection sharing will be clarified as part of the implementation.  GRPC
seems to provide different approaches to implicit and explicit connection
sharing, depending on the programming language:

* In Go, connection dialing and client creation are separated; the connection
  can be explicitly shared,
  <https://github.com/grpc/grpc-go/issues/526#issuecomment-180071610>.
* The node client seems to automatically share connection to the same host with
  identical options,
  <https://github.com/grpc/grpc/issues/8515#issuecomment-256701944>.

### Git back end architecture

The Git back end uses an intermediate representation, named GIR, to implement
the conversion between Nog high-level and Git low-level.

Proof-of-concept implementation in `nogstorage_2017-02.tar.bz2`:

* `nogstorage_2017-02/pkg/nogstorage2/nog2.proto`: Protobuf messages and GRPC
  service.
* `nogstorage_2017-02/cmd/noggitstored2`: GRPC server.
* `nogstorage_2017-02/cmd/nogstore2/main.go`: Testing client.

The externally visible operations are the usual `GetX()` and `PostX()`
operations.  Full `Entry` keys are used everywhere:

```c
message Entry { // Pseudo-Protobuf
    EntryType type; // commit, tree, object, ...
    string id;
    Encoding enc;
}
```

Another proof-of-concept in `nog-store-fso_2017-08.tar.bz2`, whose focus is
read-only access to Git trees that use a specific layout convention to store
filesystem stat information.

#### Lifting Git to Nog

A history walk `GetRef()`, `GetCommit()`, `GetTree()`, ..., incrementally lifts
Git ids to Nog ids.  Each operation lifts just enough Git content, so that the
walk can continue.  A full walk could be executed in the background to pre-fill
GIR caches.

A lift is executed in two steps.  The first step, for example `GetRef()`, lifts
the Git id to a Nog id, of type `Entry`.  It stores a, potentially partial, GIR
in a BoltDB cache using the Nog entry as the key.  The second step, for example
`GetCommit()`, retrieves the GIR, finishes the GIR if it is partial, and
constructs a `Commmit`.

The GIR is constructed in two steps, so that unnecessary operations can be
avoided.  A history walk, for example, may traverse from a commit to the tree,
but never visit any of the parent commits.  The parent commit ids are
sufficient; the full commits are not needed.  See comment below `CommitGir`.

`LiftCommitId()` takes the following arguments:

* Git id;
* Encoding.

The function execution is memoized in BoltDB so that the actual lift operation
is executed only once.

The GRPC server implementation of `GetRef()` uses the interface:

```go
type Repo interface { // Go
    LiftCommitId(*git.Oid, pb.Encoding) (*pb.Entry, error)
    ...
}
```

GRPC `GetCommit()` uses:

```go
type Repo interface { // Go
    GetCommit(*pb.Entry) (*pb.Commit, error)
    ...
}
```

`Repo` uses an encoding-specific implementation for the actual lifting:

```go
type CommitLifter interface { // Go
    LiftCommit(Repo, *git.Oid, pb.Encoding) (*pb.CommitGir, error)
    FinishCommitGir(Repo, *pb.CommitGir) (*pb.CommitGir, error)
    NewCommitFromGir(Repo, *pb.CommitGir) (*pb.Commit, error)
}
```

The Git intermediate representation:

```c
message CommitGir { // Pseudo-Protobuf
    // partial Gir:
    string scheme; // Id scheme.
    Entry id; // The Nog id.
    bytes commitGid; // The Git object id, 20 bytes binary sha1.

    // full Gir:
    Entry tree; // Nog id of the tree; to accelerate new commit.
    repeated Entry parents; // Nog ids of the commit parents.
}
```

The two-step lifting avoids a recursion that would trigger a full history walk
when lifting a single commit.  If lifting the parent commit id triggered
constructing a full GIR, it would trigger the lifting of grandparents and, by
recursion, all ancestors.  But lifting the commit id triggers only a partial
GIR, so there is no recursion.

An alternative to two-step lifting is to only store information in the GIR that
can be computed without recursion.  Any additional information that is required
by `NewCommitFromGir()` must be read from the Git repository.  Some information
may or may not be cached separately from the GIR.

Lifting trees and objects works similarly, with a crucial difference.
`LiftXId()` for trees and objects needs additional arguments to propagate the
context in which the id is used.  An example is the tree name, which needs to
be determined from the context, since the same Git tree may have different Nog
names.

The general structure of the context information is not obvious.  The arguments
of `LiftXId()`, therefore, are grouped into a single struct:

```go
type Repo interface { // Go
    LiftTreeId(*pb.LiftTreeArgs) (*pb.Entry, error)
    LiftObjectId(*pb.LiftObjectArgs) (*pb.Entry, error)
    ...
}
```

```c
// Pseudo-Protobuf
message LiftTreeArgs {
    Encoding enc;
    string name;
    bool is_toplevel;
    ...
}

message LiftObjectArgs {
    Encoding enc;
    string name;
    bytes oid;
    ...
}
```

The same approach may be used with commits, too, for consistency.

An alternative, which we rejected, is to add a separate `details` argument.
The full argument list of `LiftXId()` then is:

* Git id;
* Encoding;
* Details, to propagate the context.

```go
type Repo interface { // Go
    LiftTreeId(*git.Oid, pb.Encoding, *pb.LiftTreeDetails) (*pb.Entry, error)
    LiftObjectId(*git.Oid, pb.Encoding, *pb.LiftObjectDetails) (*pb.Entry, error)
    ...
}
```

```c
message LiftTreeDetails { // Pseudo-Protobuf
    string name; // The Nog name is determined by the Git parent.
    bool is_toplevel; // Toplevel trees may use a special convention.
}

message LiftObjectDetails {
    string name;
    bytes meta_gid; // A Git blob with meta JSON.
}
```

The meaning of Git trees and objects is only defined in the context in which
they appear.  The context is propagated through a history walk that starts from
commits.  Since commits are the entry points for the walk, they are lifted
without context in order to implement `GetRef()` without providing a context.

Note that `GetRef()` requires an encoding.  Different interpretations of Git
are possible through different encodings.  In practice, we most likely will not
use this option but instead use a single default encoding that inspects the Git
content to determine details.

#### Lowering Nog to Git

Lowering constructs Git content from which the Nog content can be
reconstructed.  It then stores the GIR in BoltDB using the Nog id as the key,
so that dependent content can access the GIR.

Example: `PostObject()` creates Git blobs and stores their Git ids in the
object GIR.  `PostTree()` retrieves the object GIR and uses it to map the
children Nog ids to their corresponding Git ids in order to construct a Git
tree.  Code snippet from `LowerTree()`:

```go
func (rec *recoder) LowerTree(repo recode.Repo, pbt *pb.Tree) (*pb.TreeGir, error) {
    treeBuilder, err := repo.Git().TreeBuilder()
    insertObject := func(ent *pb.Entry) error {
        realId := recode.EntryWithEnc(ent, pb.Encoding_O_GIT_NAT_NID_6SPIKE0)
        gir, err := repo.GetObjectGir(realId)
        treeBuilder.Insert(gir.Name, git.NewOidFromBytes(gir.TextGid), git.FilemodeBlob)
    }
}
```

The relevant interfaces are:

```go
type Repo interface { // Go
  LowerCommit(*pb.Commit) (*pb.CommitGir, error)
  LowerTree(*pb.Tree) (*pb.TreeGir, error)
  LowerObject(*pb.Object) (*pb.ObjectGir, error)
}

type CommitLowerer interface {
    LowerCommit(Repo, *pb.Commit) (*pb.CommitGir, error)
}
type TreeLowerer interface {
    LowerTree(Repo, *pb.Tree) (*pb.TreeGir, error)
}
type ObjectLowerer interface {
    LowerObject(Repo, *pb.Object) (*pb.ObjectGir, error)
}
```

### Elements of Git encoding

This section describes building blocks that can be used to construct Git back
end encodings.  The final design needs to be finalized as part of the
production implementation.

All examples are in `nogstorage_2017-02/cmd/noggitstored2/`.

#### Native Git tree encoding

The native Git encoding represented a Git tree in Nog as if it was a simple
filesystem tree.

Free-form Nog meta is not supported.

The toplevel tree must have a fixed name, such as `root`.

All Nog trees and objects must use fixed meta, such as
`meta={"git":{"encoding":"nat1"}}`.  Using an indicator allows the Git back end
to automatically determine the encoding.  A transfer from Git to Nog to Git
will re-create identical Git trees.

`meta` could be used to encode additional implicit Git information, such as the
file mode.

Nog names such as `.noggit*` and subtrees `.nog` may be forbidden to reserve
them for other purposes.  Other encodings can use reserved names to store
information such as Nog meta.  In principle, the native encoding could convert
any Git tree to Nog.  But automatic encodings may use indicators in the Git
tree to automatically determine the encoding.  It seems to be a reasonable
precaution to protect those indicators to avoid potential confusion during
automatic encoding detection.

Examples: `enc_nat_*`.

#### Meta Git subtree encoding

The meta Git subtree encoding stores Nog meta in Git blobs in a subtree `.nog`.

```
.nog/.json  # Tree meta
.nog/x.txt.json  # Meta for x.txt
x.txt
```

`.nog` entries are omitted if `meta` is empty.  The `.nog` tree is omitted if
empty unless it is required to avoid an empty Git tree.  If so, a placeholder
`.nog/.json` is inserted.  Nog trees can be empty, but Git trees cannot.

The toplevel tree contains a file `.noggitencoding` with content `mst1\n` to
indicate that the tree uses meta subtree encoding.  `.noggitencoding` is
inserted when lowering a commit.  The tree GIR is updated as part of the commit
lowering, so that the GIR contains an indicator that the tree is a toplevel
tree.

`meta.git.encoding` is forbidden, so that meta subtree encoding cannot be
confused with native git tree encoding.

Examples: `enc_mst_*`, `enc_auto_*/*-commit*`.

#### Automatic Git encoding

Automatic encodings use heuristics to detect the real encoding and dispatch to
it.

The native Git encoding and the meta subtree encoding are constructed such that
they are mutually exclusive on the Nog and Git level.

During lifting, the real encoding can be determined by inspecting the Git tree.

During lowering, the real encoding can be determined by inspecting `meta.git`.

Example call chain how the automatic encoding dispatches to the native
encoding:

* auto lift commit calls:
* auto lift tree, which checks and inspects tree to determine the real encoding
  and then calls:
* native lift tree, which calls:
* native lift tree children.

The forwarding from auto lift tree to native lift tree is not a tail recursion.
Auto lift tree uses a separate GIR to manage the real encoding.  Code snippet:

```go
func (rec *recoder) LiftTree(
    repo recode.Repo, t *git.Oid, enc pb.Encoding, details *pb.LiftTreeDetails,
) (*pb.TreeGir, error) {
    realEnc, err := getRealTreeEncoding(repo, t)
    nid, err := repo.LiftTreeId(t, realEnc, details)
    return &pb.TreeGir{
        Id: recode.EntryWithEnc(nid, pb.Encoding_T_GIT_AUTO_NID_8ALPHA0),
        RealEnc: realEnc,
    }, nil
}

func (rec *recoder) NewTreeFromGir(
    repo recode.Repo, gir *pb.TreeGir,
) (*pb.Tree, error) {
    tree, err := repo.GetTree(recode.EntryWithEnc(gir.Id, gir.RealEnc))
    return recode.TreeWithEnc(tree, pb.Encoding_T_GIT_AUTO_NID_8ALPHA0,), nil
}
```

Related encodings must together provide coherent functionality.  Example: If
the native Git tree encoding does not check a tree to reserve names that the
automatic encoding expects to be reserved, the automatic encoding should itself
verify a tree before dispatching to the real encoding.

Examples: `enc_auto_*`.

#### Traditional Nog sha1 ids for trees and objects

Traditional Nog sha1 ids can be supported by constructing the Nog JSON
representation during lifting.  A full tree must be lifted at once, to compute
all ids recursively.  Subsequent tree traversal uses cached GIRs.

It seems acceptable to lift a full tree at once.  Large parts of it will
probably be used afterwards anyway.  It seems reasonable that a storage system
may sometimes perform operations that require work on the order of the current
data size.

But the same is not true for commits.  Traversing the entire commit history
with all attached trees just to retrieve the first commit would be unexpectedly
expensive.  See 'stored Nog sha1 commit ids' below.

Examples: `enc_*_nid_*`.

#### Git-based Nog ids

Nog ids with a scheme prefix can be constructed from Git ids as a content hash
of the scheme prefix with the Git id.  The scheme tag can be useful to manage
encoding evolution.  The tag may, for example, be used as an indicator in
automatic encoding detection heuristics.

Examples: `enc_auto_*/*-commit*`.

```go
var scheme string
if commitDate > "2018" {
    scheme = "gid2"
} else {
    scheme = "gid1"
}
gbuf, err := proto.Marshal(&pb.SchemedGid{Scheme: scheme, Gid: c[:]})
id := fmt.Sprintf("%s:%x", scheme, sha256.Sum256(gbuf))
```

A partial `XGir` can be used to compute the id content hash instead of the
separate `SchemeGid` struct.

Example, `nog-store-fso_2017-08/internal/nog/ngrepo/ngrepo.go`:

```go
func (repo *Repo) liftTreeGir(args *pb.LiftTreeArgs) (*pb.TreeGir, error) {
        // Store as few details as necessary in the id GIR, so that changing
        // encoding from beta to final does not change the GIDs.  The encoding,
        // specifically, is not stored in the id GIR.
	scheme := "gid0"
	gir := &pb.TreeGir{
		Scheme: scheme,
		Name:   args.Name,
	}
	if args.IsToplevel {
		gir.SotTopGid = args.SotTopGid[:]
		gir.IsToplevel = true
	} else {
		gir.SotStatGid = args.SotStatGid
		gir.SotShaGid = args.SotShaGid
	}

	girb, err := proto.Marshal(gir)
	if err != nil {
		return nil, err
	}
	gir.Id = &pb.Entry{
		Type: pb.EntryType_TREE,
		Enc:  args.Enc,
		Id:   fmt.Sprintf("%s:%x", scheme, sha256.Sum256(girb)),
	}

	return gir, nil
}

```

#### Stored Nog sha1 commit ids

If the client posts a Nog sha1 commit id, the id is stored in the Git commit
footer as `Nog-Meta: <sha1>`.  It is explicitly stored, so that a Nog sha1 id
can be used for the commit without traversing its ancestors.

Storing the id should not cause confusion during usual Git operations, as long
as nobody re-uses the commit message.  Storing an id in a Git commit is less
risky than storing an id in a Git tree, since manipulating trees is part of the
usual Git workflow and generally considered safe without constraints.  Re-using
commit messages, however, is not part of the usual Git workflow.  Re-using
commit messages only happens as part of Git workflows such as rebase and
cherry-pick that manipulate the Git history and are known to be risky.

Examples: `enc_auto_*/*-commit*`.

#### Commit date heuristics

See encoding evolution proof of concept.

It starts from:

```
AUTO_NID_5: NAT_NID_5
NAT_NID_5: meta must be `{}`, native git encoding.
```

It evolves to:

```
AUTO_NID_6: NAT_NID_6 + MST_NID_4
NAT_NID_6: meta must be `{git.encoding: nat0}`, like NAT_NID_0.
MST_NID_4: .noggitencoding, like MST_NID_0.
```

The default choice is based on the commit date.  If a tree requires a different
encoding, it is stored in the commit message footer.

`AUTO_NID_6` is implemented such that it is backward compatible to
`AUTO_NID_5`.

#### Alpha-beta-stable evolution

The tags, like `meta.git.encoding` and `.noggitencoding` do not directly use
the encoding enum.  Instead they are managed separately, so that the tag can
remain stable from alpha to beta to stable encodings in order to keep Nog ids
valid when switching to a more stable release.

See `enc_auto_nid_8`.

### Nog Meteor app

XXX We should implement a proof-of-concept to clarify how encodings are managed
in the Meteor app and the REST API and revise the section afterwards.

The Meteor server contacts the GRPC storage back end to retrieve Nog refs,
commits, trees, and objects.

See proof-of-concept in [nog-gitstore.js](./nog-gitstore.js) and on branch
`fuimages_nog_2016/p/noggitstore-spike@a754757fd6a8edd02175038d5603515bb0886f2e`
'Tie p/noggitstore-spike ...'.

The current package `nog-content` will be kept as is, and an alternative will
be implemented.  The alternative design will manage per-repository state across
multiple operations.  Example: The Meteor server instantiates a `Repository`
instance, which initializes a GRPC client to the back end.  Subsequent requests
can then use the same GRPC client.  The `Repository` instance could be managed
per Meteor connection or per Meteor user.  See also connection sharing in the
section on GRPC.

The Meteor application needs to manage encodings.  When a back end sent a tree,
subsequent requests for children must use the encodings that the back end has
sent.

A short-term solution to managing encodings could be a cache on the Meteor
server that maps Nog ids to encodings.  The Meteor UI would remain unmodified.
The server would add encodings from the cache if available or uses a fallback
encoding.  The solution might work well in practice if we use reasonable
defaults.

An alternative to managing encodings would be modified Meteor and REST clients
that are aware of encodings.  The encodings would be embedded in the content,
for example in `tree.entries`.  Encodings would not contribute to the Nog sha1
id, but they would be mandatory when retrieving content.

### Back end micro service operations

XXX Resource estimates for the various daemons should be added.

The design described in this section has not yet been validated with
a proof-of-concept implementation.  It should be revised after implementing
a proof of concept.

Back end service daemons in a multi-user environment rely on Unix permissions
for privilege separation.

A proxy GRPC server listens on the network.  It runs without access to the
filesystem.  Requests are proxied to per-user servers via Unix domain sockets.
The proxy may run a user `nobody` or in a container without access to the host
filesystem, probably both.

Per-user GRPC servers that expose Git repositories listen on Unix domain
sockets.  The servers run in containers that bind-mount the relevant host
filesystem directories.

Network and Unix domain sockets are both protected with TLS.  Access tokens are
verified on all servers, so that a per-user server is secure even when directly
exposed without.  We might reconsider in case we observe performance problems.
But we will not drop TLS only because operations becomes simpler without TLS.

We use JWT with RSA signatures.  Estimates: Verifying a JWT with a 2k RSA
signature should take less than 1ms CPU.  Comparable ECDSA is probably 50x
slower.

Unix domain sockets follow a naming convention like `/somedir/noggitd-<uid>`,
so that the proxy server can discover them statically.

Per-user servers are either statically configured to run permanently for all
known users, or we write a custom supervisor daemon to manage them.  If the
proxy detects a missing per-user server, the proxy would ask the supervisor to
start it.  The supervisor would instruct the Docker daemon to start
a container.  Docker would then be responsible for keeping the container
running unless the container decides to shutdown.  Shutdown could be based on
a timeout heuristic, for example a server could decide to shutdown if it has
not received a request for some time.  An alternative would be some kind of
socket activation scheme using Systemd.

Early access checks could be used as an additional security measure.  Before
the proxy would ask the supervisor to start a per-user server, for example, it
could check with an authorization daemon whether the access token may access
the desired repository at all.  The authorization daemon would probably require
privileges to check permissions on behalf of other Unix user ids.  It should,
therefore, be executed as a separate process.

S3-like access to Git LFS blobs could be managed in a similar way.  An HAProxy
instance would listen on the network and proxy requests to per-user Minio
instances via Unix domain sockets.  Assuming a naming scheme `<user>/<repo>`,
the proxy configuration should be straightforward.

The per-user daemons could either be executed as individual containers, or
instances of the same service could be grouped in a single container under an
s6 supervisor.  Individual containers allow fine grained resource control,
while grouping simplifies resource control of the service as a whole, which
might be preferable when operating many identical daemons that together provide
a service.

Groups of related containers will be managed in a pod.  A service supervisor
container will run a reconciliation loop and update the static configuration if
necessary. Example: Assuming we run per-user servers, the reconciliation loop
would observe the list of available users and start additional servers for new
user accounts, update the proxy configuration, and signal the proxies to reload
their configuration.  Such reconciliation loops should be straightforward to
implement.

### Towards a production implementation

The design will be implemented in multiple stages.  In each stage, we implement
functionality across the full stack up to the Meteor UI and the REST API.  We
may decide to not support REST initially but instead return errors.  The full
implementation should always be production ready, although the functionality
may initially be very limited.

We implement a new package, tentatively named `nog-content2`, and branch at the
top of existing subscriptions to dispatch to the new implementation.  We then
extend `nog-content2` so that it calls `nog-content` when needed and migrate
entire code paths to `nog-content2`.

Possible implementation stages, not necessary in this order:

* Read-only back end, no blobs, back end runs as single user.
* Writeable back end, no blobs.
* Read-only blobs via Git LFS + Minio.
* Writeable blobs.
* Multi-user back end with privilege separation.

We start the implementation with v0-alpha tags, like `mst0a0`, and soon drop
the alpha suffix, like `mst0`.  We will switch to `mst1` tags when we are
confident that the implementation is likely to graduate to production without
breaking compatibility.  We will not manage compatibility during the v0-to-v1
transition; all ids based on v0 tags will loose their meaning.  We will,
however, manage backward compatibility of v1 tags even if we start using them
during a beta period.

## How we introduce this

Will be added later.

## Limitations and drawbacks

Will be added later.

## Alternatives

Develop a custom daemon to watch filesystem trees to learn about changes.
Store metadata in MongoDB.  Develop custom daemons to manage immutable
attributes, duplication to SAMFS, and archival.

Leverage Git with Git LFS to track repository content on a filesystem.  Modify
Nog to use Git as a storage back end.  All essential state is in Git.  Even if
we switch off MongoDB and nog.zib.de, the data and metadata is still available
on the filesystem.

A recursive interpretation of Git commits as Nog commits would allow computing
standard Nog ids for entire Git histories.  The entire Git history would be
required to compute the Nog id.  For commits this seems problematic, since it
prevents us from using shallow clones.  For trees it could be feasible, since
we want to always keep entire trees anyway.

The idea to represent all encoding information as an id prefix is too simple.
Traditional Nog ids can be represented in different ways in Git.  Additional
information beyond the id is needed to specify the encoding.  The problem is
similar to Unicode decoding.  The encoding, like `UTF-8`, must be available in
addition to a byte stream.

## Future work

Questions that will not be answered here.

The design assumes that we have an identity service to manage JWT access
tokens.  Details need to be specified elsewhere.  CoreOS Dex is a candidate for
operating an identity service.  A future version of Vault may include a secret
back end to manage JWT and could then be an interesting option, too.  Dex might
be more suitable for a user-facing login service via ZEDAT LDAP.  Vault might
be more suitable for machine-generated tokens, for example tokens that grant
restricted short-term access for a compute job to a specific repository.

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen.
* v1, 2017-08-21
* 2017-08-21: Added second proof-of-concept `nog-store-fso_2017-08`.
* 2017-08-21: A partial GIR can be used to compute content hash ids.
* 2017-08-21: `LiftX(args)` now takes a single `args` struct.
* 2017-08-06: Minor polishing.
* 2017-02-15: Substantial update to describe nogstorage2 proof of concept.
* 2017-01-09: First notes.
