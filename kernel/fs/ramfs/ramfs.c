/* SPDX-License-Identifier: BSD-2-Clause */

#include "ramfs_int.h"
#include "getdents.c.h"
#include "locking.c.h"
#include "dir_entries.c.h"
#include "inodes.c.h"
#include "stat.c.h"
#include "rw_ops.c.h"
#include "open.c.h"
#include "mkdir.c.h"

static int ramfs_unlink(vfs_path *p)
{
   ramfs_path *rp = (ramfs_path *) &p->fs_path;
   ramfs_data *d = p->fs->device_data;
   ramfs_inode *i = rp->inode;
   ramfs_inode *idir = rp->dir_inode;

   ASSERT(rwlock_wp_holding_exlock(&d->rwlock));

   if (i->type == VFS_DIR)
      return -EISDIR;

   if (!(idir->mode & 0200)) /* write permission */
      return -EACCES;

   /*
    * The only case when `rp->e` is NULL is when path == "/", but we have just
    * checked the directory case. Therefore, `rp->e` must be valid.
    */
   ASSERT(rp->dir_entry != NULL);

   /* Remove the dir entry */
   ramfs_dir_remove_entry(idir, rp->dir_entry);

   /* Trucate and delete the inode, if it's not used */
   if (!i->nlink && !get_ref_count(i)) {
      rwlock_wp_exlock(&i->rwlock);
      {
         ramfs_inode_truncate(i, 0);
      }
      rwlock_wp_exunlock(&i->rwlock);
      ramfs_destroy_inode(d, i);
   }

   return 0;
}

static int ramfs_dup(fs_handle h, fs_handle *dup_h)
{
   ramfs_handle *new_h = kmalloc(sizeof(ramfs_handle));

   if (!new_h)
      return -ENOMEM;

   memcpy(new_h, h, sizeof(ramfs_handle));
   retain_obj(new_h->inode);
   *dup_h = new_h;
   return 0;
}

static void ramfs_close(fs_handle h)
{
   ramfs_handle *rh = h;
   release_obj(rh->inode);

   if (!get_ref_count(rh->inode) && !rh->inode->nlink) {

      /*
       * !get_ref_count(rh->inode) => no handle referring to this inode
       * !rh->inode->nlink         => no dir entry referring to this inode
       *
       * It means the last link (dir entry) pointing to this inode has been
       * removed while the current task was keeping opened a handle to this
       * inode. Now, nobody can get to this inode anymore. We have to destroy
       * it.
       */

      ramfs_inode_truncate(rh->inode, 0);
      ramfs_destroy_inode(rh->fs->device_data, rh->inode);
   }

   kfree2(rh, sizeof(ramfs_handle));
}

/*
 * This function is supposed to be called ONLY by ramfs_create() in its error
 * path, as a clean-up. It is *not* a proper way to destroy a whole ramfs
 * instance after unmounting it.
 */
static void ramfs_err_case_destroy(filesystem *fs)
{
   ramfs_data *d = fs->device_data;

   if (d) {

      if (d->root) {
         ramfs_destroy_inode(d, d->root);
      }

      rwlock_wp_destroy(&d->rwlock);
      kfree2(d, sizeof(ramfs_data));
   }

   kfree2(fs, sizeof(filesystem));
}

static void
ramfs_get_entry(filesystem *fs,
                void *dir_inode,
                const char *name,
                ssize_t name_len,
                fs_path_struct *fs_path)
{
   ramfs_data *d = fs->device_data;
   ramfs_inode *idir = dir_inode;
   ramfs_entry *re;

   if (!dir_inode) {

      *fs_path = (fs_path_struct) {
         .inode = d->root,
         .dir_inode = d->root,
         .dir_entry = NULL,
         .type = VFS_DIR,
      };

      return;
   }

   re = ramfs_dir_get_entry_by_name(idir, name, name_len);

   *fs_path = (fs_path_struct) {
      .inode      = re ? re->inode : NULL,
      .dir_inode  = idir,
      .dir_entry  = re,
      .type       = re ? re->inode->type : VFS_NONE,
   };
}

static const fs_ops static_fsops_ramfs =
{
   .open = ramfs_open,
   .close = ramfs_close,
   .dup = ramfs_dup,
   .getdents_new = ramfs_getdents_new,
   .unlink = ramfs_unlink,
   .mkdir = ramfs_mkdir,
   .rmdir = ramfs_rmdir,
   .fstat = ramfs_fstat64,
   .get_entry = ramfs_get_entry,

   .fs_exlock = ramfs_exlock,
   .fs_exunlock = ramfs_exunlock,
   .fs_shlock = ramfs_shlock,
   .fs_shunlock = ramfs_shunlock,
};

filesystem *ramfs_create(void)
{
   filesystem *fs;
   ramfs_data *d;

   if (!(fs = kzmalloc(sizeof(filesystem))))
      return NULL;

   if (!(d = kzmalloc(sizeof(ramfs_data)))) {
      ramfs_err_case_destroy(fs);
      return NULL;
   }

   fs->device_data = d;
   rwlock_wp_init(&d->rwlock);
   d->next_inode_num = 1;
   d->root = ramfs_create_inode_dir(d, 0777, NULL);

   if (!d->root) {
      ramfs_err_case_destroy(fs);
      return NULL;
   }

   fs->fs_type_name = "ramfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;
   fs->fsops = &static_fsops_ramfs;

   //tmp
   {
      ramfs_inode *i1 = ramfs_create_inode_dir(d, 0755, d->root);
      VERIFY(ramfs_dir_add_entry(d->root, "e1", i1) == 0);

      ramfs_inode *i2 = ramfs_create_inode_file(d, 0644, d->root);
      VERIFY(ramfs_dir_add_entry(d->root, "e2", i2) == 0);

      ramfs_inode *i11 = ramfs_create_inode_dir(d, 0777, i1);
      VERIFY(ramfs_dir_add_entry(i1, "e11", i11) == 0);

      ramfs_inode *i12 = ramfs_create_inode_dir(d, 0777, i1);
      VERIFY(ramfs_dir_add_entry(i1, "e12", i12) == 0);

      ramfs_inode *i111 = ramfs_create_inode_file(d, 0644, i11);
      VERIFY(ramfs_dir_add_entry(i11, "e111", i111) == 0);
   }
   //end tmp
   return fs;
}
