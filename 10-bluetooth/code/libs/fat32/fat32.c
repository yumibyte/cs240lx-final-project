#include "rpi.h"
#include "fat32.h"
#include "fat32-helpers.h"
#include "pi-sd.h"

// Print extra tracing info when this is enabled.  You can and should add your
// own.
static int trace_p = 1; 
static int init_p = 0;

fat32_boot_sec_t boot_sector;


fat32_fs_t fat32_mk(mbr_partition_ent_t *partition) {
  demand(!init_p, "the fat32 module is already in use\n");
  // TODO: Read the boot sector (of the partition) off the SD card.
  boot_sector = *((fat32_boot_sec_t *)pi_sec_read(partition->lba_start, 1));

  // TODO: Verify the boot sector (also called the volume id, `fat32_volume_id_check`)
  fat32_volume_id_check(&boot_sector);

  // TODO: Read the FS info sector (the sector immediately following the boot
  // sector) and check it (`fat32_fsinfo_check`, `fat32_fsinfo_print`)
  assert(boot_sector.info_sec_num == 1);
  struct fsinfo *fs_info = pi_sec_read(partition->lba_start + 1, 1);
  fat32_fsinfo_check(fs_info);

  fat32_volume_id_print("Printing volume_id", &boot_sector);
  fat32_fsinfo_print("Printing fs_info", fs_info);

  // END OF PART 2
  // The rest of this is for Part 3:

  // TODO: calculate the fat32_fs_t metadata, which we'll need to return.
  unsigned lba_start = partition->lba_start; // from the partition
  unsigned fat_begin_lba = partition->lba_start + boot_sector.reserved_area_nsec; // the start LBA + the number of reserved sectors
  unsigned cluster_begin_lba = fat_begin_lba + boot_sector.nfats * boot_sector.nsec_per_fat; // the beginning of the FAT, plus the combined length of all the FATs
  unsigned sec_per_cluster = boot_sector.sec_per_cluster; // from the boot sector
  unsigned root_first_cluster = boot_sector.first_cluster; // from the boot sector
  unsigned n_entries = boot_sector.nsec_per_fat * 128; // from the boot sector - pjrc pg 4

  /*
   * TODO: Read in the entire fat (one copy: worth reading in the second and
   * comparing).
   *
   * The disk is divided into clusters. The number of sectors per
   * cluster is given in the boot sector byte 13. <sec_per_cluster>
   *
   * The File Allocation Table has one entry per cluster. This entry
   * uses 12, 16 or 28 bits for FAT12, FAT16 and FAT32.
   *
   * Store the FAT in a heap-allocated array.
   */
  uint32_t *fat = pi_sec_read(fat_begin_lba, boot_sector.nsec_per_fat);

  // Create the FAT32 FS struct with all the metadata
  fat32_fs_t fs = (fat32_fs_t) {
    .lba_start = lba_start,
      .fat_begin_lba = fat_begin_lba,
      .cluster_begin_lba = cluster_begin_lba,
      .sectors_per_cluster = sec_per_cluster,
      .root_dir_first_cluster = root_first_cluster,
      .fat = fat,
      .n_entries = n_entries,
  };

  if (trace_p) {
    trace("begin lba = %d\n", fs.fat_begin_lba);
    trace("cluster begin lba = %d\n", fs.cluster_begin_lba);
    trace("sectors per cluster = %d\n", fs.sectors_per_cluster);
    trace("root dir first cluster = %d\n", fs.root_dir_first_cluster);
  }

  init_p = 1;
  return fs;
}

// Given cluster_number, get lba.  Helper function.
static uint32_t cluster_to_lba(fat32_fs_t *f, uint32_t cluster_num) {
  assert(cluster_num >= 2);
  // TODO: calculate LBA from cluster number, cluster_begin_lba, and
  // sectors_per_cluster
  
  unsigned lba = f->cluster_begin_lba + (cluster_num - 2) * f->sectors_per_cluster; // pjrc p3
  // if (trace_p) trace("cluster %d to lba: %d\n", cluster_num, lba);
  return lba;
}

pi_dirent_t fat32_get_root(fat32_fs_t *fs) {
  demand(init_p, "fat32 not initialized!");
  // TODO: return the information corresponding to the root directory (just
  // cluster_id, in this case)
  
  return (pi_dirent_t) {
    .name = "",
      .raw_name = "",
      .cluster_id = fs->root_dir_first_cluster, // fix this
      .is_dir_p = 1,
      .nbytes = 0,
  };
}

// Given the starting cluster index, get the length of the chain.  Helper
// function.
static uint32_t get_cluster_chain_length(fat32_fs_t *fs, uint32_t start_cluster) {
  // TODO: Walk the cluster chain in the FAT until you see a cluster where
  // `fat32_fat_entry_type(cluster) == LAST_CLUSTER`.  Count the number of
  // clusters.
  uint32_t chain_length = 1;
  uint32_t entry = fs->fat[start_cluster];
  while (fat32_fat_entry_type(entry) != LAST_CLUSTER) {
    chain_length++;
    entry = fs->fat[entry];
  }
  return chain_length;
}

// Given the starting cluster index, read a cluster chain into a contiguous
// buffer.  Assume the provided buffer is large enough for the whole chain.
// Helper function.
static void read_cluster_chain(fat32_fs_t *fs, uint32_t start_cluster, uint8_t *data) {
  // TODO: Walk the cluster chain in the FAT until you see a cluster where
  // fat32_fat_entry_type(cluster) == LAST_CLUSTER.  For each cluster, copy it
  // to the buffer (`data`).  Be sure to offset your data pointer by the
  // appropriate amount each time.
  uint32_t offset = 0;
  uint32_t current_cluster = start_cluster;
  uint32_t iter = 0;
  while (1) {
    uint32_t lba = cluster_to_lba(fs, current_cluster);
    pi_sd_read(data + offset, lba, fs->sectors_per_cluster);
    if (fat32_fat_entry_type(current_cluster) == LAST_CLUSTER)
      break;
    // if (trace_p) trace("iter=%d: \tcurrent cluster =%d; next cluster = %d\n", iter, current_cluster, fs->fat[current_cluster]);
    current_cluster = fs->fat[current_cluster];
    offset += fs->sectors_per_cluster * 512; // 512 bytes in a sector
    iter++;
  }
}

// Converts a fat32 internal dirent into a generic one suitable for use outside
// this driver.
static pi_dirent_t dirent_convert(fat32_dirent_t *d) {
  pi_dirent_t e = {
    .cluster_id = fat32_cluster_id(d),
    .is_dir_p = d->attr == FAT32_DIR,
    .nbytes = d->file_nbytes,
  };
  // can compare this name
  memcpy(e.raw_name, d->filename, sizeof d->filename);
  // for printing.
  fat32_dirent_name(d,e.name);
  return e;
}

// Gets all the dirents of a directory which starts at cluster `cluster_start`.
// Return a heap-allocated array of dirents.
static fat32_dirent_t *get_dirents(fat32_fs_t *fs, uint32_t cluster_start, uint32_t *dir_n) {
  // TODO: figure out the length of the cluster chain (see
  // `get_cluster_chain_length`)
  uint32_t chain_length = get_cluster_chain_length(fs, cluster_start);

  // TODO: allocate a buffer large enough to hold the whole directory
  uint8_t *buf = kmalloc(chain_length * fs->sectors_per_cluster * 512);
  *dir_n = chain_length * fs->sectors_per_cluster * 512 / sizeof(fat32_dirent_t); // total number of bytes / 32 bytes per dirent. 32 bytes per dirent from pjrc p3
  // TODO: read in the whole directory (see `read_cluster_chain`)
  // if (trace_p) trace("running get_dirents\n");
  read_cluster_chain(fs, cluster_start, buf);
  // if (trace_p) trace("done with get_dirents\n\n");
  return (fat32_dirent_t *)buf;
}

pi_directory_t fat32_readdir(fat32_fs_t *fs, pi_dirent_t *dirent) {
  demand(init_p, "fat32 not initialized!");
  demand(dirent->is_dir_p, "tried to readdir a file!");
  // TODO: use `get_dirents` to read the raw dirent structures from the disk
  uint32_t n_dirents;
  fat32_dirent_t *dirents = get_dirents(fs, dirent->cluster_id, &n_dirents);

  // TODO: allocate space to store the pi_dirent_t return values
  // HELP
  pi_dirent_t * pi_dirents = kmalloc(n_dirents * sizeof(pi_dirent_t));

  // TODO: iterate over the directory and create pi_dirent_ts for every valid
  // file.  Don't include empty dirents, LFNs, or Volume IDs.  You can use
  // `dirent_convert`.
  uint32_t num_dirents = 0;
  for(int i = 0; i < n_dirents; i++) {
    if (fat32_dirent_free(&dirents[i])) continue; // free space
    if (fat32_dirent_is_lfn(&dirents[i])) continue; // LFN version of name
    if (dirents[i].attr & FAT32_VOLUME_LABEL) continue; // volume label
    pi_dirents[num_dirents] = dirent_convert(&dirents[i]);
    num_dirents++;
  }
  

  // TODO: create a pi_directory_t using the dirents and the number of valid
  // dirents we found
  return (pi_directory_t) {
    .dirents = pi_dirents,
    .ndirents = num_dirents,
  };
}

static int find_dirent_with_name(fat32_dirent_t *dirents, int n, char *filename) {
  // TODO: iterate through the dirents, looking for a file which matches the
  // name; use `fat32_dirent_name` to convert the internal name format to a
  // normal string.
  
  // returns: the index of the matching dirent
  for (int i = 0; i < n; i++) {
    if (fat32_dirent_free(&dirents[i])) continue;
    if (fat32_dirent_is_lfn(&dirents[i])) continue;
    if (dirents[i].attr & FAT32_VOLUME_LABEL) continue;

    char name[11];
    fat32_dirent_name(&dirents[i], name);
    if (strcmp(name, filename) == 0)
      return i;
    trace("candidate=<%s> target=<%s>\n", name, filename);
  }
  return -1;
}

pi_dirent_t *fat32_stat(fat32_fs_t *fs, pi_dirent_t *directory, char *filename) {
  demand(init_p, "fat32 not initialized!");
  demand(directory->is_dir_p, "tried to use a file as a directory");

  // TODO: use `get_dirents` to read the raw dirent structures from the disk
  uint32_t n_dirents;
  fat32_dirent_t *dirents = get_dirents(fs, directory->cluster_id, &n_dirents);

  // TODO: Iterate through the directory's entries and find a dirent with the
  // provided name.  Return NULL if no such dirent exists.  You can use
  // `find_dirent_with_name` if you've implemented it.
  int idx = find_dirent_with_name(dirents, n_dirents, filename);
  if (idx == -1)
    return NULL;

  // TODO: allocate enough space for the dirent, then convert
  // (`dirent_convert`) the fat32 dirent into a Pi dirent.
  pi_dirent_t *dirent = kmalloc(sizeof(pi_dirent_t));
  *dirent = dirent_convert(&dirents[idx]);
  return dirent;
}

pi_file_t *fat32_read(fat32_fs_t *fs, pi_dirent_t *directory, char *filename) {
  // This should be pretty similar to readdir, but simpler.
  demand(init_p, "fat32 not initialized!");
  demand(directory->is_dir_p, "tried to use a file as a directory!");

  // TODO: read the dirents of the provided directory and look for one matching the provided name
  pi_dirent_t *matching_dir = fat32_stat(fs, directory, filename);

  // TODO: figure out the length of the cluster chain
  uint32_t n_clusters = get_cluster_chain_length(fs, matching_dir->cluster_id);

  // TODO: allocate a buffer large enough to hold the whole file
  char *data = kmalloc(n_clusters * fs->sectors_per_cluster * 512); // 512 bytes per sector

  // TODO: read in the whole file (if it's not empty)
  read_cluster_chain(fs, matching_dir->cluster_id, data);

  // TODO: fill the pi_file_t
  pi_file_t *file = kmalloc(sizeof(pi_file_t));
  *file = (pi_file_t) {
    .data = data,
    .n_data = matching_dir->nbytes,
    .n_alloc = n_clusters * fs->sectors_per_cluster * 512,
  };
  return file;
}

/******************************************************************************
 * Everything below here is for writing to the SD card (Part 7/Extension).  If
 * you're working on read-only code, you don't need any of this.
 ******************************************************************************/

static uint32_t find_free_cluster(fat32_fs_t *fs, uint32_t start_cluster) {
  // TODO: loop through the entries in the FAT until you find a free one
  // (fat32_fat_entry_type == FREE_CLUSTER).  Start from cluster 3.  Panic if
  // there are none left.
  if (start_cluster < 3) start_cluster = 3;

  /* Search from start_cluster up to end of FAT */
  for (uint32_t i = start_cluster; i < fs->n_entries; i++) {
    if (fat32_fat_entry_type(fs->fat[i]) == FREE_CLUSTER) {
      if (trace_p) trace("found free cluster %d\n", i);
      return i;
    }
  }


  if (trace_p) trace("failed to find free cluster from %d\n", start_cluster);
  panic("No more clusters on the disk!\n");
}

static void write_fat_to_disk(fat32_fs_t *fs) {
  // TODO: Write the FAT to disk.  In theory we should update every copy of the
  // FAT, but the first one is probably good enough.  A good OS would warn you
  // if the FATs are out of sync, but most OSes just read the first one without
  // complaining.
  if (trace_p) trace("syncing FAT\n");
  pi_sd_write(fs->fat, fs->fat_begin_lba, boot_sector.nsec_per_fat);
  // unimplemented();

}

// Given the starting cluster index, write the data in `data` over the
// pre-existing chain, adding new clusters to the end if necessary.
// writes file/directory data to the data region of the disk. 
// It follows the cluster chain and writes your actual bytes (directory entries, file contents) sector by sector.
static void write_cluster_chain(fat32_fs_t *fs, uint32_t start_cluster, uint8_t *data, uint32_t nbytes) {
  // Walk the cluster chain in the FAT, writing the in-memory data to the
  // referenced clusters.  If the data is longer than the cluster chain, find
  // new free clusters and add them to the end of the list.
  // Things to consider:
  //  - what if the data is shorter than the cluster chain?
  //  - what if the data is longer than the cluster chain?
  //  - the last cluster needs to be marked LAST_CLUSTER
  //  - when do we want to write the updated FAT to the disk to prevent
  //  corruption?
  //  - what do we do when nbytes is 0?
  //  - what about when we don't have a valid cluster to start with?
  //
  //  This is the main "write" function we'll be using; the other functions
  //  will delegate their writing operations to this.

  uint32_t cluster_size = fs->sectors_per_cluster * 512;

  // 0 bytes: we need to walk entire chain and free any clusters in this chain
  if (nbytes == 0) {
    if (start_cluster >= 2) {
      uint32_t cluster = start_cluster;
      while (1) {
        uint32_t entry = fs->fat[cluster]; // save how this entry was originally marked as. 
        fs->fat[cluster] = FREE_CLUSTER; // now, fat entry of this cluster is free. marked free.
        if (fat32_fat_entry_type(entry) == LAST_CLUSTER) // if this cluster was the last, there are no other clusters 
          break;
        cluster = entry;
      }
      write_fat_to_disk(fs);
    }
    return;
  }

  uint32_t bytes_left = nbytes;
  uint32_t offset = 0;
  uint32_t current_cluster = start_cluster;

  while (bytes_left > 0) {
    // write to each cluster
    uint32_t to_write = bytes_left < cluster_size ? bytes_left : cluster_size;
    uint32_t lba = cluster_to_lba(fs, current_cluster);
    pi_sd_write(data + offset, lba, fs->sectors_per_cluster);
    offset += to_write;
    bytes_left -= to_write;

    uint32_t entry = fs->fat[current_cluster];
    // if we wrote everything, this cluster should be the last. 
    if (bytes_left == 0) {
      // Mark this as the last cluster and free any tail.
      fs->fat[current_cluster] = 0x0FFFFFFF;
      if (fat32_fat_entry_type(entry) != LAST_CLUSTER) {
        uint32_t tail = entry;
        while (1) {
          uint32_t next = fs->fat[tail];
          fs->fat[tail] = FREE_CLUSTER;
          if (fat32_fat_entry_type(next) == LAST_CLUSTER)
            break;
          tail = next;
        }
      }
      break;
    }

    // if we have more bytes to write, but this is the last cluster, find free clsuter
    if (fat32_fat_entry_type(entry) == LAST_CLUSTER) {
      uint32_t new_cluster = find_free_cluster(fs, current_cluster);
      // set next cluster as this new_cluster
      fs->fat[current_cluster] = new_cluster;
      fs->fat[new_cluster] = 0x0FFFFFFF;
      current_cluster = new_cluster;
    } else {
      current_cluster = entry;
    }
  }
  // every write_cluster_chain call ends with a write_fat_to_disk 
  // because modifying a cluster chain (extending it, shrinking it, freeing tail clusters) 
  // always changes the FAT
  write_fat_to_disk(fs);
}

int fat32_rename(fat32_fs_t *fs, pi_dirent_t *directory, char *oldname, char *newname) {
  // TODO: Get the dirents `directory` off the disk, and iterate through them
  // looking for the file.  When you find it, rename it and write it back to
  // the disk (validate the name first).  Return 0 in case of an error, or 1
  // on success.
  // Consider:
  //  - what do you do when there's already a file with the new name?
  demand(init_p, "fat32 not initialized!");
  if (trace_p) trace("renaming %s to %s\n", oldname, newname);
  if (!fat32_is_valid_name(newname)) return 0;

  // TODO: get the dirents and find the right one
  uint32_t dir_n;
  fat32_dirent_t *dirents = get_dirents(fs, directory->cluster_id, &dir_n);
  int idx = find_dirent_with_name(dirents, dir_n, oldname);
  if (idx == -1) return 0;
  fat32_dirent_t *found_dirent = &dirents[idx];
  // TODO: update the dirent's name
  fat32_dirent_set_name(found_dirent, newname);


  // TODO: write out the directory, using the existing cluster chain (or
  // appending to the end); implementing `write_cluster_chain` will help
  // unimplemented();
  write_cluster_chain(fs, directory->cluster_id, (uint8_t *)dirents, dir_n * sizeof(fat32_dirent_t));
  return 1;

}

// Create a new directory entry for an empty file (or directory).
// Inside a specific directory 
pi_dirent_t *fat32_create(fat32_fs_t *fs, pi_dirent_t *directory, char *filename, int is_dir) {
  demand(init_p, "fat32 not initialized!");
  if (trace_p) trace("creating %s\n", filename);
  if (!fat32_is_valid_name(filename)) return NULL;

  // TODO: read the dirents and make sure there isn't already a file with the
  // same name
  // unimplemented();
  uint32_t dir_n;
  fat32_dirent_t *dirents = get_dirents(fs, directory->cluster_id, &dir_n);
  int idx = find_dirent_with_name(dirents, dir_n, filename);
  if (idx != -1) return NULL;


  // TODO: look for a free directory entry and use it to store the data for the
  // new file.  If there aren't any free directory entries, either panic or
  // (better) handle it appropriately by extending the directory to a new
  // cluster.
  // When you find one, update it to match the name and attributes
  // specified; set the size and cluster to 0.
  // unimplemented();
  int free_exists = 0;
  fat32_dirent_t* free_dirent;
  for (int i = 0; i < dir_n; i++) {
    if (fat32_dirent_free(&dirents[i])) {
      free_exists = 1;
      free_dirent = &dirents[i];
      fat32_dirent_set_name(&dirents[i], filename);
      dirents[i].attr = is_dir ? FAT32_DIR : FAT32_ARCHIVE;
      dirents[i].file_nbytes = 0;
      dirents[i].hi_start = 0;
      dirents[i].lo_start = 0;
      break;
    }
  }

  if (!free_exists) {
    panic("no free directory entry available");
  }
  // TODO: write out the updated directory to the disk
  write_cluster_chain(fs, directory->cluster_id, (uint8_t *)dirents, dir_n * sizeof(fat32_dirent_t));

  // unimplemented();

  // TODO: convert the dirent to a `pi_dirent_t` and return a (kmalloc'ed)
  // pointer
  // unimplemented();
  pi_dirent_t *dirent = kmalloc(sizeof(pi_dirent_t));
  *dirent = dirent_convert(free_dirent);
  return dirent;
}

// Delete a file, including its directory entry.
int fat32_delete(fat32_fs_t *fs, pi_dirent_t *directory, char *filename) {
  demand(init_p, "fat32 not initialized!");
  if (trace_p) trace("deleting %s\n", filename);
  if (!fat32_is_valid_name(filename)) return 0;
  // TODO: look for a matching directory entry, and set the first byte of the
  // name to 0xE5 to mark it as free
  // unimplemented();
  uint32_t dir_n;
  fat32_dirent_t *dirents = get_dirents(fs, directory->cluster_id, &dir_n);
  int idx = find_dirent_with_name(dirents, dir_n, filename);
  if (idx == -1) return 0;
  dirents[idx].filename[0] = 0xE5;

  // TODO: free the clusters referenced by this dirent
  // freeing these clusters = Deleting the data here. 
  uint32_t cur_cluster = fat32_cluster_id(&dirents[idx]);
  if (cur_cluster >= 2) {
    while (1) {
      uint32_t next = fs->fat[cur_cluster];
      fs->fat[cur_cluster] = FREE_CLUSTER;
      if (fat32_fat_entry_type(next) == LAST_CLUSTER)
        break;
      cur_cluster = next;
    }
  }
  // unimplemented();

  // TODO: write out the updated directory to the disk
  // unimplemented();
  write_cluster_chain(fs, directory->cluster_id, (uint8_t *)dirents, dir_n * sizeof(fat32_dirent_t));
  return 1;
}

int fat32_truncate(fat32_fs_t *fs, pi_dirent_t *directory, char *filename, unsigned length) {
  demand(init_p, "fat32 not initialized!");
  if (trace_p) trace("truncating %s\n", filename);

  // TODO: edit the directory entry of the file to list its length as `length` bytes,
  // then modify the cluster chain to either free unused clusters or add new
  // clusters.
  //
  // Consider: what if the file we're truncating has length 0? what if we're
  // truncating to length 0?
  // unimplemented();
  uint32_t dir_n;
  // directory->cluster_id is the starting cluster that of this directory. 
  // find all entries starting in this cluster 
  fat32_dirent_t *dirents = get_dirents(fs, directory->cluster_id, &dir_n);
  int idx = find_dirent_with_name(dirents, dir_n, filename);
  if (idx == -1) return 0;
  dirents[idx].file_nbytes = length;
  
  uint32_t bytes_left = length; 
  uint32_t cur_cluster = fat32_cluster_id(&dirents[idx]); 
  uint32_t cluster_size = fs->sectors_per_cluster * 512;
  while (fat32_fat_entry_type(cur_cluster) != LAST_CLUSTER) {
    uint32_t bytes_write = bytes_left > cluster_size ? cluster_size : bytes_left;
    uint32_t next = fs->fat[cur_cluster];
    if (bytes_left - bytes_write == 0) {
      if (bytes_left > 0) {
        fs->fat[cur_cluster] = LAST_CLUSTER;
      } else {
        fs->fat[cur_cluster] = FREE_CLUSTER;
      }
    }
    bytes_left -= bytes_write;
    cur_cluster= next; 
  }
  // TODO: write out the directory entry
  write_cluster_chain(fs, directory->cluster_id, (uint8_t *)dirents, dir_n * sizeof(fat32_dirent_t));
  return 0;
}

// after we create a directory entry with fat32_create, we call fat32_write to actually write the data
int fat32_write(fat32_fs_t *fs, pi_dirent_t *directory, char *filename, pi_file_t *file) {
  demand(init_p, "fat32 not initialized!");
  demand(directory->is_dir_p, "tried to use a file as a directory!");

  // TODO: Surprisingly, this one should be rather straightforward now.
  // - load the directory
  // - exit with an error (0) if there's no matching directory entry
  uint32_t dir_n;
  fat32_dirent_t *dirents = get_dirents(fs, directory->cluster_id, &dir_n);
  int idx = find_dirent_with_name(dirents, dir_n, filename);
  if (idx == -1) return 0;

  // - update the directory entry with the new size
  dirents[idx].file_nbytes = file->n_data;


  // - write out the file as clusters & update the FAT
  // - write out the directory entry
  // Special case: the file is empty to start with, so we need to update the
  // start cluster in the dirent
  uint32_t start_cluster = fat32_cluster_id(&dirents[idx]);

  // Special case: this file only has dirent created. start cluster will be 0 
  if (start_cluster == 0 && file->n_data > 0) {
    start_cluster = find_free_cluster(fs, 3); // start scanning for free cluster 
    fs->fat[start_cluster] = LAST_CLUSTER; // mark it as last cluster for now. write_cluster_chain will find more clusters if needed
    dirents[idx].hi_start = (start_cluster >> 16) & 0xFFFF;
    dirents[idx].lo_start = start_cluster & 0xFFFF;
  }

  // write data into the cluster of that directory entry 
  if (file->n_data > 0) {
    write_cluster_chain(fs, start_cluster, (uint8_t *)file->data, file->n_data);
  }

  // write entire directory(its entries) back to disk. 
  write_cluster_chain(fs, directory->cluster_id, (uint8_t *)dirents, dir_n * sizeof(fat32_dirent_t));
  return 1;
}

int fat32_flush(fat32_fs_t *fs) {
  demand(init_p, "fat32 not initialized!");
  // no-op
  return 0;
}