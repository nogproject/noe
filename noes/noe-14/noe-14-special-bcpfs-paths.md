# NOE-14 -- Special BCPFS Paths
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2017-08-29

2019-10-28: NOE-14 contains ideas that are actively used on BCPFS.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-14 describes an approach to handling special situations that require paths
that do not fit into the general filesystem naming scheme.

Special symlinks are explicitly declared in `bcpfs.hcl`.  `bcpfs-perms apply`
creates them.  `bcpfs-perms check` verifies them.  They must be manually
removed.

## Motivation

Facility managers have asked to make microscope documentation available as
a toplevel directory `guides/` in a microscope share.  The request seems
reasonable.  But it does not fit into the general filesystem naming concept.
We want to be able to create ad hoc solutions if we believe that they are
relevant but do not yet see a general pattern that justifies developing a new
general mechanism.

## Design

`bcpfs.hcl` is modified to specify a list of explicit symlinks.  Example:

```
symlink {
    target = "../../fake-facility/service/guides"
    path = "srv/fake-tem/guides"
}

symlink {
    target = "../../fake-facility/service/guides"
    path = "srv/fake-analysis/guides"
}
```

`bcpfs-perms apply` creates symlinks.  `bcpfs-perms check` verifies symlinks.
Symlinks must be manually removed.

Special symlinks should be used carefully to avoid unexpected side effects with
admin commands, for example commands that propagate permissions.  A few
symlinks seem acceptable.

## How we introduce this

We use the mechanism if users explicitly request special solutions.  But we do
not advertise the option.

We document the purpose of special symlinks in a comment in `bcpfs.hcl`.

## Limitations

The design is deliberately limited to symlinks to reduce the risk, although
more general special paths may seem useful in some situations.

## Alternatives

We could do nothing and simply reject requests for special paths, asking users
to find a solution within the current naming concept.  This alternative seems
too unfriendly.

We could allow more general special paths, like directories.  `bcpfs-perms`
could use path patterns as a more flexible mechanism to ignore special paths.
But it seems risky that flexible paths may cause unexpected side effects with
admin scripts.  It seems safer to manage a specific solution that forces us to
explicitly document every special symlink.

Symlinks could be managed in a configuration management system like Salt.  But
the application managers may not have direct access to the configuration.

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: frozen
* v1, 2017-08-29
* 2017-08-29: Initial version
