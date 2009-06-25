/*
  Copyright (c) 2007-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _FD_H
#define _FD_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "list.h"
#include <sys/types.h>
#include <unistd.h>
#include "glusterfs.h"
#include "locking.h"

struct _inode;
struct _dict;
struct _fd_ctx {
	uint64_t key;
	uint64_t value;
};

/* If this structure changes, please have mercy on the booster maintainer
 * and update the fd_t struct in booster/src/booster-fd.h.
 * See the comment there to know why.
 */
struct _fd {
        pid_t             pid;
	int32_t           flags;
        int32_t           refcount;
        struct list_head  inode_list;
        struct _inode    *inode;
        struct _dict     *ctx;
        gf_lock_t         lock; /* used ONLY for manipulating
                                   'struct _fd_ctx' array (_ctx).*/
	struct _fd_ctx   *_ctx;
};
typedef struct _fd fd_t;

struct fd_table_entry {
        fd_t    *fd;
        int     next_free;
};
typedef struct fd_table_entry fdentry_t;

struct _fdtable {
        int             refcount;
        uint32_t        max_fds;
        pthread_mutex_t lock;
        fdentry_t       *fdentries;
        int             first_free;
};
typedef struct _fdtable fdtable_t;

/* Signifies no more entries in the fd table. */
#define GF_FDTABLE_END  -1

/* This is used to invalidated
 * the next_free value in an fdentry that has been allocated
 */
#define GF_FDENTRY_ALLOCATED    -2

#include "logging.h"
#include "xlator.h"

inline void 
gf_fd_put (fdtable_t *fdtable, int32_t fd);

fd_t *
gf_fd_fdptr_get (fdtable_t *fdtable, int64_t fd);

fdtable_t *
gf_fd_fdtable_alloc (void);

int32_t 
gf_fd_unused_get (fdtable_t *fdtable, fd_t *fdptr);

fdentry_t *
gf_fd_fdtable_get_all_fds (fdtable_t *fdtable, uint32_t *count);

void 
gf_fd_fdtable_destroy (fdtable_t *fdtable);

fd_t *
fd_ref (fd_t *fd);

void
fd_unref (fd_t *fd);

fd_t *
fd_create (struct _inode *inode, pid_t pid);

fd_t *
fd_lookup (struct _inode *inode, pid_t pid);

uint8_t
fd_list_empty (struct _inode *inode);

fd_t *
fd_bind (fd_t *fd);

int
fd_ctx_set (fd_t *fd, xlator_t *xlator, uint64_t value);

int 
fd_ctx_get (fd_t *fd, xlator_t *xlator, uint64_t *value);

int 
fd_ctx_del (fd_t *fd, xlator_t *xlator, uint64_t *value);

int
__fd_ctx_set (fd_t *fd, xlator_t *xlator, uint64_t value);

int 
__fd_ctx_get (fd_t *fd, xlator_t *xlator, uint64_t *value);

int 
__fd_ctx_del (fd_t *fd, xlator_t *xlator, uint64_t *value);

#endif /* _FD_H */
