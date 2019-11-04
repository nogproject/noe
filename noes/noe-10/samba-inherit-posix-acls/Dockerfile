# <https://hub.docker.com/_/debian/>
FROM debian:8.9@sha256:51cd80bb935b76fbbf49640750736abc63ab7084d5331e198326b20063e7f13c

LABEL maintainer "Steffen Prohaska <prohaska@zib.de>"

# <https://github.com/krallin/tini/releases>
ENV \
    TINI_VERSION=v0.15.0

RUN set -x && \
    apt-get update && \
    apt-get -y install \
        apt-utils \
        curl \
    && \
    apt-get clean

RUN set -x && \
    curl -sSL https://github.com/krallin/tini/releases/download/${TINI_VERSION}/tini -o /tmp/tini && \
    curl -sSL https://github.com/krallin/tini/releases/download/${TINI_VERSION}/tini.asc -o /tmp/tini.asc && \
    gpg --keyserver ha.pool.sks-keyservers.net --recv-keys 6380DC428747F6C393FEACA59A84159D7001A4E5 && \
    gpg --verify /tmp/tini.asc && \
    chmod a+x /tmp/tini && \
    mv /tmp/tini /usr/local/bin/tini

# Install packages listed in Build-Depends
# `samba-${SAMBA_SRC_VERSION}/debian/control`, so that an image layer already
# contains the packages that `build-deb` is going to install.
RUN set -x && \
    apt-get update && \
    apt-get -y install \
        dpkg-dev \
        bison \
        debhelper \
        dh-systemd \
        docbook-xml \
        docbook-xsl \
        faketime \
        flex \
        libacl1-dev \
        libaio-dev \
        libarchive-dev \
        libattr1-dev \
        libblkid-dev \
        libbsd-dev \
        libcap-dev \
        libcups2-dev \
        libgnutls28-dev \
        libheimntlm0-heimdal \
        libldap2-dev \
        libldb-dev \
        libncurses5-dev \
        libntdb-dev \
        libpam0g-dev \
        libparse-yapp-perl \
        libpcap-dev \
        libpopt-dev \
        libreadline-dev \
        libsubunit-dev \
        libtalloc-dev \
        libtdb-dev \
        libtevent-dev \
        perl \
        perl-modules \
        pkg-config \
        po-debconf \
        python-all-dev \
        python-dnspython \
        python-ldb \
        python-ldb-dev \
        python-ntdb \
        python-talloc-dev \
        python-tdb \
        python-testtools \
        python3 \
        subunit \
        xsltproc \
        zlib1g-dev \
    && \
    apt-get clean

ENV \
    SAMBA_SRC_VERSION="4.2.14+dfsg" \
    SAMBA_PKG_VERSION="2:4.2.14+dfsg-0+deb8u7+b1" \
    SAMBA_PATCHED_PKG_VERSION="2:4.2.14+dfsg-0+deb8u7+bcpfs3" \
    SAMBA_PATCHED_DEB_VERSION="4.2.14+dfsg-0+deb8u7+bcpfs3"

COPY deb/ /deb/
RUN set -x && \
    cd /deb && \
    chmod 0755 build-deb && \
    ./build-deb

RUN set -x && \
    apt-get update && \
    apt-get -y install \
        heimdal-hdb-api-8 \
        python-crypto \
        tdb-tools \
        update-inetd \
    && \
    apt-get clean

RUN set -x && \
    dpkg --install \
        /deb/libwbclient0_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
        /deb/python-samba_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
        /deb/samba-common-bin_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
        /deb/samba-common_${SAMBA_PATCHED_DEB_VERSION}_all.deb \
        /deb/samba-dsdb-modules_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
        /deb/samba-libs_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
        /deb/samba-vfs-disable-chmod_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
        /deb/samba-vfs-disable-setfacl_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
        /deb/samba_${SAMBA_PATCHED_DEB_VERSION}_amd64.deb \
    && \
    true

COPY init-smbd /usr/local/bin/
RUN set -x && \
    chmod 0755 /usr/local/bin/init-smbd && \
    printf 'test\ntest\n' | adduser --shell /bin/bash --gecos testuser alice && \
    printf 'test\ntest\n' | smbpasswd -s -a alice && \
    printf 'test\ntest\n' | adduser --shell /bin/bash --gecos testuser bob && \
    printf 'test\ntest\n' | smbpasswd -s -a bob && \
    printf 'test\ntest\n' | adduser --shell /bin/bash --gecos testuser charly && \
    printf 'test\ntest\n' | smbpasswd -s -a charly && \
    addgroup ag-foo && \
    addgroup bar-ops && \
    adduser alice ag-foo && \
    adduser bob bar-ops && \
    adduser charly ag-foo && \
    echo 'include = /samba/local.conf' >>/etc/samba/smb.conf

VOLUME ["/samba"]

EXPOSE 139

ENTRYPOINT ["tini", "-g", "--"]
CMD ["/usr/local/bin/init-smbd"]
