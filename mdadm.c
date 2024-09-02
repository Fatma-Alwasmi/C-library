#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"

void translate(uint32_t address, uint32_t *disk_id, uint32_t *block_id, uint32_t *offset) // function calculates disk num block num and offset
{
  *disk_id = address / JBOD_DISK_SIZE;
  *block_id = (address - JBOD_DISK_SIZE * (*disk_id)) / JBOD_BLOCK_SIZE;
  *offset = address % JBOD_BLOCK_SIZE;
}

int min(int num1, int num2) // gets the minimum of two nums
{
  if (num1 < num2)
  {
    return num1;
  }
  return num2;
}
int mount = 0;

int op(uint32_t command, uint32_t disk_id, uint32_t block_id) // puts the disk num, block num, and command in a 32-bit binary number
{
  uint32_t bits = (block_id & 0xff) | (disk_id & 0xff) << 22 | (command & 0xff) << 26;
  return bits;
}

void seek_and_read(uint32_t disk_id, uint32_t block_id, uint8_t *buf)
{
  jbod_client_operation(op(JBOD_SEEK_TO_DISK, disk_id, block_id), NULL);
  jbod_client_operation(op(JBOD_SEEK_TO_BLOCK, disk_id, block_id), NULL);
  jbod_client_operation(op(JBOD_READ_BLOCK, disk_id, block_id), &buf[0]);
}

void seek_and_write(uint32_t disk_id, uint32_t block_id, uint8_t *buf)
{
  jbod_client_operation(op(JBOD_SEEK_TO_DISK, disk_id, block_id), NULL);
  jbod_client_operation(op(JBOD_SEEK_TO_BLOCK, disk_id, block_id), NULL);
  jbod_client_operation(op(JBOD_WRITE_BLOCK, disk_id, block_id), &buf[0]);
}

int mdadm_mount(void) // mounts the disks
{
  if (mount == 0)
  {
    mount = 1;
    jbod_client_operation(op(JBOD_MOUNT, 0, 0), NULL);

    return 1;
  }
  return -1;
}

int mdadm_unmount(void) // unmounts the disks
{
  if (mount == 1)
  {
    mount = 0;
    jbod_client_operation(op(JBOD_UNMOUNT, 0, 0), NULL);
    return 1;
  }
  return -1;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf)
{
  if (mount == 0) // if the disks are unmounted, the function will not execute. only returns -1
  {
    return -1;
  }
  if (len > 1024) // if len is greater than 1024, the function will not read/execute. only returns -1.
  {
    return -1;
  }

  if ((addr + len) >= JBOD_DISK_SIZE * JBOD_NUM_DISKS)
  {
    return -1;
  }
  if (buf == NULL && len != 0)
  {
    return -1;
  }
  uint32_t disk_id = addr / JBOD_DISK_SIZE;                                // calculating disk num
  uint32_t block_id = (addr - JBOD_DISK_SIZE * disk_id) / JBOD_BLOCK_SIZE; // cauculating block num
  uint32_t offset = addr % JBOD_BLOCK_SIZE;                                // caulculating offset
  uint32_t last_disk_id = (addr + len) / JBOD_DISK_SIZE;
  uint32_t last_block_id = ((addr + len) - JBOD_DISK_SIZE * disk_id) / JBOD_BLOCK_SIZE;

  uint8_t temp_buf[256]; // temperary buffer to hold an intire block for us to then read from it.

  if (cache_enabled())
  {

    if (disk_id != last_disk_id || block_id != last_block_id) // read across blocks or read across disks
    {
      int num_read = 0;
      int copy_len = len;
      while (copy_len > 0)
      {
        if ((cache_lookup(disk_id, block_id, buf) == -1))
        {
          seek_and_read(disk_id, block_id, &temp_buf[0]);
          cache_insert(disk_id, block_id, &temp_buf[0]);

          int min_of_min = min(256, 256 - offset);
          int num_bytes_to_read_from_disk = min(copy_len, min_of_min);

          memcpy(buf + num_read, temp_buf + offset, num_bytes_to_read_from_disk);

          num_read += num_bytes_to_read_from_disk;
          copy_len -= num_bytes_to_read_from_disk;
          addr += num_bytes_to_read_from_disk;
          translate(addr, &disk_id, &block_id, &offset);
        }
        else
        {
          int min_of_min = min(256, 256 - offset);
          int num_bytes_to_read_from_disk = min(copy_len, min_of_min);
          num_read += num_bytes_to_read_from_disk;
          copy_len -= num_bytes_to_read_from_disk;
          addr += num_bytes_to_read_from_disk;
          translate(addr, &disk_id, &block_id, &offset);
        }
      }
      return len;
    }

    else if (block_id == last_block_id && (cache_lookup(disk_id, block_id, buf) == -1)) // read within block
    {
      seek_and_read(disk_id, block_id, &temp_buf[0]);
      cache_insert(disk_id, block_id, &temp_buf[0]);
      memcpy(buf, temp_buf + offset, len);
      return len;
    }
    return len;
  }

  //__________________________________________________________________
  //____________________read cache____________________________________

  else
  {

    if (disk_id != last_disk_id || block_id != last_block_id) // read across blocks or read across disks
    {
      int num_read = 0;
      int copy_len = len;
      while (copy_len > 0)
      {
        seek_and_read(disk_id, block_id, &temp_buf[0]);

        int min_of_min = min(256, 256 - offset);
        int num_bytes_to_read_from_disk = min(copy_len, min_of_min);

        memcpy(buf + num_read, temp_buf + offset, num_bytes_to_read_from_disk);

        num_read += num_bytes_to_read_from_disk;
        copy_len -= num_bytes_to_read_from_disk;
        addr += num_bytes_to_read_from_disk;
        translate(addr, &disk_id, &block_id, &offset);
      }

      return len;
    }

    else if (block_id == last_block_id) // read within block
    {
      seek_and_read(disk_id, block_id, &temp_buf[0]);
      memcpy(buf, temp_buf + offset, len);

      return len;
    }
  }

  return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf)
{
  if (mount == 0) // if the disks are unmounted, the function will not execute. only returns -1
  {
    return -1;
  }

  if (len > 1024) // if len is greater than 1024, the function will not read/execute. only returns -1.
  {
    return -1;
  }

  if ((addr + len) > JBOD_DISK_SIZE * JBOD_NUM_DISKS)
  {
    return -1;
  }
  if (buf == NULL && len != 0)
  {
    return -1;
  }
  uint32_t disk_id = addr / JBOD_DISK_SIZE;                                // calculating disk num
  uint32_t block_id = (addr - JBOD_DISK_SIZE * disk_id) / JBOD_BLOCK_SIZE; // cauculating block num
  uint32_t offset = addr % JBOD_BLOCK_SIZE;                                // caulculating offset
  uint8_t temp_buf[256];                                                   // temperary buffer to hold an intire block for us to then read from it.
  uint32_t last_disk_id = (addr + len) / JBOD_DISK_SIZE;
  uint32_t last_block_id = ((addr + len) - JBOD_DISK_SIZE * disk_id) / JBOD_BLOCK_SIZE;

  if (cache_enabled())
  {
    if (disk_id != last_disk_id || block_id != last_block_id) // write across blocks or across disks
    {
      int num_read = 0;
      int copy_len = len;
      while (copy_len > 0)
      {

        if (cache_lookup(disk_id, block_id, temp_buf) == -1)
        {

          seek_and_read(disk_id, block_id, &temp_buf[0]);

          int min_of_min = min(256, 256 - offset);
          int num_bytes_to_read_from_disk = min(copy_len, min_of_min);

          memcpy(temp_buf + offset, buf + num_read, num_bytes_to_read_from_disk);

          cache_insert(disk_id, block_id, &temp_buf[0]);

          seek_and_write(disk_id, block_id, &temp_buf[0]);
          num_read += num_bytes_to_read_from_disk;
          copy_len -= num_bytes_to_read_from_disk;
          addr += num_bytes_to_read_from_disk;
          translate(addr, &disk_id, &block_id, &offset);
        }
        else
        {
          int min_of_min = min(256, 256 - offset);
          int num_bytes_to_read_from_disk = min(copy_len, min_of_min);
          memcpy(temp_buf + offset, buf + num_read, num_bytes_to_read_from_disk);
          cache_update(disk_id, block_id, &temp_buf[0]);
          seek_and_write(disk_id, block_id, &temp_buf[0]);

          num_read += num_bytes_to_read_from_disk;
          copy_len -= num_bytes_to_read_from_disk;
          addr += num_bytes_to_read_from_disk;
          translate(addr, &disk_id, &block_id, &offset);
        }
      }

      return len;
    }

    else if (block_id == last_block_id) // write within block
    {
      if (cache_lookup(disk_id, block_id, temp_buf) == -1)
      {
        seek_and_read(disk_id, block_id, &temp_buf[0]);
        memcpy(temp_buf + offset, buf, len);
        seek_and_write(disk_id, block_id, &temp_buf[0]);
        cache_insert(disk_id, block_id, temp_buf);

        return len;
      }
      else
      {
        memcpy(temp_buf + offset, buf, len);
        seek_and_write(disk_id, block_id, &temp_buf[0]);
        cache_update(disk_id, block_id, &temp_buf[0]);
        return len;
      }
    }

    return len;
  }

  //_______________________________cache_____________________
  //_________________________________________________________
  else
  {
    if (disk_id != last_disk_id || block_id != last_block_id) // write across blocks or across disks
    {
      int num_read = 0;
      int copy_len = len;
      while (copy_len > 0)
      {
        seek_and_read(disk_id, block_id, &temp_buf[0]);

        int min_of_min = min(256, 256 - offset);
        int num_bytes_to_read_from_disk = min(copy_len, min_of_min);

        memcpy(temp_buf + offset, buf + num_read, num_bytes_to_read_from_disk);

        seek_and_write(disk_id, block_id, &temp_buf[0]);

        num_read += num_bytes_to_read_from_disk;
        copy_len -= num_bytes_to_read_from_disk;
        addr += num_bytes_to_read_from_disk;
        translate(addr, &disk_id, &block_id, &offset);
      }

      return len;
    }

    else if (block_id == last_block_id) // write within block
    {
      seek_and_read(disk_id, block_id, &temp_buf[0]);
      memcpy(temp_buf + offset, buf, len);
      seek_and_write(disk_id, block_id, &temp_buf[0]);

      return len;
    }

    return len;
  }
}