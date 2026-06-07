#include "rpi.h"
#include "pi-sd.h"
#include "mbr.h"

mbr_t *mbr_read() {
  // Be sure to call pi_sd_init() before calling this function!
  pi_sd_init();
  // TODO: Read the MBR into a heap-allocated buffer.  Use `pi_sd_read` or
  // `pi_sec_read` to read 1 sector from LBA 0 into memory.
  // data = 512 bytes big, lba = 0, nsec = 1
  mbr_t* mbr= pi_sec_read(0, 1);
  // mrt_t* mbr

  // TODO: Verify that the MBR is valid. (see mbr_check)
  mbr_check(mbr);

  // TODO: Return the MBR.
  return mbr;
}