/*

It was a Kernel bug:

Und wenn Samba unter Kontrolle ist, brauchen wir noch einen Kernel-Patch "ext4:
Don't clear SGID when inheriting ACLs", damit SGID mit default POSIX ACL ohne
Samba überhaupt korrekt funktioniert:

Ist in v4.13-rc4
<https://github.com/torvalds/linux/commit/a3bb2d5587521eea6dab2d05326abb0afb460abd>

Kommt wohl nach 4.12-stable <https://patchwork.kernel.org/patch/9891301/>

```
commit a3bb2d5587521eea6dab2d05326abb0afb460abd
Author: Jan Kara <jack@suse.cz>
Date:   Sun Jul 30 23:33:01 2017 -0400

    ext4: Don't clear SGID when inheriting ACLs
    
    When new directory 'DIR1' is created in a directory 'DIR0' with SGID bit
    set, DIR1 is expected to have SGID bit set (and owning group equal to
    the owning group of 'DIR0'). However when 'DIR0' also has some default
    ACLs that 'DIR1' inherits, setting these ACLs will result in SGID bit on
    'DIR1' to get cleared if user is not member of the owning group.
    
    Fix the problem by moving posix_acl_update_mode() out of
    __ext4_set_acl() into ext4_set_acl(). That way the function will not be
    called when inheriting ACLs which is what we want as it prevents SGID
    bit clearing and the mode has been properly set by posix_acl_create()
    anyway.
    
    Fixes: 073931017b49d9458aa351605b43a7e34598caef
    CC: stable@vger.kernel.org
    Signed-off-by: Theodore Ts'o <tytso@mit.edu>
    Signed-off-by: Jan Kara <jack@suse.cz>
    Reviewed-by: Andreas Gruenbacher <agruenba@redhat.com>

diff --git a/fs/ext4/acl.c b/fs/ext4/acl.c
index 2985cd0a640d..46ff2229ff5e 100644
--- a/fs/ext4/acl.c
+++ b/fs/ext4/acl.c
@@ -189,18 +189,10 @@ __ext4_set_acl(handle_t *handle, struct inode *inode, int type,
 	void *value = NULL;
 	size_t size = 0;
 	int error;
-	int update_mode = 0;
-	umode_t mode = inode->i_mode;
 
 	switch (type) {
 	case ACL_TYPE_ACCESS:
 		name_index = EXT4_XATTR_INDEX_POSIX_ACL_ACCESS;
-		if (acl) {
-			error = posix_acl_update_mode(inode, &mode, &acl);
-			if (error)
-				return error;
-			update_mode = 1;
-		}
 		break;
 
 	case ACL_TYPE_DEFAULT:
@@ -224,11 +216,6 @@ __ext4_set_acl(handle_t *handle, struct inode *inode, int type,
 	kfree(value);
 	if (!error) {
 		set_cached_acl(inode, type, acl);
-		if (update_mode) {
-			inode->i_mode = mode;
-			inode->i_ctime = current_time(inode);
-			ext4_mark_inode_dirty(handle, inode);
-		}
 	}
 
 	return error;
@@ -240,6 +227,8 @@ ext4_set_acl(struct inode *inode, struct posix_acl *acl, int type)
 	handle_t *handle;
 	int error, credits, retries = 0;
 	size_t acl_size = acl ? ext4_acl_size(acl->a_count) : 0;
+	umode_t mode = inode->i_mode;
+	int update_mode = 0;
 
 	error = dquot_initialize(inode);
 	if (error)
@@ -254,7 +243,20 @@ ext4_set_acl(struct inode *inode, struct posix_acl *acl, int type)
 	if (IS_ERR(handle))
 		return PTR_ERR(handle);
 
+	if ((type == ACL_TYPE_ACCESS) && acl) {
+		error = posix_acl_update_mode(inode, &mode, &acl);
+		if (error)
+			goto out_stop;
+		update_mode = 1;
+	}
+
 	error = __ext4_set_acl(handle, inode, type, acl, 0 /* xattr_flags */);
+	if (!error && update_mode) {
+		inode->i_mode = mode;
+		inode->i_ctime = current_time(inode);
+		ext4_mark_inode_dirty(handle, inode);
+	}
+out_stop:
 	ext4_journal_stop(handle);
 	if (error == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
 		goto retry;
```

*/

// linux/fs/attr.c

// user space -> chmod() -> vfs setattr

int ext4_setattr(struct dentry *dentry, struct iattr *attr)
// ->

int setattr_prepare(struct dentry *dentry, struct iattr *attr)
{
    // ...
	/* Make sure a caller can chmod. */
	if (ia_valid & ATTR_MODE) {
		if (!inode_owner_or_capable(inode))
			return -EPERM;
		/* Also check the setgid bit! */
		if (!in_group_p((ia_valid & ATTR_GID) ? attr->ia_gid :
				inode->i_gid) &&
		    !capable_wrt_inode_uidgid(inode, CAP_FSETID))
			attr->ia_mode &= ~S_ISGID;  // <-- This might drop SGID.  Bad!
	}
    // ...
}


/*

CAPABILITIES(7)

       CAP_FSETID
              * Don't clear set-user-ID and set-group-ID mode bits when a
                file is modified;
              * set the set-group-ID bit for a file whose GID does not match
                the filesystem or any of the supplementary GIDs of the
                calling process.

*/

// kernel/groups.c

/*
 * Check whether we're fsgid/egid or in the supplemental group..
 */
int in_group_p(kgid_t grp)
{
	const struct cred *cred = current_cred();
	int retval = 1;

	if (!gid_eq(grp, cred->fsgid))
		retval = groups_search(cred->group_info, grp);
	return retval;
}


// fs/inode.c


/**
 * inode_init_owner - Init uid,gid,mode for new inode according to posix standards
 * @inode: New inode
 * @dir: Directory inode
 * @mode: mode of the new inode
 */
void inode_init_owner(struct inode *inode, const struct inode *dir,
			umode_t mode)
{
	inode->i_uid = current_fsuid();
	if (dir && dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;  // <-- sets SGID if parent has SGID.  Good!
	} else
		inode->i_gid = current_fsgid();
	inode->i_mode = mode;
}
EXPORT_SYMBOL(inode_init_owner);


// XFS might be different.  But it's not.  ISGID is dropped if user is not in
// group.
//
// xfs_inode.c

static int
xfs_ialloc() {
	/*
         *
	 * If the group ID of the new file does not match the effective group
	 * ID or one of the supplementary group IDs, the S_ISGID bit is cleared
	 * (and only if the irix_sgid_inherit compatibility variable is set).
	 */
	if ((irix_sgid_inherit) &&
	    (inode->i_mode & S_ISGID) &&
	    (!in_group_p(xfs_gid_to_kgid(ip->i_d.di_gid))))
		inode->i_mode &= ~S_ISGID;
}


/**

ISGID is preserved without default ACL.

root@samba:/samba/testdata-inherit-posix-acls/device/x# id bob
uid=1001(bob) gid=1001(bob) groups=1001(bob),1004(bar-ops)

root@samba:/samba/testdata-inherit-posix-acls/device/x# su bob
bob@samba:/samba/testdata-inherit-posix-acls/device/x$ getfacl .
# file: .
# owner: root
# group: ag-foo
# flags: -s-
user::rwx
group::---
group:ag-foo:rwx
group:bar-ops:rwx
mask::rwx
other::---

bob@samba:/samba/testdata-inherit-posix-acls/device/x$ mkdir x
bob@samba:/samba/testdata-inherit-posix-acls/device/x$ ls -l
total 4
drwxr-sr-x 2 bob ag-foo 4096 Aug 10 21:23 x
bob@samba:/samba/testdata-inherit-posix-acls/device/x$ getfacl x
# file: x
# owner: bob
# group: ag-foo
# flags: -s-
user::rwx
group::r-x
other::r-x

*/
