/* SPDX-License-Identifier: BSD-2-Clause */

static filesystem *
get_retained_fs_at(const char *path, const char **fs_path_ref)
{
   mountpoint *mp, *best_match = NULL;
   u32 len, pl, best_match_len = 0;
   filesystem *fs = NULL;
   mp_cursor cur;

   *fs_path_ref = NULL;
   pl = (u32)strlen(path);

   mountpoint_iter_begin(&cur);

   while ((mp = mountpoint_get_next(&cur))) {

      len = mp_check_match(mp->path, mp->path_len, path, pl);

      if (len > best_match_len) {
         best_match = mp;
         best_match_len = len;
      }
   }

   if (best_match) {
      *fs_path_ref = (best_match_len < pl) ? path + best_match_len - 1 : "/";
      fs = best_match->fs;
      retain_obj(fs);
   }

   mountpoint_iter_end(&cur);
   return fs;
}

static int
vfs_resolve(filesystem *fs, const char *path, vfs_path *rp, bool res_last_sl)
{
   func_get_entry get_entry = fs->fsops->get_entry;
   fs_path_struct e;
   const char *pc;
   void *idir;

   get_entry(fs, NULL, NULL, 0, &e);

   idir = e.inode; /* idir = root's inode */
   bzero(rp, sizeof(*rp));

   ASSERT(*path == '/');
   pc = ++path;

   /* Always set the `fs` field, no matter what */
   rp->fs = fs;

   if (!*path) {
      /* path was just "/" */
      rp->fs_path = e;
      rp->last_comp = path;
      return 0;
   }

   while (*path) {

      if (*path != '/') {
         path++;
         continue;
      }

      /*
       * We hit a slash '/' in the path: we now must lookup this path component.
       *
       * NOTE: the code in upper layers normalizes the user paths, but it makes
       * sense to ASSERT that.
       */

      ASSERT(path[1] != '/');

      get_entry(fs, idir, pc, path - pc, &e);

      if (!e.inode) {

         if (path[1])
            return -ENOENT; /* the path does NOT end here: no such entity */

         /* no such entity, but the path ends here, with a trailing slash */
         break;
      }

      /* We've found an entity for this path component (pc) */

      if (!path[1]) {

         /* the path ends here, with a trailing slash */

         if (e.type != VFS_DIR)
            return -ENOTDIR; /* that's a problem only if `e` is NOT a dir */

         break;
      }

      idir = e.inode;
      pc = ++path;
   }

   ASSERT(path - pc > 0);

   get_entry(fs, idir, pc, path - pc, &rp->fs_path);
   rp->last_comp = pc;
   return 0;
}
