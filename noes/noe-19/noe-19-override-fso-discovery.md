# NOE-19 -- User Override for FSO Discovery
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2018-09-28

2019-10-28: NOE-13 contains ideas that are actively used in Nog FSO.

For the initial implementation, see commits in the week before
`fuimages_nog_2016/next@c256992884f75e7bb4ac7972a9c3672aa0bbcf02` 2018-03-26
'Tie next: p/merge-accounts'.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-19 is an amendment to NOE-13 and NOE-18.  NOE-13 describes the general Git
filesystem observer design.  NOE-18 describes how to handle nested shadow
repos.  NOE-19 describes mechanisms that allow users to override some admin
settings for repo initialization.  The mechanism should allow us to find
a practical trade-off between centrally controlled, standardized configuration
and flexibility to handle special situations, in particular related to legacy
directory trees.

Related NOEs:

* [NOE-13](./../noe-13/noe-13-git-fso.md) -- Git Filesystem Observer
* [NOE-18](./../noe-18/noe-18-fso-nesting.md) -- Git Filesystem Observer
  Nesting

## Motivation

NOE-13 and NOE-18 describe repo discovery using a central configuration that
determines which paths are considered as repositories.  The only way for a user
to add other paths was to talk to an admin and ask for a change of the central
configuration.

In practice, we want more flexibility.  Specifically, admins should be able to
grant users limited permission to override central settings in order to add
repos for ignored paths right away without contacting an admin.

## Design

### User override to enable paths

The root repo naming `PathPatterns` is extended to handle a config field
`enabledPaths`, which contains a list of pairs `<depth> <relpath>`.  The
`enabledPaths` are logically processed before the normal `patterns`.  An
`enabledPath` match is handled as a `superrepo` rule, so that a user can either
initialize a repo at the enabled path or enable nested paths, which are
displayed as ignored below the enabled path.

The implementation simply expands the `enabledPaths` to `patterns` as follows:

```
0 foo  ->  "superrepo foo"
1 foo  ->  "superrepo foo", "superrepo foo/*"
2 foo  ->  "superrepo foo", "superrepo foo/*", "superrepo foo/*/*"
...
```

Paths can be enabled via the following GRPC:

```
service Registry {
    rpc EnableDiscoveryPaths(EnableDiscoveryPathsI) returns (EnableDiscoveryPathsO);
}

message EnableDiscoveryPathsI {
    string registry = 1;
    bytes vid = 2;
    string global_root = 3;
    // `paths` are global paths for now.  We may later add support for paths
    // relative to root.
    repeated DepthPath depth_paths = 4;
}

message DepthPath {
    int32 depth = 1;
    string path = 2;
}
```

The registry converts the command into an event that contains a list of entries
that were appended to `enabledPaths`.  Example in pseudo-protobuf JSON:

```
{
  "event": "EV_FSO_REPO_NAMING_CONFIG_UPDATED",
  "repoNaming": {
    "global_root": "/exinst/data/projects/foo",
    "rule": "PathPatterns",
    "config": {
      "enabledPaths": ["0 2011"],
    }
  }
}
```

The GUI is modified to display buttons at ignored paths to enable the path or
the path with its direct subdirectories.

Access is controlled by:

* `fso/enable-discovery-path` aka `AA_FSO_ENABLE_DISCOVERY_PATH` on the global
  root `path`

### Initialize repos with ignore-most or bundle-subdirs, switch to enter-subdirs later

If users are allowed to enable paths, the preferred way to initialize repos is
`ignore-most` in order to avoid accidentally creating massive repos.
`ignore-most` ensures that the Git tree of the shadow repo has a limited size
that is independent of the number of files in the repo toplevel.  `ignore-most`
is the default if a repo init policy is configured for a root.  As a safety
measure, the registry rejects `EnableDiscoveryPaths()` if no repo init policy
is configured.

`bundle-subdirs` may also be a reasonably init policy, assuming that users
create only a small number of regular files directly in the toplevel directory.

The registry is changed to pass the desired subdir tracking to the pre-init
limit check.

The size limit checks are disabled for `ignore-most`, because it is always safe
to track only the repo toplevel and a fixed number of files.

The size limit checks for `bundle-subdirs` and `ignore-subdirs` are initially
also disabled, because it is usually safe to track a single directory level.
We will add a new type of limit check that will refuse `bundle-subdirs` or
`ignore-subdirs` if the number of files in the repo toplevel exceed a limit.

A new GRPC allows changing the subdir tracking later:

```
service Stat {
    rpc ReinitSubdirTracking(ReinitSubdirTrackingI) returns (ReinitSubdirTrackingO);
}

message ReinitSubdirTrackingI {
    bytes repo = 1;
    string author_name = 2;
    string author_email = 3;
    SubdirTracking subdir_tracking = 4;
    JobControl job_control = 6;
}

enum JobControl {
    JC_UNSPECIFIED = 0;
    JC_WAIT = 1;
    JC_BACKGROUND = 2;
}
```

Access is controlled by:

* `fso/init-repo` aka `AA_FSO_INIT_REPO` on the global repo `path`

The original init limit checks are applied before switching to `enter-subdirs`.

`JobControl: JC_WAIT` is a new option that tells `nogfsostad` to wait for the
operation to complete and return a problem as a GRPC error.  `nog-app` uses
this mode to propagate limit violations to the GUI.

The changed limit checks allow admins to configure tighter limits.  Repos close
to the root of a filesystem may, for example, be only tracked with
`bundle-subdirs`, while repos for deeper directories may use `enter-subdirs`.

### Stat service job control

All the `Stat` GRPCs now support `JobControl: JC_WAIT` to tell `nogfsostad` to
wait for the operation to complete.  `nog-app` uses this mode to propagate
errors that happen within the deadline to the GUI.  Jobs may still complete in
the background if the GRPC is terminated after its deadline.  The GRPCs are:

```
service Stat {
    rpc Stat(StatI) returns (StatO);
    rpc Sha(ShaI) returns (ShaO);
    rpc RefreshContent(RefreshContentI) returns (RefreshContentO);
    rpc ReinitSubdirTracking(ReinitSubdirTrackingI) returns (ReinitSubdirTrackingO);
}
```

### Admin responsibility to manage enabled paths

If admins reapply the original static `PathPatterns` configuration, which does
not contain `enabledPaths`, the user-enabled paths are again disabled.  This
may be desired in some situations.  Example: a user enables a path and
initializes all the desired repos.  The enabled paths can then be disabled
again.

In general, admins should review the events that added `enabledPaths` and
decide how to update the static `PathPatterns` configuration to allow users to
initialize repos in the future.

Admins should set a repo init policy that uses `ignore-most` or
`bundle-subdirs`, to avoid accidentally creating massive repos, unless
a location is known to be very likely always safe with `enter-subdirs`.

## How we introduce this

We introduce the mechanism for a ZIB filesystem.  We limit the permission to
enable paths initially to admins and selected users and reconsider later how to
include more users.

## Limitations

Not discussed.

## Alternatives

We could add a separate access action to control who can
`ReinitSubdirTracking()` the repo subdir tracking separately from general repo
write permission.  The initial subdir tracking is controlled as part of the
root configuration.  Changing it later should perhaps require similar
privileges as controlling the root configuration.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

Should `ReinitSubdirTracking()` use a separate access action?

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v1, 2018-09-28
* 2018-09-27: Use `ignore-most` as safe default init policy
* 2018-03-26: Initial version
