/**
 * \file drm_proc.c
 * /proc support for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 *
 * \par Acknowledgements:
 *    Matthew J Sottek <matthew.j.sottek@intel.com> sent in a patch to fix
 *    the problem with the proc files not outputting all their information.
 */

/*
 * Created: Mon Jan 11 09:48:47 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"

static int drm_name_info(char *buf, char **start, off_t offset,
			 int request, int *eof, void *data);
static int drm_vm_info(char *buf, char **start, off_t offset,
		       int request, int *eof, void *data);
static int drm_clients_info(char *buf, char **start, off_t offset,
			    int request, int *eof, void *data);
static int drm_queues_info(char *buf, char **start, off_t offset,
			   int request, int *eof, void *data);
static int drm_bufs_info(char *buf, char **start, off_t offset,
			 int request, int *eof, void *data);
static int drm_objects_info(char *buf, char **start, off_t offset,
			 int request, int *eof, void *data);
static int drm_gem_name_info(char *buf, char **start, off_t offset,
			     int request, int *eof, void *data);
static int drm_gem_object_info(char *buf, char **start, off_t offset,
			       int request, int *eof, void *data);
#if DRM_DEBUG_CODE
static int drm_vma_info(char *buf, char **start, off_t offset,
			int request, int *eof, void *data);
#endif

/**
 * Proc file list.
 */
static struct drm_proc_list {
	const char *name;	/**< file name */
	int (*f) (char *, char **, off_t, int, int *, void *);		/**< proc callback*/
} drm_proc_list[] = {
	{"name", drm_name_info},
	{"mem", drm_mem_info},
	{"vm", drm_vm_info},
	{"clients", drm_clients_info},
	{"queues", drm_queues_info},
	{"bufs", drm_bufs_info},
	{"objects", drm_objects_info},
	{"gem_names", drm_gem_name_info},
	{"gem_objects", drm_gem_object_info},
#if DRM_DEBUG_CODE
	{"vma", drm_vma_info},
#endif
};

#define DRM_PROC_ENTRIES ARRAY_SIZE(drm_proc_list)

/**
 * Initialize the DRI proc filesystem for a device.
 *
 * \param dev DRM device.
 * \param minor device minor number.
 * \param root DRI proc dir entry.
 * \param dev_root resulting DRI device proc dir entry.
 * \return root entry pointer on success, or NULL on failure.
 *
 * Create the DRI proc root entry "/proc/dri", the device proc root entry
 * "/proc/dri/%minor%/", and each entry in proc_list as
 * "/proc/dri/%minor%/%name%".
 */
int drm_proc_init(struct drm_minor *minor, int minor_id,
		  struct proc_dir_entry *root)
{
	struct proc_dir_entry *ent;
	int i, j;
	char name[64];

	sprintf(name, "%d", minor_id);
	minor->dev_root = proc_mkdir(name, root);
	if (!minor->dev_root) {
		DRM_ERROR("Cannot create /proc/dri/%s\n", name);
		return -1;
	}

	for (i = 0; i < DRM_PROC_ENTRIES; i++) {
		ent = create_proc_entry(drm_proc_list[i].name,
					S_IFREG | S_IRUGO, minor->dev_root);
		if (!ent) {
			DRM_ERROR("Cannot create /proc/dri/%s/%s\n",
				  name, drm_proc_list[i].name);
			for (j = 0; j < i; j++)
				remove_proc_entry(drm_proc_list[i].name,
						  minor->dev_root);
			remove_proc_entry(name, root);
			minor->dev_root = NULL;
			return -1;
		}
		ent->read_proc = drm_proc_list[i].f;
		ent->data = minor;
	}
	return 0;
}

/**
 * Cleanup the proc filesystem resources.
 *
 * \param minor device minor number.
 * \param root DRI proc dir entry.
 * \param dev_root DRI device proc dir entry.
 * \return always zero.
 *
 * Remove all proc entries created by proc_init().
 */
int drm_proc_cleanup(struct drm_minor *minor, struct proc_dir_entry *root)
{
	int i;
	char name[64];

	if (!root || !minor->dev_root)
		return 0;

	for (i = 0; i < DRM_PROC_ENTRIES; i++)
		remove_proc_entry(drm_proc_list[i].name, minor->dev_root);
	sprintf(name, "%d", minor->index);
	remove_proc_entry(name, root);

	return 0;
}

/**
 * Called when "/proc/dri/.../name" is read.
 *
 * \param buf output buffer.
 * \param start start of output data.
 * \param offset requested start offset.
 * \param request requested number of bytes.
 * \param eof whether there is no more data to return.
 * \param data private data.
 * \return number of written bytes.
 *
 * Prints the device name together with the bus id if available.
 */
static int drm_name_info(char *buf, char **start, off_t offset, int request,
			 int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;

	if (dev->unique) {
		DRM_PROC_PRINT("%s %s %s\n",
			       dev->driver->pci_driver.name,
			       pci_name(dev->pdev), dev->unique);
	} else {
		DRM_PROC_PRINT("%s %s\n", dev->driver->pci_driver.name,
			       pci_name(dev->pdev));
	}

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

/**
 * Called when "/proc/dri/.../vm" is read.
 *
 * \param buf output buffer.
 * \param start start of output data.
 * \param offset requested start offset.
 * \param request requested number of bytes.
 * \param eof whether there is no more data to return.
 * \param data private data.
 * \return number of written bytes.
 *
 * Prints information about all mappings in drm_device::maplist.
 */
static int drm__vm_info(char *buf, char **start, off_t offset, int request,
			int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int len = 0;
	struct drm_map *map;
	struct drm_map_list *r_list;

	/* Hardcoded from _DRM_FRAME_BUFFER,
	   _DRM_REGISTERS, _DRM_SHM, _DRM_AGP,
	   _DRM_SCATTER_GATHER, and _DRM_CONSISTENT. */
	const char *types[] = { "FB", "REG", "SHM", "AGP", "SG", "PCI" };
	const char *type;
	int i;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;

	DRM_PROC_PRINT("slot	 offset	      size type flags	 "
		       "address mtrr\n\n");
	i = 0;
	list_for_each_entry(r_list, &dev->maplist, head) {
		map = r_list->map;
		if (!map)
			continue;
		if (map->type < 0 || map->type > 5)
			type = "??";
		else
			type = types[map->type];
		DRM_PROC_PRINT("%4d 0x%08lx 0x%08lx %4.4s  0x%02x 0x%08lx ",
			       i,
			       map->offset,
			       map->size, type, map->flags,
			       (unsigned long) r_list->user_token);

		if (map->mtrr < 0) {
			DRM_PROC_PRINT("none\n");
		} else {
			DRM_PROC_PRINT("%4d\n", map->mtrr);
		}
		i++;
	}

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

/**
 * Simply calls _vm_info() while holding the drm_device::struct_mutex lock.
 */
static int drm_vm_info(char *buf, char **start, off_t offset, int request,
		       int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm__vm_info(buf, start, offset, request, eof, data);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when "/proc/dri/.../queues" is read.
 *
 * \param buf output buffer.
 * \param start start of output data.
 * \param offset requested start offset.
 * \param request requested number of bytes.
 * \param eof whether there is no more data to return.
 * \param data private data.
 * \return number of written bytes.
 */
static int drm__queues_info(char *buf, char **start, off_t offset,
			    int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int len = 0;
	int i;
	struct drm_queue *q;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;

	DRM_PROC_PRINT("  ctx/flags   use   fin"
		       "   blk/rw/rwf  wait    flushed	   queued"
		       "      locks\n\n");
	for (i = 0; i < dev->queue_count; i++) {
		q = dev->queuelist[i];
		atomic_inc(&q->use_count);
		DRM_PROC_PRINT_RET(atomic_dec(&q->use_count),
				   "%5d/0x%03x %5d %5d"
				   " %5d/%c%c/%c%c%c %5Zd\n",
				   i,
				   q->flags,
				   atomic_read(&q->use_count),
				   atomic_read(&q->finalization),
				   atomic_read(&q->block_count),
				   atomic_read(&q->block_read) ? 'r' : '-',
				   atomic_read(&q->block_write) ? 'w' : '-',
				   waitqueue_active(&q->read_queue) ? 'r' : '-',
				   waitqueue_active(&q->
						    write_queue) ? 'w' : '-',
				   waitqueue_active(&q->
						    flush_queue) ? 'f' : '-',
				   DRM_BUFCOUNT(&q->waitlist));
		atomic_dec(&q->use_count);
	}

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

/**
 * Simply calls _queues_info() while holding the drm_device::struct_mutex lock.
 */
static int drm_queues_info(char *buf, char **start, off_t offset, int request,
			   int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm__queues_info(buf, start, offset, request, eof, data);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when "/proc/dri/.../bufs" is read.
 *
 * \param buf output buffer.
 * \param start start of output data.
 * \param offset requested start offset.
 * \param request requested number of bytes.
 * \param eof whether there is no more data to return.
 * \param data private data.
 * \return number of written bytes.
 */
static int drm__bufs_info(char *buf, char **start, off_t offset, int request,
			  int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int len = 0;
	struct drm_device_dma *dma = dev->dma;
	int i;

	if (!dma || offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;

	DRM_PROC_PRINT(" o     size count  free	 segs pages    kB\n\n");
	for (i = 0; i <= DRM_MAX_ORDER; i++) {
		if (dma->bufs[i].buf_count)
			DRM_PROC_PRINT("%2d %8d %5d %5d %5d %5d %5ld\n",
				       i,
				       dma->bufs[i].buf_size,
				       dma->bufs[i].buf_count,
				       atomic_read(&dma->bufs[i]
						   .freelist.count),
				       dma->bufs[i].seg_count,
				       dma->bufs[i].seg_count
				       * (1 << dma->bufs[i].page_order),
				       (dma->bufs[i].seg_count
					* (1 << dma->bufs[i].page_order))
				       * PAGE_SIZE / 1024);
	}
	DRM_PROC_PRINT("\n");
	for (i = 0; i < dma->buf_count; i++) {
		if (i && !(i % 32))
			DRM_PROC_PRINT("\n");
		DRM_PROC_PRINT(" %d", dma->buflist[i]->list);
	}
	DRM_PROC_PRINT("\n");

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

/**
 * Simply calls _bufs_info() while holding the drm_device::struct_mutex lock.
 */
static int drm_bufs_info(char *buf, char **start, off_t offset, int request,
			 int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm__bufs_info(buf, start, offset, request, eof, data);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when "/proc/dri/.../objects" is read.
 *
 * \param buf output buffer.
 * \param start start of output data.
 * \param offset requested start offset.
 * \param request requested number of bytes.
 * \param eof whether there is no more data to return.
 * \param data private data.
 * \return number of written bytes.
 */
static int drm__objects_info(char *buf, char **start, off_t offset, int request,
			  int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int len = 0;
	struct drm_buffer_manager *bm = &dev->bm;
	struct drm_fence_manager *fm = &dev->fm;
	uint64_t used_mem;
	uint64_t used_emer;
	uint64_t low_mem;
	uint64_t high_mem;
	uint64_t emer_mem;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;

	DRM_PROC_PRINT("Object accounting:\n\n");
	if (fm->initialized) {
		DRM_PROC_PRINT("Number of active fence objects: %d.\n",
			       atomic_read(&fm->count));
	} else {
		DRM_PROC_PRINT("Fence objects are not supported by this driver\n");
	}

	if (bm->initialized) {
		DRM_PROC_PRINT("Number of active buffer objects: %d.\n\n",
			       atomic_read(&bm->count));
	}
	DRM_PROC_PRINT("Memory accounting:\n\n");
	if (bm->initialized) {
		DRM_PROC_PRINT("Number of locked GATT pages: %lu.\n", bm->cur_pages);
	} else {
		DRM_PROC_PRINT("Buffer objects are not supported by this driver.\n");
	}

	drm_query_memctl(&used_mem, &used_emer, &low_mem, &high_mem, &emer_mem);

	if (used_mem > 16*PAGE_SIZE) {
		DRM_PROC_PRINT("Used object memory is %lu pages.\n",
			       (unsigned long) (used_mem >> PAGE_SHIFT));
	} else {
		DRM_PROC_PRINT("Used object memory is %lu bytes.\n",
			       (unsigned long) used_mem);
	}
	if (used_emer > 16*PAGE_SIZE) {
		DRM_PROC_PRINT("Used emergency memory is %lu pages.\n",
			       (unsigned long) (used_emer >> PAGE_SHIFT));
	} else {
		DRM_PROC_PRINT("Used emergency memory is %lu bytes.\n\n",
			       (unsigned long) used_emer);
	}
	DRM_PROC_PRINT("Soft object memory usage threshold is %lu pages.\n",
		       (unsigned long) (low_mem >> PAGE_SHIFT));
	DRM_PROC_PRINT("Hard object memory usage threshold is %lu pages.\n",
		       (unsigned long) (high_mem >> PAGE_SHIFT));
	DRM_PROC_PRINT("Emergency root only memory usage threshold is %lu pages.\n",
		       (unsigned long) (emer_mem >> PAGE_SHIFT));

	DRM_PROC_PRINT("\n");

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

/**
 * Simply calls _objects_info() while holding the drm_device::struct_mutex lock.
 */
static int drm_objects_info(char *buf, char **start, off_t offset, int request,
			 int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm__objects_info(buf, start, offset, request, eof, data);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when "/proc/dri/.../clients" is read.
 *
 * \param buf output buffer.
 * \param start start of output data.
 * \param offset requested start offset.
 * \param request requested number of bytes.
 * \param eof whether there is no more data to return.
 * \param data private data.
 * \return number of written bytes.
 */
static int drm__clients_info(char *buf, char **start, off_t offset,
			     int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int len = 0;
	struct drm_file *priv;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;

	DRM_PROC_PRINT("a dev	pid    uid	magic	  ioctls\n\n");
	list_for_each_entry(priv, &dev->filelist, lhead) {
		DRM_PROC_PRINT("%c %3d %5d %5d %10u %10lu\n",
			       priv->authenticated ? 'y' : 'n',
			       priv->minor->index,
			       priv->pid,
			       priv->uid, priv->magic, priv->ioctl_count);
	}

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

/**
 * Simply calls _clients_info() while holding the drm_device::struct_mutex lock.
 */
static int drm_clients_info(char *buf, char **start, off_t offset,
			    int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm__clients_info(buf, start, offset, request, eof, data);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

struct drm_gem_name_info_data {
	int			len;
	char			*buf;
	int			eof;
};

static int drm_gem_one_name_info(int id, void *ptr, void *data)
{
	struct drm_gem_object *obj = ptr;
	struct drm_gem_name_info_data	*nid = data;

	DRM_INFO("name %d size %zd\n", obj->name, obj->size);
	if (nid->eof)
		return 0;

	nid->len += sprintf(&nid->buf[nid->len],
			    "%6d%9zd%8d%9d\n",
			    obj->name, obj->size,
			    atomic_read(&obj->handlecount.refcount),
			    atomic_read(&obj->refcount.refcount));
	if (nid->len > DRM_PROC_LIMIT) {
		nid->eof = 1;
		return 0;
	}
	return 0;
}

static int drm_gem_name_info(char *buf, char **start, off_t offset,
			     int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	struct drm_gem_name_info_data nid;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	nid.len = sprintf(buf, "  name     size handles refcount\n");
	nid.buf = buf;
	nid.eof = 0;
	idr_for_each(&dev->object_name_idr, drm_gem_one_name_info, &nid);

	*start = &buf[offset];
	*eof = 0;
	if (nid.len > request + offset)
		return request;
	*eof = 1;
	return nid.len - offset;
}

static int drm_gem_object_info(char *buf, char **start, off_t offset,
			       int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	DRM_PROC_PRINT("%d objects\n", atomic_read(&dev->object_count));
	DRM_PROC_PRINT("%d object bytes\n", atomic_read(&dev->object_memory));
	DRM_PROC_PRINT("%d pinned\n", atomic_read(&dev->pin_count));
	DRM_PROC_PRINT("%d pin bytes\n", atomic_read(&dev->pin_memory));
	DRM_PROC_PRINT("%d gtt bytes\n", atomic_read(&dev->gtt_memory));
	DRM_PROC_PRINT("%d gtt total\n", dev->gtt_total);
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

#if DRM_DEBUG_CODE

static int drm__vma_info(char *buf, char **start, off_t offset, int request,
			 int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int len = 0;
	struct drm_vma_entry *pt;
	struct vm_area_struct *vma;
#if defined(__i386__)
	unsigned int pgprot;
#endif

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;

	DRM_PROC_PRINT("vma use count: %d, high_memory = %p, 0x%08lx\n",
		       atomic_read(&dev->vma_count),
		       high_memory, virt_to_phys(high_memory));
	list_for_each_entry(pt, &dev->vmalist, head) {
		if (!(vma = pt->vma))
			continue;
		DRM_PROC_PRINT("\n%5d 0x%08lx-0x%08lx %c%c%c%c%c%c 0x%08lx000",
			       pt->pid,
			       vma->vm_start,
			       vma->vm_end,
			       vma->vm_flags & VM_READ ? 'r' : '-',
			       vma->vm_flags & VM_WRITE ? 'w' : '-',
			       vma->vm_flags & VM_EXEC ? 'x' : '-',
			       vma->vm_flags & VM_MAYSHARE ? 's' : 'p',
			       vma->vm_flags & VM_LOCKED ? 'l' : '-',
			       vma->vm_flags & VM_IO ? 'i' : '-',
			       vma->vm_pgoff);

#if defined(__i386__)
		pgprot = pgprot_val(vma->vm_page_prot);
		DRM_PROC_PRINT(" %c%c%c%c%c%c%c%c%c",
			       pgprot & _PAGE_PRESENT ? 'p' : '-',
			       pgprot & _PAGE_RW ? 'w' : 'r',
			       pgprot & _PAGE_USER ? 'u' : 's',
			       pgprot & _PAGE_PWT ? 't' : 'b',
			       pgprot & _PAGE_PCD ? 'u' : 'c',
			       pgprot & _PAGE_ACCESSED ? 'a' : '-',
			       pgprot & _PAGE_DIRTY ? 'd' : '-',
			       pgprot & _PAGE_PSE ? 'm' : 'k',
			       pgprot & _PAGE_GLOBAL ? 'g' : 'l');
#endif
		DRM_PROC_PRINT("\n");
	}

	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

static int drm_vma_info(char *buf, char **start, off_t offset, int request,
			int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data; 
	struct drm_device *dev = minor->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm__vma_info(buf, start, offset, request, eof, data);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}
#endif