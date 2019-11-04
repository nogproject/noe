# NOE-16 -- Auto-Suggestion Metadata Model Inspired by Wikidata
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1.0.1, 2019-11-01

NOE-16 contains preliminary ideas.  Its status, however, is frozen instead of
retired, because the ideas seem potentially relevant for future work.

See [CHANGELOG](#changelog) at end of document.

## Summary

NOE-16 describes the data model that we use for metadata auto-suggestions.  The
data model is inspired by Wikidata, specifically properties and items.

## Motivation

Nog metadata is loosely typed, similar to JSON, see NOE-13:

```
<field_name>: <JSON-value>
...
```

See also:

* [NOE-3](./../noe-3/noe-3-data-catalog-structures.md) -- Data Structures for
  Catalogs, Protocols, and Journals
* [NOE-13](./../noe-13/noe-13-git-fso.md) -- Git Filesystem Observer

The hypothesis is that auto-suggestion during data entry will guide users
towards established metadata conventions and will be sufficient to ensure
useful metadata in practice.  A question then is how to generate good
auto-suggestions.

This NOE-16 describes a data model that is inspired by Wikidata.  Their
approach has proved to be useful in practice.  The hope is that their ideas
will be useful for Nog, too, and by using a similar data model, it will be
straightforward to use information from Wikidata for high-quality
auto-suggestions, for example a list of genes.

## Design

The data model is inspired by Wikidata.  See introduction to Wikidata,
<https://www.wikidata.org/wiki/Wikidata:Introduction>, in particular items and
properties.

### Translation of Wikidata terms to Nog terms

Items and properties:

* Wikidata label: Nog symbol.  A Nog symbol's primary purpose is to be
  machine-readable.  We use the first Nog name as the human-readable
  correspondence to a Wikidata label.
* Wikidata identifier: Nog ID.  See section on UUIDs below.
* Wikidata aliases: Nog names.  Nog names usually include the Wikidata label as
  the first entry followed by the Wikidata aliases.
* Wikidata description: Nog description

Properties:

* Wikidata statement about example uses of a property: Nog examples.

Property types:

* Wikidata property datatype: Nog `suggestValues`, which specifies an
  auto-suggestion algorithm, together with Nog `suggestValuesParams`, which
  specify parameters of the algorithm
* Wikidata property datatype item with a statement about its type: Nog suggest
  `TypedItem` with parameter `ofType`
* Wikidata property datatype quantity with a statement about units to be used:
  Nog suggest `Quantity` with parameter `units`

Items that represents a class of values, for example "human (Q5)", are used
similarly in Wikidata and Nog.  Same for units and items that represent values
of a certain type.

Nog uses the following terms that have no direct correspondence in the visible
Wikidata model:

* Nog name tokens, `nameTokens`, or `tokens`, depending on context: lowercase
  words that can be used for prefix auto-suggestion.  Name tokens are usually
  generated from names by splitting on whitespace and ignoring irrelevant
  strings like "the", "of", or numbers.

### Properties and property types

We use properties to represent two aspects of metadata fields:

* Properties are used when suggesting new metadata fields.
* Property types are used when suggesting values when entering metadata values.

Properties are used in a way that is similar to Wikidata's use of properties to
establish meaning.  They are stored in a MongoDB collection `mdProperties`.
The relevant information is illustrated with an example:

```javascript
{
  id: '<derived from symbol, see section on UUIDs>',
  symbol: 'keywords',
  names: ['Keywords', 'Topics'],
  nameTokens: [
    'keywords',
    'topics',
  ],
  description: (
    'Keywords can be used to associate search terms with any content.'
  ),
  examples: [
    'cellular orientation',
  ],
}
```

Auto-suggestion of new metadata fields uses a prefix search on `nameTokens` to
compile a list of suggestions.

Property types are used like Wikidata property datatypes to specify which type
of values should be used in metadata fields.  They are stored in a MongoDB
collection `mdPropertyTypes`.  The relevant information is illustrated with
examples:

Enum-like values, items of a certain type; see algorithm below:

```javascript
{
  id: '<derived from symbol, see section on UUIDs>',
  symbol: 'keywords'
  suggestValues: 'TypedItem',
  suggestValuesParams: {
    ofType: ['$knownThis'],
  },
  knownValues: [
    'amira',
    'bcpfs',
    'fluoromath',
    'hand',
    'release',
  ],
},
```

Quantity, number with unit; see algorithm below:

```javascript
{
  id: 'P2049', // See section on UUIDs.
  symbol: 'width',
  suggestValues: 'Quantity',
  suggestValuesParams: {
    units: [
      'Q11573', // `m` Wikidata UUID from `id`.
      'Q175821', // `um` Wikidata UUID from `id`.
      'Q178674', // `nm` Wikidata UUID from `id`.
    ],
  },
}
```

Auto-suggestion of metadata values searches for suitable property types by
matching the metadata field name to the property type symbol.  It then applies
the `suggestValues` algorithm to compile a list of suggestions.

The `TypedItem` algorithm uses items, which stored in the MongoDB collection
`mdItems`.  Items may be manually curated, like:

```javascript
{
  id: '<derived from symbol, see section on UUIDs>',
  symbol: 'Steffen Prohaska',
  names: [
    'Steffen Prohaska',
    'spr',
  ],
  description: (
    'Steffen Prohaska is a researcher at ZIB.'
  ),
  ofType: [
    'Q5',
  ],
  tokens: [
    'steffen',
    'prohaska',
    'spr',
  ],
}
```

Items may also be automatically derived from values that have been used before,
like:

```javascript
{
  id: '...',
  symbol: 'alice',
  names: [
    'alice'
  ],
  description: '\'alice\' has been used in \'keywords\' before.',
  ofType: [
    'jX4WWn1qX3KlUJKQSuU1YA', // see UUIDs.
  ],
  tokens: [
    'alice'
  ],
}
```

The `Quantity` algorithm suggests numbers followed by on of the units that are
specified in the property type `suggestValuesParams.units`.

### Property and item UUIDs

Nog uses sha1-based name v5 UUIDs as property and item IDs.  The IDs are
base64url-encoded for MongoDB and Meteor.  MongoDB could use binary IDs, but
Meteor requires strings.

UUIDs are derived using several UUID namespaces and conventions:

XXX See `fuimages_nog_2016/packages/nog-suggest/uuid.js` for now.  Some details
should be copied to here before the final NOE.

* A UUID namespace for IDs from Wikidata `Q` and `P` identifiers
* A UUID namespace for property IDs from a namespace like `/sys/md/org` and
  a symbol like `keywords`
* A UUID namespace for item IDs from a namespace and symbol
* A UUID namespace for IDs that represent the class of known values that have
  been used before with a property
* A UUID namespace for item IDs that are derived from a property id and
  a symbol.  This is used for item IDs that represent values that have been
  used with a property before.

This approach allows us to use unified IDs for information from Wikidata and
for internal information, managing naming details in the UUID convention and
namespace.  Using context heuristics, Wikidata identifier like `Q5` and
internal identifiers like `keywords` in namespace `/sys/md/org` can both be
automatically converted to a UUID.

### Curated properties and items

Fixed property and items are imported from information that is stored
externally.

See `packages/nog-suggest/fixed-md-data.js`, attached as supplementary
information, for an early proof of concept.

See `nog-internal-research_fuimages_2017/suggest-maintainer_2018-02` for the
initial approach to maintain information in an FSO repo.

### Learning suggestions values from catalogs

The content of catalogs is used to infer property types and known values that
are from then used for auto-suggestion.  See
`packages/nog-suggest/known-values.js`.

Briefly, the search terms of the catalog are analyzed:

* If a field mostly contains numbers, it uses auto-suggestion `Quantity`.
* If a field mostly contains strings, it uses auto-suggestion `TypedItem`.
  Items are automatically generated for values that have been used with the
  property before.

The details can be tweaked using a fixed table of known property types.  See
`packages/nog-suggest/fixed-property-types.js`.

### Namespaces for definitions and suggestions

We use two namespaces to control access: one for property and item definitions
and one for property types that are enabled for suggestions.

The namespace for definitions is called `mdns`.  Adding a property or item to
the database requires permission to access action `sys/write` aka
`AA_SYS_WRITE` with a path:

* `/sys/md/wikidata`: Defines Wikidata properties and items
* `/sys/md/g/<group>`: Defines properties and items with group scope
* `/sys/md/u/<user>`: Defines properties and items with user scope

The namespace to enable suggestions is called `sugns`.  Enabling a property
type requires `sys/read` aka `AA_SYS_READ` to the `mdns` namespace that
contains the definition and `sys/write` aka `AA_SYS_WRITE` to the `sugns`
suggestion namespace.  Example:

* `sys/read` aka `AA_SYS_READ` from `/sys/md/wikidata` and `/sys/md/g/org`
* `sys/write` aka `AA_SYS_WRITE` to `/sys/sug/g/org`

Example MongoDB docs from `mdPropertyTypes`:

```json
{
  "_id" : "PVEIHTY3XjiP5SsGK76Hrw",
  "symbol" : "keywords",
  "suggest" : "TypedItem",
  "suggestParams" : {
    "ofType" : [
      "jX4WWn1qX3KlUJKQSuU1YA"
    ]
  },
  "mdns" : "/sys/md/g/visual",
  "sugnss" : [
    "/sys/sug/g/visual"
  ]
}

{
  "_id" : "VHYK3lD-W6Sx859WiSyxmg",
  "symbol" : "width",
  "suggest" : "Quantity",
  "suggestParams" : {
    "units" : [
      "tBEFa7jYWW-AW6diPbYc0g",
      "REadRYQPUrSWYvSvEzkpew",
      "V1QxElnDUA6HW3XIGb_-6g"
    ]
  },
  "mdns" : "/sys/md/wikidata",
  "sugnss" : [
    "/sys/sug/g/visual",
    "/sys/sug/default"
  ]
}
```

`mdns` and `sugnss` work in the same way for `mdProperties`, `mdPropertyTypes`,
and `mdItems`.

When a client requests data for auto-suggestion, it requests a list `sugnss` of
suggestion namespaces.  `checkAccess` verifies `AA_SYS_READ` access to the
`sugnss` namespaces.  MongoDB selection operators are then conditioned on
`sugnss`.  The enabled suggestion namespaces are configured for user groups
using a mechanism similar to the home screen links.

The same general mechanism is used when deriving properties from catalogs.  The
catalog maintainer who executes the operation must have the requires
`AA_SYS_READ` and `AA_SYS_WRITE` permissions.

### Storing fixed suggestion metadata in FSO repos

Fixed suggestion metadata can be stored in FSO repos, from where admins can
import it into MongoDB, so that it becomes active.

The data format stored in FSO repos corresponds to the data format in
`fixed-md-data.js`; see supplementary information.

See `nog-internal-research_fuimages_2017/suggest-maintainer_2018-02` for the
initial approach to maintain information in an FSO repo.

Example import command:

```javascript
NogSuggest.callApplyFixedMdFromRepo({
  repo: '/nog/lib/fixed-md-sug',
  mdnss: ['/sys/md/g/visual'],
}, console.log)

NogSuggest.callApplyFixedSuggestionNamespacesFromRepo({
  repo: '/nog/lib/fixed-md-sug',
  mdnss: ['/sys/md/g/visual'],
  sugnss: ['/sys/sug/g/visual'],
}, console.log)
```

Example FSO repo `master-meta` content:

```
# fluoromath/specimen_patient.prop
description: "Specimen Patient ID identifies a patient."
examples: ["2488"]
id: "$nogScopeSymbolPropertyId"
mdns: "/sys/md/g/visual"
names: ["Specimen Patient ID","Patient ID"]
suggestValues: "TypedItem"
suggestValuesParams: {"ofType":["$knownThis"]}
symbol: "specimen_patient"
type: "Property"

# fluoromath/specimen_patient.sugns
nog_fixed_suggestion_namespace: [{"id":"$nogScopeSymbolPropertyId","mdns":"/sys/md/g/visual","op":"EnableProperty","sugns":"/sys/sug/g/visual","symbol":"specimen_patient"},{"id":"$nogScopeSymbolPropertyId","mdns":"/sys/md/g/visual","op":"EnablePropertyType","suggestFromMdnss":["/sys/md/g/visual"],"sugns":"/sys/sug/g/visual","symbol":"specimen_patient"}]

# fluoromath/specimen_siteid.prop
description: "Specimen Site ID identifies a site that processed a specimen."
examples: ["2271"]
id: "$nogScopeSymbolPropertyId"
mdns: "/sys/md/g/visual"
names: ["Specimen Site ID","Site ID"]
suggestValues: "TypedItem"
suggestValuesParams: {"ofType":["$knownThis"]}
symbol: "specimen_siteid"
type: "Property"

# fluoromath/specimen_siteid.sugns
nog_fixed_suggestion_namespace: [{"id":"$nogScopeSymbolPropertyId","mdns":"/sys/md/g/visual","op":"EnableProperty","sugns":"/sys/sug/g/visual","symbol":"specimen_siteid"},{"id":"$nogScopeSymbolPropertyId","mdns":"/sys/md/g/visual","op":"EnablePropertyType","suggestFromMdnss":["/sys/md/g/visual"],"sugns":"/sys/sug/g/visual","symbol":"specimen_siteid"}]
```

## How we introduce this

We start with a small list of hard-coded properties and items and try the
approach for Visual catalogs.  We reconsider how to proceed when we have
gathered some practical experience.

## Limitations

The namespace approach in may not scale to a large number of items, which may
result from large catalogs.  If we observe scalability issues, we will consider
a different namespace partitioning approach.  We could move large namespaces
into separate collections, similar to the approach we use with catalogs.

## Alternatives

Deliberately left empty.

## Future work

The following questions seem relevant but will not be answered in this NOE.
They are left for future work.

Establish a process to continuously maintain and evolve context-dependent
metadata schemas.

How to import information from external sources, such as Wikidata?

How to manage per-user namespaces?

## Supplementary information

### Proof of concept for storing suggestion metadata in FSO repo

See `nog-internal-research_fuimages_2017/suggest-maintainer_2018-02/` for
a proof of concept for storing fixed suggestion metadata in FSO repo.  It is
based on the data structures in `fixed-md-data.js`, see below.

References:

 * `fuimages_nog-internal-research_2017@71459039d239c8db95ca6d2f533f01fde20f5927`
   2018-02-12 'suggest-maintainer: proof of concept how to maintain suggestion
   metadata'

### fixed-md-data.js

`fuimages_nog_2016/packages/nog-suggest/fixed-md-data.js`:

```javascript
// `fixedMd` contains properties and items in a serialization format that could
// be stored, for example, in an fso repo.  Nog app would load it from there
// and insert corresponding docs into MongoDB.
const fixedMd = [
  {
    type: 'Property',
    // NogScopeSymbolProperty UUID from `mdns` and `symbol`.
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'keywords',
    names: ['Keywords', 'Topics'],
    nameTokens: [ // optional, default is `$tokensFromNames`.
      'keywords',
      'topics',
    ],
    description: (
      'Keywords can be used to associate search terms with any content.'
    ),
    examples: [
      'cellular orientation',
    ],
    suggestValues: 'TypedItem',
    suggestValuesParams: {
      ofType: ['$knownThis'],
    },
    knownValues: [
      'amira',
      'bcpfs',
      'fluoromath',
      'hand',
      'release',
    ],
  },

  {
    type: 'Property',
    id: '$nogSymbolPropertyId',
    symbol: 'author',
    // No `names` indicates that it is a partial property that is used when
    // suggesting values but not when adding a metadata field.
    suggestValues: 'TypedItem',
    suggestValuesParams: {
      ofType: [
        'Q5',
        '$knownThis',
      ],
    },
    knownValues: [
      'Steffen Prohaska',
      'Uli Homberg',
      'Marc Osterland',
    ],
  },

  {
    type: 'Property',
    id: '$nogSymbolPropertyId',
    symbol: 'doi',
    names: ['DOI', 'Digital Object Identifier'],
    description: (
      'DOI is a character string that is used as a permanent identifier for ' +
      'a digital object, in a format controlled by the International DOI' +
      'Foundation.'
    ),
    examples: [
      'http://dx.doi.org/10.1007/s11440-014-0308-1',
    ],
  },

  {
    type: 'Property',
    id: '$nogSymbolPropertyId',
    symbol: 'opus_url',
    names: ['OPUS URL'],
    description: (
      'OPUS URL points to a ZIB publication in OPUS.'
    ),
    examples: [
      'https://opus4.kobv.de/opus4-zib/frontdoor/index/index/docId/4397',
    ],
  },

  {
    type: 'Property',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'tags',
    names: ['Tags', 'Labels'],
    description: (
      'Tags are strings without space that can be used to search content. ' +
      'To define a tag, simply start using it, similar to a hashtag.'
    ),
    suggestValues: 'TypedItem',
    suggestValuesParams: {
      ofType: ['$knownThis'],
    },
    examples: [
      '1247Bp4',
      'MyProject',
    ],
  },

  {
    type: 'Property',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'specimen_siteid',
    names: ['Specimen Site ID', 'Site ID'],
    description: (
      'Specimen Site ID identifies the site that processed a specimen.'
    ),
    suggestValues: 'TypedItem',
    suggestValuesParams: {
      ofType: ['$knownThis'],
    },
    examples: [
      '2271',
    ],
  },

  {
    type: 'Property',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'specimen_patient',
    names: ['Specimen Patient ID', 'Patient ID'],
    description: (
      'Specimen Patient ID identifies the patient.'
    ),
    suggestValues: 'TypedItem',
    suggestValuesParams: {
      ofType: ['$knownThis'],
    },
    examples: [
      '2488',
    ],
  },

  {
    type: 'Property',
    id: '$nogSymbolPropertyId',
    symbol: 'topics',
    names: ['Topics'],
    description: (
      'Topics are added by catalog maintainers to group catalog entries.'
    ),
    examples: [
      'publication',
      'repo',
      'video',
    ],
  },

  {
    type: 'Property',
    id: '$nogSymbolPropertyId',
    symbol: 'imaging_date',
    names: [
      'Imaging Date',
      'Date of Imaging',
      'Acquisition Date',
      'Date of Image Acquisition',
    ],
    description: (
      'Imaging Date is the date when image data was acquired. ' +
      'It should be specified in ISO format.'
    ),
    examples: [
      '2015-12-29',
    ],
  },

  {
    type: 'Property',
    id: 'P2049', // Wikidata UUID from symbol.
    symbol: 'width',
    names: ['Width'],
    nameTokens: ['$tokensFromNames'],
    description: 'width of an object',
    examples: ['10 nm'],
    suggestValues: 'Quantity',
    suggestValuesParams: {
      units: [
        'Q11573', // `m` Wikidata UUID from `id`.
        'Q175821', // `um` Wikidata UUID from `id`.
        'Q178674', // `nm` Wikidata UUID from `id`.
      ],
    },
  },

  // The Wikidata item 'human (Q5)`.
  {
    type: 'Item',
    id: 'Q5',
    symbol: 'human',
    names: [
      'human',
      'person',
    ],
    description: (
      'An individual human being.'
    ),
  },

  // Similar to Wikidata `Quantity (Q29934271)`.
  {
    type: 'Item',
    id: '$nogSymbolItemId',
    symbol: 'Quantity',
    names: [
      'Quantity',
    ],
    description: (
      'Quantity is the property datatype that indicates that ' +
      'the value should be a number with a unit.'
    ),
  },

  // See
  // <https://www.wikidata.org/wiki/Wikidata:Units#Length_(length_(Q36253))>
  {
    type: 'Item',
    id: 'Q11573', // Wikidata UUID from `id`.
    symbol: 'm',
    names: [
      'metre',
      'm',
      'meter',
      'meters',
      'metres',
    ],
    description: 'SI unit of length',
  },
  {
    type: 'Item',
    id: 'Q175821',
    symbol: 'um',
    names: [
      'micrometre',
      'micrometer',
      'µm',
      'micron',
      'um',
    ],
    description: 'one millionth of a metre',
  },
  {
    type: 'Item',
    id: 'Q178674',
    symbol: 'nm',
    names: [
      'nanometre',
      'nm',
      'nanometer',
    ],
    description: 'unit of length',
  },

  {
    type: 'Item',
    id: '$nogScopeSymbolItemId',
    mdns: '/sys/md/g/visual',
    symbol: 'Steffen Prohaska',
    names: [
      'Steffen Prohaska',
      'spr',
    ],
    description: (
      'Steffen Prohaska is a researcher at ZIB.'
    ),
    ofType: [
      'Q5',
    ],
  },

  {
    type: 'Item',
    id: '$nogScopeSymbolItemId',
    mdns: '/sys/md/g/visual',
    symbol: 'Uli Homberg',
    names: [
      'Uli Homberg',
      'Ulrike Homberg',
      'uho',
    ],
    description: (
      'Uli Homberg is a researcher at ZIB.'
    ),
    ofType: [
      'Q5',
    ],
  },

];

const fixedSuggestionNamespaces = [

  {
    op: 'EnableProperty',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'keywords',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: '$nogSymbolPropertyId',
    symbol: 'doi',
    mdns: '/sys/md/nog',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: '$nogSymbolPropertyId',
    symbol: 'opus_url',
    mdns: '/sys/md/nog',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'tags',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'specimen_siteid',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'specimen_patient',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: '$nogSymbolPropertyId',
    symbol: 'topics',
    mdns: '/sys/md/nog',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: '$nogSymbolPropertyId',
    symbol: 'imaging_date',
    mdns: '/sys/md/nog',
    sugns: '/sys/sug/g/visual',
  },

  {
    op: 'EnableProperty',
    id: 'P2049', // width
    mdns: '/sys/md/wikidata',
    sugns: '/sys/sug/default',
  },

  {
    op: 'EnablePropertyType',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'keywords',
    sugns: '/sys/sug/g/visual',
    suggestFromMdnss: [
      '/sys/md/wikidata',
      '/sys/md/g/visual',
    ],
  },

  {
    op: 'EnablePropertyType',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'specimen_siteid',
    sugns: '/sys/sug/g/visual',
    suggestFromMdnss: [
      '/sys/md/g/visual',
    ],
  },

  {
    op: 'EnablePropertyType',
    id: '$nogScopeSymbolPropertyId',
    mdns: '/sys/md/g/visual',
    symbol: 'specimen_patient',
    sugns: '/sys/sug/g/visual',
    suggestFromMdnss: [
      '/sys/md/g/visual',
    ],
  },

  {
    op: 'EnablePropertyType',
    id: '$nogSymbolPropertyId',
    symbol: 'author',
    sugns: '/sys/sug/g/visual',
    suggestFromMdnss: [
      '/sys/md/wikidata',
      '/sys/md/nog',
      '/sys/md/g/visual',
    ],
  },

  {
    op: 'EnablePropertyType',
    id: 'P2049',
    sugns: '/sys/sug/default',
    suggestFromMdnss: [
      '/sys/md/wikidata',
    ],
  },

];

export {
  fixedMd,
  fixedSuggestionNamespaces,
};
```

### fixed-property-types.js

`fuimages_nog_2016/packages/nog-suggest/fixed-property-types.js`:

```javascript
import {
  uuidNogScopeSymbolProperty,
  uuidNogSymbolProperty,
  uuidWikidata,
} from './uuid.js';

// `fixedPropertyTypes` contains properties that have been defined elsewhere,
// such as in `./fixed-md-data.js`.
//
// XXX It should perhaps be derived from information that is stored in
// collections, so that fixed types can be maintained in external storage, such
// as fso repos, and used when discovering known values from catalogs.
const fixedPropertyTypes = new Map();

function insert(mdNamespace, typ) {
  let nsMap = fixedPropertyTypes.get(mdNamespace);
  if (!nsMap) {
    nsMap = new Map();
    fixedPropertyTypes.set(mdNamespace, nsMap);
  }
  nsMap.set(typ.symbol, typ);
}

function findFixedPropertyType(mdNamespace, symbol) {
  const ns = fixedPropertyTypes.get(mdNamespace);
  if (!ns) {
    return null;
  }
  return ns.get(symbol);
}

// The property is used and defined in the namespace.
function defNogScopeSymbolProperty(mdNamespace, symbol) {
  insert(mdNamespace, {
    symbol,
    id: uuidNogScopeSymbolProperty(mdNamespace, symbol),
    displayName: symbol,
  });
}

// The property is used in the namespace but defined without namespace.
function defNogSymbolProperty(mdNamespace, symbol) {
  insert(mdNamespace, {
    symbol,
    id: uuidNogSymbolProperty(symbol),
    displayName: symbol,
  });
}

// The property is used in the namespace but defined as Wikidata.
function defWikidataProperty(mdNamespace, symbol, id) {
  insert(mdNamespace, {
    symbol,
    id: uuidWikidata(id),
    displayName: `${symbol} (${id})`,
  });
}

defNogScopeSymbolProperty('/sys/md/g/visual', 'keywords');
defNogSymbolProperty('/sys/md/g/visual', 'author');
defNogScopeSymbolProperty('/sys/md/g/visual', 'tags');
defWikidataProperty('/sys/md/g/visual', 'width', 'P2049');

export {
  findFixedPropertyType,
};
```

## CHANGELOG

<!--
Changelog format:
* YYYY-MM-DD: subject
* v1, YYYY-MM-DD: subject
* YYYY-MM-DD: subject
-->

* v1.0.1, 2019-11-01: polishing
* v1, 2019-10-28: frozen
* 2018-02-19: Polishing
* 2018-02-13: How to store fixed suggestion metadata in FSO repos
* 2018-02-08: Initial version
