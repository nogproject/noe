# NOE-3 -- Data Structures for Catalogs, Protocols, and Journals (redacted)
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2019-10-28

This is a redacted document.  The full document is
[noe-3-data-catalog-structures](./../../../noe-sup/noes/noe-3/noe-3-data-catalog-structures.md)
in the supplementary repo.  It can be made available upon request.

NOE-3 contains preliminary ideas.  Its status, however, is frozen instead of
retired, because the ideas seem potentially relevant for future work.

See [CHANGELOG](#changelog) at end of document.

## Summary

This NOE describes data structures around catalogs, protocols and journals, so
that different implementations can be developed and used.  The NOE also
contains practical recommendations for metadata naming.

The document does not describe use cases in detail.  It mentions use cases only
where necessary to explain the data structures.

## Motivation

We have developed a rough understanding of the general workflow for building
a data catalog:

 - Storing metadata on a subset of objects and trees.
 - Applying `nog-catalog` to scan the repos and create a metadata index.

The details, however, varied between a series of case studies.

This document has the following goals:

 - to describe minimal requirements on the data structures to achieve
   compatibility between implementations;
 - to describe variants that proved to be useful in order to guide further
   discussion;
 - to provide guidance how to choose metadata field names.

Solutions should be immediately useful at ZIB and provide perspectives for
future extension to cooperation partners.  Data acquisition and management for
BioSupraMol is of particular interest, including the related data analysis
collaboration projects.

## Design

### Metadata in Nog, data on the filesystem

We decided that Nog will support local filesystem-based data storage.  We must
not assume anymore that all data will be in object storage.

Implementations should manage primarily metadata in Nog and leave data on the
filesystem, in particular if the data is large.  It is acceptable to store
small data representations, like thumbnails, in object storage.

### Workflow strategies

We have been exploring two main strategies to managing metadata:

metadata-crud: Metadata is directly created and updated on trees and objects.

activity-journal: Metadata is added in a structured process.  The update
activity is represented in a journal.  The activity can be guided by
a protocol.

#### Case studies

The following is a complete list of case studies.  Some details are discussed
in the subsections below.

 - ...REDACTED...

#### Examples for metadata-crud

Marc's scripts, see ...REDACTED...

#### Examples for activity-journal

`nogstd` with protocols, ...REDACTED...

### Metadata types and general conventions

See also `nog-catalog/README.md`.

Metadata on the original entries may be of JSON type `String`, `Number`,
`[String]`, or `[Number]`.  Metadata of other types will be ignored.

The naming scheme is inspired by Prometheus's naming convention, see
<https://prometheus.io/docs/practices/naming/#metric-names>:

 - Metadata keys should have a (single-word) scope prefix.  Examples:
   `imaging_`, `experiment_`, or `processing_`.
 - Metadata keys should use a suffix to specify units, using American spelling
   in plural with prefixes in a single word.  Examples: `_meters`,
   `_micrometers`, `_angstroms`.
 - Related entries should use the same prefix for each unit dimension, for
   example avoid using both angstroms and nanometers or micrometers and
   nanometers in related imaging protocols.
 - Metadata keys should use suffixes to indicate a grouping of vector
   quantities.  Examples: `imaging_pixel_size_x_micrometers`,
   `imaging_pixel_size_y_micrometers`, and
   `imaging_slice_thickness_micrometers`; or `imaging_voxel_size_x_angstroms`,
   `imaging_voxel_size_y_angstroms`, and `imaging_voxel_size_z_angstroms`.

See [practical-metadata-naming](#practical-metadata-naming) for specific
examples.

### Multi-tooling

It should be possible to use different tools for managing metadata and easily
add custom scripts when needed.

Tools should converge towards common data structures as described in this
document.  Initially, all tools are expected to respect at least the minimal
data structures.  Over time, tools hopefully move towards a common set of
extended data structures.

Tools should be designed such that they can be combined with other tools.
Tools, for example, should leave unknown metadata unmodified unless they are
explicitly told to remove it.

Sharing common code in a library seems useful.  But it is not part of the NOE.
The purpose of the NOE is to describe the data structures, so that tools could
be developed independently, without sharing code.

### The Journal

#### Journal tree

The journal is a toplevel tree named `journal` of kind `journal`.  Example:

```
/journal/ {
  meta: {
    "journal": {}
  }
}
```

Journal entries are children of the `journal` tree.  Youngest children come
first.  Example:

```
/journal/stdcatalog-protocol-2016-12-05T170217Z/
/journal/nogstd-import-data-protocol-2016-11-24T164133Z/
```

#### Minimal journal entry

Every tool must understand the minimal structure.

Journal entries are trees.  They are identified as journal entries by their
location in a journal tree.

Journal entries have no specific kind to identify them as such.  There was no
consensus whether entries should have such a specific kind.

The entry name should be unique within the journal.  `<protocol>@<ISO-UTC>`
could be a reasonable scheme.  The `@` is a bit problematic, however, since it
causes ugly URLs that require percent-encoding.  A restriction to letters,
digits, and hyphen, as in the traditional DNS LDH rule, seems reasonable:
`<protocol>-<ISO-UTC>` would then be an obvious naming scheme.

The entry name is not intended to be directly presented to humans.  A UI should
apply some kind of formatting.  Journal entries should contain metadata that
are useful in a UI.  The details are unspecified.

A journal entry tree can have children.  A child object `note.md` is
interpreted similar to a commit message in git: it is, for example, displayed
by the journal view as the summary of the journal entry.  Example:

```
/journal/stdcatalog-protocol-2016-12-05T170217Z/ {}
/journal/stdcatalog-protocol-2016-12-05T170217Z/note.md {
  text: "journal entry message."
}
```

`nogup` creates minimal journal entries.

#### Extended journal entry

The extended structures are optional.  Tools should gracefully handle unknown
details and converge towards a common data structure over time.

##### Journal entry for protocol activities

A journal entry can be of kind `protocol` to indicate that the activity
followed a structured process as documented in a protocol. We use the same
kind, `protocol`, for generic protocol descriptions and for specific activities
that followed a protocol.  `protocol` indicates that both are related and use
a data structure that is flexible enough to represent both.  Whether an entry
is a generic description or a specific activity must be inferred from the
context.  See [protocols and activities](#protocols-and-activities) below for
details.

Journal entries should not use kind `protocol` if they describe something that
was not not based on a protocol.

`protocol.props`, if present, is a map of metadata that has been recorded as
part of the activity.  `protocol.props` are displayed by the journal view.

The first child can be a tree that contains a generic protocol package as
stored in a registry.  If so, the journal entry should also be of kind
`package` to indicate that the nested package structure that we use for
programs applies here, too.  See protocols and activities.  The analogy is not
perfect and should perhaps be reconsidered.  For programs, usually only the
latest version matters.  Journal entries may update previous entries, in which
case only the latest activity for a protocol matters.  But they may also add
information, so that it is not obvious that only the latest activity for
a protocol matters.

`protocol.affected` maybe used to refer to data entries whose metadata has been
affected by the journal entry.  `protocol.affected` is a list of name content
ref objects.  The details need to be clarified.

##### Journal entry seal date

`journal_seal_date`, an ISO UTC date time, indicates that the entry has been
sealed and should not be modified anymore.

##### Extended journal entry example

```
/journal/stdcatalog-protocol-2016-12-05T170217Z/ {
  meta: {
    "journal_seal_date": "2016-12-05T17:07:13Z",
    "package": {
      "description": "Activity based on protocol `stdcatalog-protocol`.",
      "name": "sprohaska/REDACTED/stdcatalog-protocol"
      "version": {
        "date": "2016-12-05T17:02:17Z"
      }
    },
    "protocol": {
      "affected": [
        {
          "path": "fs/stdrepolist",
          "sha": "0d9737e1c765c637e665fa796820f499228151ae"
        }
      ],
      "props": {
        "nog_path": [
          "fs/stdrepolist"
        ],
        "nog_stage": "sealed",
        "std_maintainer": "Steffen Prohaska"
      }
    }
  }
}
/journal/stdcatalog-protocol-2016-12-05T170217Z/stdcatalog-protocol@0.2.0-0/ {
  See protocol structure.
}
/journal/stdcatalog-protocol-2016-12-05T170217Z/note.md {
  text: "..."
}
```

### Protocols and activities

The goal of protocols is to structure procedures and the capturing of related
metadata.  Protocols are prototypes for activities.  Protocols can be used to
create journal entries, which represent specific activities.  A protocol
contains general descriptions of procedures and supporting details, such as
a schema for metadata that should be captured.  A journal entry, on the other
hand, contains the information for one specific activity.  The activity can be
based on a protocol.  But it should also be possible to start an activity from
scratch.  Common themes from individual activities could be abstracted into
a protocol.

We use the same kind for generic protocols and for specific activities.
`protocol` only indicates that the data structures are related.  The details
must be inferred from the context.  The terms generic protocol and protocol
activity are sometimes used for clarity.

#### Generic protocol package

Nogpm has been extended to better support protocol packages.  The usual
conventions should be used for `nogpackage.json`.

`dependencies` should be empty.  Dependency handling for protocols is
undefined.

The package must be of kind `protocol` and use `protocolRegistry`.

Content that has a specific meaning during the use of the protocol is specified
as `objects` and will be stored as unpacked Nog entries.

Example `nogpackage.json`:

```
{
  "package": {
    "authors": [
      {
        "email": "prohaska@zib.de",
        "name": "Steffen Prohaska"
      }
    ],
    "dependencies": [
    ],
    "description": "Protocol for importing data into a stdrepo via a nogrepo",
    "name": "nogstd-import-data-protocol",
    "readme": "README.md",
    "version": {
      "major": 0,
      "minor": 5,
      "patch": 0,
      "prerelease": null
    },
    "content": {
      "objects": [
        {
          "text": "protocol.md",
          "meta": "protocol.md.meta.yml"
        },
        { "text": "script.js" }
      ]
    }
  },
  "protocol": {
  },
  "protocolRegistry": "sprohaska/nogpackages"
}
```

#### Protocol registry

A protocol registry is a Nog repo.  The root tree must have kind
`protocolRegistry` and contain a subtree `protocols` of kind `protocols`.

The only way to create a protocol registry is currently to start from a file
repo and use the technical view to create the subtree and add the kinds.

#### protocol.md with propsSchema

This variant has been used in `nogstd-import-data-protocol`.

A child object `protocol.md` of a protocol package is a human-readable text
that describes the procedure.

`protocol.md.meta.yml` specifies a schema for metadata.  Example:

```yaml
protocol:
  propsSchema:
    - key: zib_importer
      description: Person at ZIB who is responsible for the imported data.
```

The information for a specific activity is gathered in a journal entry in
`protocol.props`.  `props` may or may not follow the `propsSchema` of the
embedded protocol.  Example:

```
/journal/some-protocol-2016-12-05T170217Z/ {
  meta: {
    ...
    "protocol": {
      ...
      "props": {
        "zib_importer": "Steffen Prohaska"
      }
    }
  }
}
```

#### script.js

This variant has been used in `nogstd-import-data-protocol`.

`script.js` contains JavsScript that determines how the `props` that are
recorded in an activity are applied to data entries.  `script.apply({ props })`
receives the `props` and returns an array of Mongo-style `[selector, modifier]`
pairs how to modify entries.  See protocols nogstd-import-data-protocol and
nogstd-tooling for details.

The idea is that a general purpose scripting hook can be used to write a wide
range of specific protocols without touching the core of the protocol and
activity implementation.  `script.js` could be safely executed in a dedicated
Nodejs VM in the future.

#### Protocol activity as journal entry with nested protocol package

This variant has been used in `nogstd-import-data-protocol`.

A journal entry that represents an activity that is based on a protocol
contains the generic protocol as the first child.  Example:

```
/journal/stdcatalog-protocol-2016-12-05T170217Z/stdcatalog-protocol@0.2.0-0/ {
  Content of the protocol package.
}
```

The information from the journal entry subtree is flattened into an object that
represents the activity.  The process of flattening is called linking, like
linking a program from object files.  Another analogy is following the
prototype links until a property is found.  A tentative heuristic for linking
has been implemented in `nogstd-tooling/nogstd/nogprotocol.py`:

 - Merge `props` such that the closest to the root wins.
 - Concatenate the `propsSchema` list by starting from root and appending
   children, so that schemas closer to the root win.

#### Mustache rendering of protocol.md

This idea has been used in `import-zib-coop-data-protocol`.

`protocol.md` is rendered through Mustache with a data context that contains
`props` and related information.  This approach could be used to display
detailed journal entries that render the protocol text with inline information
from the specific activity.

#### Protocol status

This variant has been used in `nogstd-import-data-protocol`.

The status of generic protocol packages and procol activities in journal
entries is maintained in `nog_stage`:

 - `generic`: A generic protocol.
 - `active`: An active journal entry that may be modified.
 - `sealed`: A sealed journal entry, usually with a `journal_seal_date`.

Such a status could be used in the future to enforce immutability of sealed
journal entries.

### Practical metadata naming

This section contains a collection of metadata that has been used in the case
studies and a few initial recommendations.

The presentation should get more structured over time and provide guidance in
form of best practices, good practices, emerging practices, and deprecated
naming and suggestions what to use instead.

This document contains only an initial collection of ideas.  Updated
recommendations will be maintained in protocols or in separate NOEs.

Protocol `zib-visual-metadata-guide` contains the guide for ZIB Visual;
...REDACTED...

#### Example catalogs

Our catalogs:

 - ...REDACTED...

Example catalogs:

 - ...REDACTED...

#### Deprecated metadata

Simple keys as in Marc's case studies should be avoided.  Use longer names with
a scope prefix instead.

#### Metadata best practices

It seems too early to describe best practices.

#### Metadata good practices

Use a scope prefix.

#### Metadata emerging practices

Review metadata used by others.  Use existing schemas if they fit.  Feel free
to invent new schemas if necessary.

See case studies for specific ideas.

Document metadata fields as a schema.

#### Metadata case studies

Metadata from Marc's first repos:  It does not follow the Prometheus-like
naming scheme.

```
propsSchema:
  - key: Study
    description: Subproject of 'project'.
    examples:
      - Collective Migration

  - key: celltype
    description: Cell types used in the assay.
    examples:
      - myoblasts

  - key: lens
    description: Lens description, e.g. magnification.
    examples:
      - 5x

  - key: material
    description: Surface material of the assay.
    examples:
      - plastic

  - key: mutation
    description: Subtypes of the celltype from above.
    examples:
      - deltaK32

  - key: partner
    description: External coop partner.
    examples:
      - ...REDACTED...

  - key: project
    description: Name of the ZIB project.
    examples:
      - ...REDACTED...

  - key: research_focus
    description: Keywords describing the goal of the assay.
    examples:
      - cellular migration

  - key: staining
    description: Specific information for LM, e.g. channel description.
    examples:
      - brightfield
```

Import of Marc's data by Steffen:

```
propsSchema:
  - key: import_date
    description: ISO date of import w/o time.
    examples:
      - '2016-11-02'

  - key: import_importer
    description: Name of importer.
    examples:
      - Steffen Prohaska

  - key: import_origin
    description: Some kind of reference to the original source.
    examples:
      - nog.zib.de/REDACTED
```

Metadata from Steffen's stdcatalog case study.  Examples:

```
...REDACTED...
```

Metadata schema from Vincent's ...REDACTED...

```
...REDACTED...
```

Metadata from Uli's first repos:

```
...REDACTED...
```

## How we teach this

We will demonstrate the design by applying it to create data catalogs that are
used internally at ZIB.  Early adaptors are expected to be familiar with
`nog.py`.  Researchers at ZIB should start from protocols and potentially
adjust scripts to create something useful.

We will polish the design and create further protocols that are useful in
practice.

We will decide further details when we have a first UI.

## Limitations and drawbacks

The data structures are only loosely described.  The NOE may give too little
practical guidance to ensure convergence of implementations.

The focus of the NOE is on data structures.  It provides little help for the
actual implementation.  This is a deliberate choice to leave room for further
experimentation.

## Alternatives

### Journal entry naming

Since journal entries are not intended to be directly presented to humans, they
could be named using some kind of content address.  The name cannot be the sha
of the journal entry, because the sha includes the name.  A naming scheme based
on protocol and time to encode some semantic meaning seemed reasonable.

### Different kinds for protocols and activities

A single kind `protocol` is used for generic protocol descriptions and for
specific activities.  Different kinds could be used to distinguish the
different aspects.  We decided to use a single kind to indicate that the
generic description and the specific activities are two aspects of the same
data structure.  This is similar to the use of kinds `package` and `program`,
which are also used for generic aspects in registries and specific aspects in
workspaces.

### Grouping journal entries by status

We decided against grouping journal entries into subtrees by status.  Status
changes of journal entries should not change their location.

## Unresolved questions

The questions in this section will not be answered in this NOE.  But they
probably need to be answered in the future.

* How to handle a large number of journal entries?  A simple linear list of
  journal entries may become problematic.  It should be relatively easy to
  switch to a hierarchical structure, such as one subtree per year, later.

* It feels a bit dangerous to use relatively simple names for kinds, such as
  `protocol`.  The names could collide with ordinary metadata.  Maybe we should
  introduce a naming convention that clearly indicates kind objects.  Ideas:
  Use a prefix and a special separator that must not be used for ordinary
  metadata field names.  Maybe `<kindNamespace>:<kind>`, example:
  `nog:protocol`.  Such a naming convention might cause some tricky encoding
  issues in the short term but could be less error-prone in the long term.
  A more elaborate scheme could include a version to indicate maturity, similar
  to Kubernetes's API versioning
  <https://github.com/kubernetes/kubernetes/blob/master/docs/api.md#api-versioning>,
  like `kind:v<x><maturity>:<kind>`, example: `kind:v1alpha1:protocol`.

* We were not completely happy with using the same kind, `protocol`, for
  generic protocol descriptions and specific protocol activities.  Furthermore,
  the analogy between protocol activities with nested protocol packages and
  workspace programs with nested program packages might be misleading: the
  effect of activities can be the union of the individual effects, while for
  programs usually only the effect of the latest version matters.

* The details of linking of nested protocol packages should perhaps be
  specified more precisely.

* A possible UI is not described in detail here.  It will be addressed
  separately in the future.

* Where to share common code?

## LOG

...REDACTED...

## CHANGELOG

* v1, 2019-10-28
*  ...REDACTED...
* v0, 2016-12-20: initial version.
