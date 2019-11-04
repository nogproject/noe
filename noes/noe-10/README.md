# Build and test

Build Debian binary package `samba-vfs-disable-chmod-acl`:

```bash
cd samba-inherit-posix-acls
docker-compose build
```

Test:

```bash
docker-compose up
```

Connect to Samba `localhost:9139`, users `alice`, `bob`, `charly`, password
always `test`.  Navigate to the shares `testdata-disable-chmod` and
`testdata-inherit-posix-acls`.  Create files and check permissions with:

```bash
docker exec -it samba bash
cd /samba/...
getfacl ...
```

## Save deb

```bash
./tools/bin/save-deb
git status
git commit ...
```

## Pack supplementary information

```bash
./tools/bin/pack-suppl
```
