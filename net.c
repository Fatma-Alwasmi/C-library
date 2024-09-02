#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
static bool nread(int fd, int len, uint8_t *buf)
{
  int t = 0;
  int bytes_read = 0;
  while (t < len)
  {
    int n = read(fd, &buf[bytes_read], len - bytes_read);
    t += n;
    bytes_read += n;

    if (t != len)
    {
      return false;
    }
  }

  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
static bool nwrite(int fd, int len, uint8_t *buf)
{
  int t = 0;
  int bytes_read = 0;
  while (t < len)
  {
    int n = write(fd, &buf[bytes_read], len - bytes_read);
    t += n;
    bytes_read += n;

    if (t != len)
    {
      return false;
    }
  }
  return true;
}

/* attenpts to recieve a packet from fd; returns true and success and false on failure*/
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block)
{
  uint8_t header[HEADER_LEN];
  uint16_t len;
  uint32_t new_opcode;
  uint16_t new_returnCode;

  if (nread(fd, HEADER_LEN, header) == true)
  {
    memcpy(&len, &header[0], sizeof(len));
    len = ntohs(len);
    if (len == HEADER_LEN)
    {
      memcpy(&new_opcode, header + 2, sizeof(new_opcode));
      memcpy(&new_returnCode, header + 6, sizeof(new_returnCode));
      new_opcode = ntohl(new_opcode);
      new_returnCode = ntohs(new_returnCode);
      *op = new_opcode;
      *ret = new_returnCode;
      return true;
    }
    if (len == HEADER_LEN + JBOD_BLOCK_SIZE)
    {
      memcpy(&new_opcode, header + 2, sizeof(new_opcode));
      memcpy(&new_returnCode, header + 6, sizeof(new_returnCode));
      nread(fd, JBOD_BLOCK_SIZE, block);
      new_opcode = ntohl(new_opcode);
      new_returnCode = ntohs(new_returnCode);
      *op = new_opcode;
      *ret = new_returnCode;
      return true;
    }
  }

  return false;
}

/* attempts to sent a packet to sd; returns true on success and false on failure */
static bool send_packet(int sd, uint32_t op, uint8_t *block)
{
  uint16_t len = 0;
  uint16_t returncode = 0;
  uint32_t command = op >> 26;
  uint32_t new_opcode = 0;
  uint8_t buf[HEADER_LEN + JBOD_BLOCK_SIZE];
  int location = 0;

  if (command == JBOD_WRITE_BLOCK)
  {
    len = htons(HEADER_LEN + JBOD_BLOCK_SIZE);
  }
  else
  {
    len = htons(HEADER_LEN);
  }

  new_opcode = htonl(op);
  returncode = htons(returncode);
  if (command == JBOD_WRITE_BLOCK)
  {
    memcpy(&buf[location], &len, sizeof(len));
    location += sizeof(len);
    memcpy(&buf[location], &new_opcode, sizeof(new_opcode));
    location += sizeof(new_opcode);
    memcpy(&buf[location], &returncode, sizeof(returncode));
    location += sizeof(returncode);
    memcpy(&buf[location], &block[0], JBOD_BLOCK_SIZE);

    if (nwrite(sd, HEADER_LEN + JBOD_BLOCK_SIZE, buf) == true)
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    memcpy(&buf[location], &len, sizeof(len));
    location += sizeof(len);
    memcpy(&buf[location], &new_opcode, sizeof(new_opcode));
    location += sizeof(new_opcode);
    memcpy(&buf[location], &returncode, sizeof(returncode));
    location += sizeof(returncode);
    if (nwrite(sd, HEADER_LEN, buf) == true)
    {
      return true;
    }
    else
    {
      return false;
    }
  }
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not.*/
bool jbod_connect(const char *ip, uint16_t port)
{

  struct sockaddr_in caddr;
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if (inet_aton(ip, &caddr.sin_addr) == 0)
  {
    return false;
  }

  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1)
  {
    return false;
  }

  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1)
  {
    return false;
  }
  return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void)
{
  close(cli_sd);
  cli_sd = -1;
}

/* sends the JBOD operation to the server (use the send_packet function) and receives
(use the recv_packet function) and processes the response.*/
int jbod_client_operation(uint32_t op, uint8_t *block)
{
  uint32_t new_opcode;
  uint16_t new_returncode;

  if (send_packet(cli_sd, op, block) == false)
  {
    return -1;
  }

  if (recv_packet(cli_sd, &new_opcode, &new_returncode, block) == false)
  {
    return -1;
  }
  if (new_returncode == -1)
  {
    return -1;
  }
  return 0;
}