# NOE-15 -- Towards UI Version 2
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: retired, v1, 2018-01-25

NOE-15 contains ideas that had been initially used in nog-app.  But we later
decided to develop a separate application nog-app-2 instead.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-15 describes the transition strategy from repos that uses MongoDB and S3 to
filesystem observer repos with the goal of eventually retiring the old
approach.

## Motivation

We decided in fall 2016 to modify Nog to be useful with traditional
filesystems.  [NOE-13](./../noe-13/noe-13-git-fso.md) describes the design of
filesystem observer repositories.

We need a strategy to retire the old approach that uses MongoDB with S3 and
replace it with the new approach that uses filesystem observers.  New users
should only see the new approach.  Legacy users should have time to transition
from the old to the new approach.  Eventually, the old approach will be
disabled.

## Design

The general strategy is to add a UI version 2 that is fully React-based, using
a suffix `V2` if necessary to distinguish it from the existing UI.  The UI
version 2 will be mounted at `/v2` until it is powerful enough to replace the
UI version 1.

Route names are parameterized as needed, so that links point only to routes of
the same version.  The different UI versions must not be mixed, because
navigating between them would leave an orphan view of the other version behind.

UI version 2 should only use ECMAScript.

UI version 2 may use Blaze components during the transition period.  Ideally,
all Blaze would eventually be replaced by React.

UI version 2 uses a completely separate toplevel layout, so that it is
independent of UI version 2.  See `apps/nog-app/meteor/imports/ui-v2/` for the
toplevel UI version 2 entry points.

## How we introduce this

We initially mount UI version 2 at `/v2`, allowing expert users to switch
between version 1 and 2.

When version 2 is fully functional, we mount it at `/` and mount version 1 at
`/v1`, allowing legacy users to switch back to version 1.  New users will only
see UI version 2.

## Limitations

The transition period may create some confusion.  But it seems unavoidable.

## Alternatives

We could integrate new features incrementally.  It might be more convenient for
users.  But it would probably be harder to maintain, and the transition would
taker longer.  We prefer the more aggressive approach.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

How to get rid completely of the old functionality?  How to retire old data?
When to switch off S3?

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* 2019-10-28: retired
* v1, 2018-01-25
* 2018-01-25: Initial version
