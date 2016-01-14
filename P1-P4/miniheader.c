#include "miniheader.h"

#include <stdint.h>

void pack_unsigned_int(char* buf, unsigned int val)
{
    unsigned char* ubuf = (unsigned char *) buf;
    ubuf[0] = (val>>24) & 0xff;
    ubuf[1] = (val>>16) & 0xff;
    ubuf[2] = (val>>8) & 0xff;
    ubuf[3] = val & 0xff;
}

unsigned int unpack_unsigned_int(const char *buf)
{
    const uint8_t* ubuf = (const uint8_t*) buf;
    return (unsigned int) (ubuf[0]<<24) | (ubuf[1]<<16) | (ubuf[2]<<8) | ubuf[3];
}

void pack_unsigned_short(char* buf, unsigned short val)
{
    unsigned char* ubuf = (unsigned char *) buf;
    ubuf[0] = (val>>8) & 0xff;
    ubuf[1] = val & 0xff;
}

unsigned short unpack_unsigned_short(const char* buf)
{
    unsigned char* ubuf = (unsigned char *) buf;
    return (unsigned short) (ubuf[0]<<8) | ubuf[1];
}

void pack_address(char* buf, const network_address_t address)
{
    pack_unsigned_int(buf, address[0]);
    pack_unsigned_int(buf+sizeof(unsigned int), address[1]);
}

void unpack_address(const char* buf, network_address_t address)
{
    address[0] = unpack_unsigned_int(buf);
    address[1] = unpack_unsigned_int(buf+sizeof(unsigned int));
}
