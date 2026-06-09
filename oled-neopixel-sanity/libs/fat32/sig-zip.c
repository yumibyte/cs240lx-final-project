#include "sig-zip.h"
#include "miniz.h"

static void put_u16(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)(val & 0xff);
    buf[1] = (uint8_t)((val >> 8) & 0xff);
}

static void put_u32(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)(val & 0xff);
    buf[1] = (uint8_t)((val >> 8) & 0xff);
    buf[2] = (uint8_t)((val >> 16) & 0xff);
    buf[3] = (uint8_t)((val >> 24) & 0xff);
}

static unsigned entry_name_len(const char *name) {
    unsigned len = 0;
    while(name[len])
        len++;
    return len;
}

// ZIP method 8 expects raw deflate; mz_compress2 adds a zlib header/footer
static int sig_deflate_raw(const uint8_t *src, uint32_t src_len,
                           uint8_t *dst, mz_ulong *dst_len) {
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = src;
    stream.avail_in = src_len;
    stream.next_out = dst;
    stream.avail_out = (mz_uint32)*dst_len;

    int status = mz_deflateInit2(&stream, MZ_BEST_COMPRESSION, MZ_DEFLATED,
                                 -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
    if(status != MZ_OK)
        return status;

    status = mz_deflate(&stream, MZ_FINISH);
    if(status != MZ_STREAM_END) {
        mz_deflateEnd(&stream);
        return (status == MZ_OK) ? MZ_BUF_ERROR : status;
    }

    *dst_len = stream.total_out;
    return mz_deflateEnd(&stream);
}

int sig_zip_pack(const char *entry_name, const uint8_t *raw, uint32_t raw_len,
                 uint8_t **out_zip, uint32_t *out_len) {
    unsigned name_len = entry_name_len(entry_name);
    if(name_len == 0 || name_len > 255)
        return -1;

    mz_ulong comp_bound = mz_deflateBound(0, raw_len);
    uint8_t *comp = kmalloc((unsigned)comp_bound);
    mz_ulong comp_len = comp_bound;
    int status = sig_deflate_raw(raw, raw_len, comp, &comp_len);
    if(status != MZ_OK)
        return -1;

    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, raw, raw_len);

    uint32_t local_hdr = 30 + name_len;
    uint32_t central_hdr = 46 + name_len;
    uint32_t end_hdr = 22;
    uint32_t zip_len = local_hdr + (uint32_t)comp_len + central_hdr + end_hdr;

    uint8_t *zip = kmalloc(zip_len);
    uint8_t *p = zip;

    put_u32(p, 0x04034b50);
    p += 4;
    put_u16(p, 20);
    p += 2;
    put_u16(p, 0);
    p += 2;
    put_u16(p, 8);
    p += 2;
    put_u16(p, 0);
    p += 2;
    put_u16(p, 0);
    p += 2;
    put_u32(p, (uint32_t)crc);
    p += 4;
    put_u32(p, (uint32_t)comp_len);
    p += 4;
    put_u32(p, raw_len);
    p += 4;
    put_u16(p, (uint16_t)name_len);
    p += 2;
    put_u16(p, 0);
    p += 2;
    memcpy(p, entry_name, name_len);
    p += name_len;
    memcpy(p, comp, (unsigned)comp_len);
    p += comp_len;

    uint8_t *c = p;
    put_u32(c, 0x02014b50);
    c += 4;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 20);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 8);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u32(c, (uint32_t)crc);
    c += 4;
    put_u32(c, (uint32_t)comp_len);
    c += 4;
    put_u32(c, raw_len);
    c += 4;
    put_u16(c, (uint16_t)name_len);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u32(c, 0);
    c += 4;
    put_u32(c, 0);
    c += 4;
    memcpy(c, entry_name, name_len);
    c += name_len;

    put_u32(c, 0x06054b50);
    c += 4;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 0);
    c += 2;
    put_u16(c, 1);
    c += 2;
    put_u16(c, 1);
    c += 2;
    put_u32(c, central_hdr);
    c += 4;
    put_u32(c, local_hdr + (uint32_t)comp_len);
    c += 4;
    put_u16(c, 0);
    c += 2;

    *out_zip = zip;
    *out_len = zip_len;
    return 0;
}
