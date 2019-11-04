# NOE-1 -- The Nog Evolution Process
By Steffen Prohaska
<!--@@VERSIONINC@@-->

Status: active, v4, 2019-10-28

## What is a NOE?

NOE stands for Nog Evolution Proposal.

The purpose of a NOE is to discuss and document Nog design decisions.  A NOE
consists of a main text and may contain a number of related files, such as
supplementary internal information or example implementations.

## Why do we use NOEs?

Using a process to documenting design decisions is considered best practice in
several communities.  The documentation can be helpful when trying to
understand the system in the future.  It can also be useful when reconsidering
decisions.

Examples of similar processes:

 - IETF RFCs, <https://en.wikipedia.org/wiki/Request_for_Comments>
 - Python PEP, <https://www.python.org/dev/peps/pep-0001/>
 - Swift Evolution, <https://github.com/apple/swift-evolution>
 - Kubernetes Proposals,
   <https://github.com/kubernetes/kubernetes/tree/master/docs/proposals>
 - Rust RFCs, <https://github.com/rust-lang/rfcs>
 - Ember RFCs, <https://github.com/emberjs/rfcs>

## Structure of a NOE

Each NOE is maintained as a separate directory with a main file `noe-#-*.md`
and supplementary files.  Example:

```
noe-9999/noe-9999-abbrev-title.md
noe-9999/some-suppl-information.md
noe-9999/impl.py
```

NOEs have a status:

* `active` NOEs may be modified.
* `frozen` NOEs are immutable.  Frozen NOEs may only be amended with small
  notes, for example a reference to a newer NOE with updated information.
* `postponed` may be used in place of `active` to indicate that the NOE needs
  more work, but it is unclear when the work will continue.
* `retired` NOEs are no longer relevant.

NOEs that document a design decision are usually marked `frozen` when the
discussion has settled and the design has been implemented.

Other NOEs may remain `active` for a long time, some forever.  An obvious
example is the NOE index (NOE-0).

Some form of version information should be used:

* The Git commit of the repository: It is simple and suitable for identifying
  the NOE version.  But the version may change more frequently than necessary
  due to unrelated changes in other parts of the repo.
* A manually maintained document version combined with a release date is a way
  to provide a more stable version, which may be useful for mature documents.
  If in doubt, use a simple version counter starting with `v1` and indicate
  upcoming releases by a placeholder date followed by `(unreleased)`.
  Examples: `Status: active, v1, 2016-10-01`; `Status: active, v2, 2016-11-xx
  (unreleased)`.

A changelog seems useful, too.

A NOE may be split into multiple documents. The main text should be relevant
for the openly available Nog code base, for example design decisions.
Supplementary information may be relevant only for a restricted audience, for
example a detailed description of the local deployment configuration, including
internal naming conventions.

## How to start a NOE?

The Ember RFC template
<https://github.com/emberjs/rfcs/blob/master/0000-template.md> seems to be
a good starting point to provide a general document structure.

Consider our adaption of the Ember template as a starting point:
[noe-x-template](./noe-x-template.md).

## Changelog

* v4, 2019-10-28: New status `retired`
* 2019-07-24: Fixed typos
* v3, 2018-12-19: Switched to Markdown H1 title
* v2, 2017-07-31: Changed status values to `active`, `frozen`, `postponed`
* v1, 2017-07-29
* 2017-07-16: Switched to v1-based release counting
* 2017-01-09: Added noe-x-template
* v0, 2016-12-05: Initial description of the process
