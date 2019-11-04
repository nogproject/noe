# NOE-5 -- Service-to-Service JWT Auth
By Steffen Prohaska
<!--@@VERSIONINC@@-->

## Status

Status: frozen, v1, 2017-12-21

2019-10-28: NOE-5 contains ideas that are actively used in Nog FSO.

See [CHANGELOG](#changelog) at end of document.

## Summary

This NOE describes how we use JWT for service-to-service communication.

## Motivation

NOE-4 states that JWT will be used for auth between services, but it leaves the
details unspecified.

This NOE describes a design for operating services that use JWT for
service-to-service auth.

## Design

We use nested JWTs with per-service authorization logic.

### RFCs

The primary RFCs are:

* JWS: <https://www.rfc-editor.org/info/rfc7515>
* JWT: <https://www.rfc-editor.org/info/rfc7519>

The following RFCs may also be relevant:

* JWE: <https://www.rfc-editor.org/info/rfc7516>
* Full list of JOSE: <https://tools.ietf.org/wg/jose/>

### Nested JWTs

Nested JWTs seem to be a useful building block.  For the general idea, see
figure below and the presentation 'microXchg 2017 - Will Tran: Beyond OAuth2
– end to end microservice security', <https://youtu.be/G7A6ftCbVQY?t=33m19s>,
<https://youtu.be/G7A6ftCbVQY?t=34m31s>,
<https://youtu.be/G7A6ftCbVQY?t=29m11s>,
<https://github.com/pivotal-cf-experimental/spring-cloud-services-security>.

<img src="./nested-jwt.png" width="800"/> \
*Nested JWT example from Will Tran's presentation.*

The Nog Meteor app, for example, would receive a user identity token from
a user identity service.  It would wrap the user identity token into
a service-to-service token and pass it to a Git back end.  The Git back end
would first verify that Nog app is asking for access and then unwrap the
identity token to retrieve, for example, the numeric LDAP Unix id.  A request
may only proceed if the back end successfully verified the outer and inner JWT.
Authorization decisions may be based on the service-to-service call chain.

RSA-signed JWTs have a size of 500--1000 bytes, depending on the fields that
are stored in the JWT.  The size after nesting a few levels should still be
reasonable.

### Key infrastructure using Vault

When using nested JWTs, every service must be able to sign tokens and verify
tokens of other services.  Every service should have a separate signing key
that is regularly replaced.  Services need to retrieve certificates of services
whose signatures they want to trust.

We use Vault to build a public key infrastructure (PKI), similar to the general
idea that is described in Tommy Murphy's blog post 'Using Vault as
a Certificate Authority for Kubernetes'
<https://www.digitalocean.com/company/blog/vault-and-kubernetes/>.

Services contact Vault via HTTPS using a private CA.  Vault is assumed to be
a trusted source of keys and certificates.  Services receive an access token
that provides limited access to Vault.  Access policies allow services to issue
certificates for their role and read the public certificates for other services
that they want to trust.

The subsections below describe two alternatives for JWT signing key
distribution and verification.  The preferred alternative is the JWS x5c
header, because it requires fewer continuously operating services.

#### Alternative x5c: key certificate chain

Services that issue JWTs include the RSA signing key certificate in the JWS x5c
header.

A service that verifies a JWT first verifies the signing key with a list of
root CA certificates and elements from the certificate.  There could, for
example, be a rule that the JWT issuer must equal the certificate OU or must be
contained in the certificate SAN.  The service then verifies the JWT signature
with the verified signing key.

The CA is regularly replaced, using the following general procedure:

 - A new root CA certificate is added.
 - All RSA signing keys are replaced with keys that are signed by the new CA.
 - The old root CA certificate is removed.

This alternative requires no continuously running servers.  Key rotation would
be part of regular operations tasks.

The following shell command can be used to format an x5c element; example in
RFC 7515, Appendix B, <https://tools.ietf.org/html/rfc7515#appendix-B>:

```bash
pbpaste | base64 --decode | openssl x509 -inform DER -noout -text
```

#### Alternative x5t: key discovery by id

Each service uses a support container to run two reconciliation loops.  The
first loop maintains a private key that the service uses to sign JWTs.  The key
is regularly renewed.  The corresponding public certs are published to Vault,
so that other services can retrieve them.  A new key is activated with a delay
to give other services a chance to update their list of certificates before the
key becomes active:

```
repeat every x seconds:
  if key recent enough:
    continue
  issue key to temporary directory
  publish new key
  sleep activation delay
  activate new key
```

The second loop observes lists of certificates of other services and retrieves
the public certificates:

```
repeat every y seconds:
  foreach observed service:
    foreach published cert:
      if cert stored locally:
        continue
      if cert expired:
        continue
      retrieve cert
```

The timings must be configured such that certificates are retrieved before
a new key is activated.

When a reconciliation loop changes the configuration, it signals the primary
service daemons to reload, ideally without service interruption.

We use the JWS `x5t` header to communicate key ids between services.

The reconciliation loops use Unix operations that allow atomic updates in order
to avoid race conditions.  Specifically, related files, such as a private key
and its certificate, are stored into a fresh directory and not renamed
afterwards.  The active directory is configured by moving a symlink atomically
in place.  Service daemons are expected to use `realpath()` to resolve the
symlink first and then load related files using the absolute, immutable
directory path.  Similarly, new certificates are written to temporary files and
then moved to their final location.

See stdrepo `fuimages_stdcloud_2016-01/modules/simple` for a proof-of-concept
implementation.  The proof of concept is implemented in Bash.  We may consider
re-implementing it in Python or Go.  We should also consider running the
reconciliation loops as a non-root user.  Specifically, the following
directories contain the implementation that is used with the Meteor app and Go
noggit implementations below:

```
fuimages_stdcloud_2016-01/modules/simple/images/examplesup
fuimages_stdcloud_2016-01/modules/simple/jobs/nogstoragespike
```

Example of directory structure as maintained by the reconciliation loops:

XXX The key dir naming convention should probably be changed to `x5t`.

```
 # Active key:
/example/ssl/keys/exampled -> 5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18
/example/ssl/keys/5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18/key.pem
/example/ssl/keys/5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18/renewAfter
/example/ssl/keys/5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18/cert.pem
/example/ssl/keys/5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18/x5t.txt
/example/ssl/keys/5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18/ca.pem
/example/ssl/keys/5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18/x5t-s256.txt
/example/ssl/keys/5b-fa-b4-9c-9c-fc-c9-03-17-44-d7-88-b5-93-65-be-6f-62-c5-18/serial.txt

 # Retired key:
/example/ssl/keys/2e-96-6a-4c-ea-1a-1d-67-50-4d-92-ba-6d-19-8d-99-de-7b-86-1d/key.pem
/example/ssl/keys/2e-96-6a-4c-ea-1a-1d-67-50-4d-92-ba-6d-19-8d-99-de-7b-86-1d/renewAfter
/example/ssl/keys/2e-96-6a-4c-ea-1a-1d-67-50-4d-92-ba-6d-19-8d-99-de-7b-86-1d/cert.pem
/example/ssl/keys/2e-96-6a-4c-ea-1a-1d-67-50-4d-92-ba-6d-19-8d-99-de-7b-86-1d/x5t.txt
/example/ssl/keys/2e-96-6a-4c-ea-1a-1d-67-50-4d-92-ba-6d-19-8d-99-de-7b-86-1d/ca.pem
/example/ssl/keys/2e-96-6a-4c-ea-1a-1d-67-50-4d-92-ba-6d-19-8d-99-de-7b-86-1d/x5t-s256.txt
/example/ssl/keys/2e-96-6a-4c-ea-1a-1d-67-50-4d-92-ba-6d-19-8d-99-de-7b-86-1d/serial.txt


 # Certificates, including expired certs that are still in the list.  They are
 # marked as ignored on the filesystem.
/example/ssl/trust/exampled/YE3nDl6xi0A34SMQQp02xXkdECI.crt.pem
/example/ssl/trust/exampled/yWbCbIBFI7jDviZMTrCK9yRd_Fo.crt.pem
/example/ssl/trust/exampled/CK7FRRaQ-R4DpqIRtTh90NiRX4Y.ignore
/example/ssl/trust/exampled/VELH0f86wNjEB0XyYbuNQYAAG_U.ignore
```

Example Vault list of certificates and example entry:

```
$ vault list x/darkdisney/devspr/example-pki-2017-02-certs/exampled
Keys
----
mrZUqM-YORJSHF5gjAV2pcUfmIA
qk1Y0W4EvHVGcdpI84TsLh8qKZk
zZFzZhoVpN9aq_fD3giLsm6Q-7A

$ vault read x/darkdisney/devspr/example-pki-2017-02-certs/exampled/qk1Y0W4EvHVGcdpI84TsLh8qKZk
Key                 Value
---                 -----
refresh_interval    360h0m0s
issuedAt            1487768850
notAfter            1491404849
path                x/darkdisney/devspr/example-pki-2017-02/cert/5b:fa:b4:9c:9c:fc:c9:03:17:44:d7:88:b5:93:65:be:6f:62:c5:18
serial              5b:fa:b4:9c:9c:fc:c9:03:17:44:d7:88:b5:93:65:be:6f:62:c5:18
x5t                 qk1Y0W4EvHVGcdpI84TsLh8qKZk
x5t_s256            Ntig_-jz8G5EVLsj4RSYM6iCpz2a9qUtNVzMcwjd-Y4
```

Example Vault policy to issue a certificate and publish it to a certificate
list:

```
path "x/darkdisney/devspr/example-pki-2017-02/issue/exampled" {
    capabilities = ["update"]
}
path "x/darkdisney/devspr/example-pki-2017-02-certs/exampled/*" {
    capabilities = ["create"]
}
```

Example Vault policy to observe a certificate list:

```
path "x/darkdisney/devspr/example-pki-2017-02-certs/exampled/" {
    capabilities = ["list"]
}
path "x/darkdisney/devspr/example-pki-2017-02-certs/exampled/*" {
    capabilities = ["read"]
}
```

Commands to compute `x5t` and `x5t#S256` JWS headers.  For x5t, see RFC 7515
Section 4.1.7 <https://tools.ietf.org/html/rfc7515#section-4.1.7>.  For
base64url, see RFC 4648 Section 5,
<https://tools.ietf.org/html/rfc4648#section-5>.  We remove padding, since the
data size is implicitly known, although RFC 7515 does not explicitly state
whether padding should be used or not.

```bash
 # x5t
openssl x509 -in "${tmpCert}" -outform der | openssl dgst -sha1 -binary \
| openssl base64 | tr '+/' '-_' | tr -d '=' >"${tmpdir}/x5t.txt"

 # x5t#S256
openssl x509 -in "${tmpCert}" -outform der | openssl dgst -sha256 -binary \
| openssl base64 | tr '+/' '-_' | tr -d '=' >"${tmpdir}/x5t-s256.txt"
```

### JWT in Meteor

A proof of concept has been implemented in Meteor; see
`fuimages_nog_2016/p/noggitstore-spike@68b6db883a6a2c293c9def22bc1abb445d16d865`
'Tie p/noggitstore-spike: DO NOT MERGE, JWT signing code'.

The proof of concept uses x5t key id headers.  Some modifications are required
for x5c key certificate headers.

The Npm package `jsonwebtoken`, <https://github.com/auth0/node-jsonwebtoken>,
is used to create nested JWTs as if an identity token had been received from
a separate identity service:

```javascript
import jwt from 'jsonwebtoken';
import fs from 'fs';

// `realpath()` resolves symlinks to avoid a race condition with background key
// renewal.  The production implementation would reload on SIGHUP.
function readKey(dir) {
  const d = fs.realpathSync(dir);
  return {
    kid: fs.readFileSync(`${d}/x5t.txt`, { encoding: 'utf8'}).trim(),
    key: fs.readFileSync(`${d}/key.pem`),
  }
}

const dex = readKey('/pki/dex/jwt/keys/dex/');
const nogapp = readKey('/pki/nogapp/jwt/keys/nogapp/');

const userToken = jwt.sign(
  {
    xrlm: 'zedat',  // Unix realm.
    xuid: 10000,  // Unix user id in Unix realm.
  },
  dex.key,
  {
    algorithm: 'RS256',
    expiresIn: '30d',
    issuer: 'dex',
    subject: 'bob@zedat',  // nog.zib.de user id.
    audience: ['nogapp', 'noggit'],
    header: { x5t: dex.kid },
  }
);

const callToken = jwt.sign(
  {
    jwt: userToken,
    op: 'Get*',
  },
  nogapp.key,
  {
    algorithm: 'RS256',
    expiresIn: '1h',
    issuer: 'nogapp',
    audience: ['noggit'],
    header: { x5t: nogapp.kid },
  }
);

// Use the token in GRPC calls, similar to:
const metadata = new grpc.Metadata();
metadata.add('authorization', callToken);
const jwtCreds = gcreds.createFromMetadataGenerator((srv, cb) => {
  cb(null, metadata);
});
const creds = gcreds.combineChannelCredentials(tlsCreds, jwtCreds);
```

The identity token is valid on the order of weeks.  The RPC token has a short
validity on the order of minutes or hours.  The details will be determined as
part of the production implementation.

The identity service provides information that can be used to check Unix
permissions.  The tentative idea is to use a combination of `xrlm` for 'Unix
realm' with `xuid` for 'Unix user id'.  Unix realms provide the scope for the
numeric user ids, such as 'ZEDAT' vs 'ZIB'.  A back end can check whether the
realm matches before using the numeric user id.

We probably have to operate separate Dex servers to independently map ZEDAT
LDAP and ZIB LDAP accounts.  The Meteor app would offer options 'Login with
ZEDAT' and 'Login with ZIB'.

### JWT in Go GRPC

A proof of concept of nested JWT auth has been implemented in:

```
fuimages_nog-internal-research_2016/nogstorage_2017-02/pkg/jwt
fuimages_nog-internal-research_2016/nogstorage_2017-02/cmd/noggitproxyd
fuimages_nog-internal-research_2016/nogstorage_2017-02/cmd/noggitstored2
```

The proof of concept uses x5t key id headers.  Some modifications are required
for x5c key certificate headers.

`jwt` managed keys.  `noggitproxyd` validates JWTs and uses `xuid` to determine
the backend.  `noggitstored2` only contains a stub to illustrate where
validation would be implemented.  See example code below.

Token validation logic will be implemented in the individual services as
needed.  We will not create a general framework for validation.  Services use
specific logic and implement just what they need.  They should verify issue,
audience and service-specific claims on several nesting levels.  Details will
be determined as part of the production implementation.

We may over time factor out common code and group it into a JWT validation
support library.

Error handling details have been redacted from the Go example code below.
A production implementation would have to handle all `errs` and `oks`.

`jwt.LoadKeys()` loads a directory of trusted keys:

```go
// Production code would handle the errs.
func LoadKeys(dir string) (*Certs, error) {
    realdir, err := filepath.EvalSymlinks(dir)
    files, err := ioutil.ReadDir(realdir)
    keys := make(map[string]*rsa.PublicKey)
    for _, f := range files {
        name := f.Name()
        if !strings.HasSuffix(name, ".crt.pem") {
            continue
        }
        path := filepath.Join(realdir, name)
        data, err := ioutil.ReadFile(path)
        key, err := jwtgo.ParseRSAPublicKeyFromPEM(data)
        x5t := strings.Split(name, ".")[0]
        keys[x5t] = key
    }
    return &Certs{keys}, nil
}
```

The server groups several directories of trusted keys to verify nested JWTs.
The authorization header is received via a context.  `validateToken()` verifies
that the token contains the expected nesting structure and claims.  Certs are
chosen from the cert directories based on the `x5t` JWT header:

```go
type Trust struct {
    dex    Certs
    nogapp Certs
}

type server struct {
    trust     *nogjwt.Trust
    upstreams map[string]pb.NogStoreReaderClient
}

func (srv *server) GetCommit(
    ctx context.Context, req *pb.GetCommitRequest,
) (*pb.GetCommitResponse, error) {
    uid, err := srv.validateToken(ctx)
    c, err := srv.getUpstream(uid)
    return c.GetCommit(ctx, req)
}

func (srv *server) validateToken(ctx context.Context) (int64, error) {
    md, ok := metadata.FromContext(ctx)
    auth, ok := md["authorization"]

    nogapp := jwt.MapClaims{}
    _, err := jwt.ParseWithClaims(auth[0], &nogapp, srv.withNogappCerts())
    if !nogapp.VerifyIssuer("nogapp", true) { }
    if !audienceContains(nogapp, "noggit") { }

    dexAuth, _ := nogapp["jwt"].(string)
    dex := jwt.MapClaims{}
    _, err = jwt.ParseWithClaims(dexAuth, &dex, srv.withDexCerts())
    if !dex.VerifyIssuer("dex", true) { }
    if !audienceContains(dex, "noggit") { }
    if !claimStringEquals(dex, "xrlm", "zedat") { }

    xuid, ok := dex["xuid"].(float64)
    return int64(xuid), nil
}

func (srv *server) withNogappCerts() func(*jwt.Token) (interface{}, error) {
    return newFindCert(srv.trust.GetNogappCerts())
}

func (srv *server) withDexCerts() func(*jwt.Token) (interface{}, error) {
    return newFindCert(srv.trust.GetDexCerts())
}

func newFindCert(certs *nogjwt.Certs) func(*jwt.Token) (interface{}, error) {
    return func(t *jwt.Token) (interface{}, error) {
        if _, ok := t.Method.(*jwt.SigningMethodRSA); !ok { }
        x5t, ok := t.Header["x5t"]
        id, ok := x5t.(string)
        cert := certs.Get(id)
        if cert == nil { }
        return cert, nil
    }
}
```

The following Go code illustrates how to parse an x5c element; example in RFC
7515, Appendix B, <https://tools.ietf.org/html/rfc7515#appendix-B>:

```go
// ```
// pbpaste | base64 --decode | go run parse-cert.go
// ```

package main

import (
    "crypto/x509"
    "fmt"
    "io/ioutil"
    "os"
)

func main() {
    data, _ := ioutil.ReadAll(os.Stdin)
    cert, _ := x509.ParseCertificate(data)
    fmt.Println("issuer:", cert.Issuer)
    fmt.Println("subject:", cert.Subject)
    // cert.verify() // https://golang.org/pkg/crypto/x509/#Certificate.Verify
}
```

## How we teach this

Intentionally left empty.

## Limitations

There are no obvious limitations.  The system seems flexible.  We should be
able to add service-specific authorization logic as needed.

## Alternatives

The directory naming convention for signing keys should probably be changed to
use `x5t` instead of dashed certificate serials.

Keys with a long validity could reduce the burden of key rotation.  We decided
against a long validity, since it create a false sense of simplicity.  Instead,
all production services must implement SIGHUP reloading of credentials, so that
key rotation can be fully automated.

Without JWT nesting, services could directly use the identity tokens and would
only need the certificates of the central identity service for token
verification.  Intermediate services would not need private signing keys.
Certificate distribution could be simplified.  A potential drawback is that
token verification logic has less information.  Implementing specific trust
choices might be difficult.

It would also be unclear how to manage short-lived tokens for
machine-to-machine communication.  Since we decided to implement automated key
distribution, we can as well assume that every service can receive a private
key and issue short-lived JWTs without contacting another service.  Although
key distribution is a burden, the actual JWT signing and verification is
straightforward and can implement service-specific decision that depend on the
service-to-service call chain.  We expect that specific decision will be useful
in the future, for example when implementing restricted access tokens for
compute jobs.

Instead of distributing private signing keys to every service, services could
contact another service to issue machine-to-machine tokens.  A potential
security advantage is that secret keys would be distributed to fewer locations.
But services would depend on the central signing service, which may negatively
impact reliability and availability.  At this point we prefer key distribution
so that services can issue JWTs independently if they have valid signing keys.
Signing key rotation should be infrequent enough (every couple of weeks), so
that services can operate independently for some time without contact to the
central PKI service.  Frequent key rotation, on the other hand, increases
security.  We will review our decisions as part of regular operations.

## Unresolved questions

*The questions in this section seem relevant but will not be answered in this
NOE.  They are left for future work.*

The details of an identity service for users is not addressed in this NOE.  The
only assumption is that such an identity service will provide JWTs that can be
used to determine information about users that is relevant for determining file
system access, such as their Unix id.  CoreOS Dex is a potential option to
create a user identity service.

## CHANGELOG

* 2019-10-28: frozen
* v1, 2017-12-21: New PKI alternative x5c, which is now preferred
* v0, 2017-03-03: First complete draft
* 2017-02-22: Started document
