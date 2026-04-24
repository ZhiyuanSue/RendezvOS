#include <common/string.h>
#include <rendezvos/ipc/ipc_serial.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>

static int is_fmt_space(char c)
{
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int fmt_param_count(const char *fmt)
{
        int n = 0;
        for (; *fmt; ++fmt) {
                if (!is_fmt_space(*fmt))
                        ++n;
        }
        return n;
}

static error_t one_measure(char fc, va_list *ap, u32 *out_len)
{
        switch (fc) {
        case 'p':
                (void)va_arg(*ap, void *);
                *out_len = (u32)sizeof(void *);
                return REND_SUCCESS;
        case 'q':
                (void)va_arg(*ap, i64);
                *out_len = (u32)sizeof(i64);
                return REND_SUCCESS;
        case 'i':
                (void)va_arg(*ap, int);
                *out_len = (u32)sizeof(i32);
                return REND_SUCCESS;
        case 'u':
                (void)va_arg(*ap, u32);
                *out_len = (u32)sizeof(u32);
                return REND_SUCCESS;
        case 's':
        case 't': {
                char *s = va_arg(*ap, char *);
                *out_len = s ? ((u32)strlen(s) + 1u) : 0u;
                return REND_SUCCESS;
        }
        default:
                return -E_IN_PARAM;
        }
}

static error_t one_write(u8 *base, u32 cap, u32 *off, char fc, va_list *ap)
{
        u32 len;
        char *str_s = NULL;

        switch (fc) {
        case 'p':
                len = (u32)sizeof(void *);
                break;
        case 'q':
                len = (u32)sizeof(i64);
                break;
        case 'i':
                len = (u32)sizeof(i32);
                break;
        case 'u':
                len = (u32)sizeof(u32);
                break;
        case 's':
        case 't':
                str_s = va_arg(*ap, char *);
                len = str_s ? ((u32)strlen(str_s) + 1u) : 0u;
                break;
        default:
                return -E_IN_PARAM;
        }

        if (*off + 1u + 4u + len > cap)
                return -E_IN_PARAM;

        base[(*off)++] = (u8)fc;
        memcpy(base + *off, &len, sizeof(len));
        *off += 4u;

        switch (fc) {
        case 'p': {
                void *p = va_arg(*ap, void *);
                memcpy(base + *off, &p, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 'q': {
                i64 v = va_arg(*ap, i64);
                memcpy(base + *off, &v, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 'i': {
                i32 v = (i32)va_arg(*ap, int);
                memcpy(base + *off, &v, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 'u': {
                u32 v = va_arg(*ap, u32);
                memcpy(base + *off, &v, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 's':
        case 't':
                if (len && str_s)
                        memcpy(base + *off, str_s, len);
                *off += len;
                return REND_SUCCESS;
        default:
                return -E_IN_PARAM;
        }
}

error_t ipc_serial_measure_va(const char *fmt, va_list ap, u32 *total_out)
{
        if (!total_out)
                return -E_IN_PARAM;
        *total_out = 0;

        va_list ap_measure;
        va_copy(ap_measure, ap);
        u32 total = 4u;
        for (const char *f = fmt; *f; ++f) {
                if (is_fmt_space(*f))
                        continue;
                u32 l;
                if (one_measure(*f, &ap_measure, &l) != REND_SUCCESS) {
                        va_end(ap_measure);
                        return -E_IN_PARAM;
                }
                total += 1u + 4u + l;
        }
        va_end(ap_measure);

        *total_out = total;
        return REND_SUCCESS;
}

error_t ipc_serial_encode_into_va(void *buf, u32 total, const char *fmt,
                                  va_list ap)
{
        if (!buf || total < 4u)
                return -E_IN_PARAM;

        u32 n_param = (u32)fmt_param_count(fmt);
        memcpy(buf, &n_param, sizeof(n_param));

        u32 off = 4u;
        va_list ap_write;
        va_copy(ap_write, ap);
        for (const char *f = fmt; *f; ++f) {
                if (is_fmt_space(*f))
                        continue;
                if (one_write((u8 *)buf, total, &off, *f, &ap_write)
                    != REND_SUCCESS) {
                        va_end(ap_write);
                        return -E_IN_PARAM;
                }
        }
        va_end(ap_write);

        if (off != total)
                return -E_IN_PARAM;
        return REND_SUCCESS;
}

void *ipc_serial_encode_va(const char *fmt, u32 *out_len, va_list ap)
{
        if (!out_len)
                return NULL;

        u32 total;
        if (ipc_serial_measure_va(fmt, ap, &total) != REND_SUCCESS)
                return NULL;

        struct allocator *cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator)
                return NULL;

        u8 *buf = (u8 *)cpu_kallocator->m_alloc(cpu_kallocator, total);
        if (!buf)
                return NULL;

        if (ipc_serial_encode_into_va(buf, total, fmt, ap) != REND_SUCCESS) {
                cpu_kallocator->m_free(cpu_kallocator, buf);
                return NULL;
        }

        *out_len = total;
        return buf;
}

void *ipc_serial_encode_alloc(const char *fmt, u32 *out_len, ...)
{
        va_list ap;
        va_start(ap, out_len);
        void *p = ipc_serial_encode_va(fmt, out_len, ap);
        va_end(ap);
        return p;
}

static error_t one_read(const u8 *base, u32 buf_len, u32 *off, char expect,
                        va_list *ap)
{
        if (*off + 1u + 4u > buf_len)
                return -E_IN_PARAM;
        u8 tag = base[*off];
        (*off)++;
        u32 len;
        memcpy(&len, base + *off, sizeof(len));
        *off += 4u;
        if (*off + len > buf_len)
                return -E_IN_PARAM;
        if (tag != (u8)expect)
                return -E_IN_PARAM;

        switch (expect) {
        case 'p': {
                if (len != sizeof(void *))
                        return -E_IN_PARAM;
                void **out = va_arg(*ap, void **);
                if (!out)
                        return -E_IN_PARAM;
                memcpy(out, base + *off, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 'q': {
                if (len != sizeof(i64))
                        return -E_IN_PARAM;
                i64 *out = va_arg(*ap, i64 *);
                if (!out)
                        return -E_IN_PARAM;
                memcpy(out, base + *off, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 'i': {
                if (len != sizeof(i32))
                        return -E_IN_PARAM;
                i32 *out = va_arg(*ap, i32 *);
                if (!out)
                        return -E_IN_PARAM;
                memcpy(out, base + *off, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 'u': {
                if (len != sizeof(u32))
                        return -E_IN_PARAM;
                u32 *out = va_arg(*ap, u32 *);
                if (!out)
                        return -E_IN_PARAM;
                memcpy(out, base + *off, len);
                *off += len;
                return REND_SUCCESS;
        }
        case 's':
        case 't': {
                char **out = va_arg(*ap, char **);
                if (!out)
                        return -E_IN_PARAM;
                if (len == 0) {
                        *out = NULL;
                } else {
                        /* Ensure wire has at least the trailing NUL byte. */
                        if (base[*off + len - 1u] != '\0')
                                return -E_IN_PARAM;
                        *out = (char *)(base + *off);
                }
                *off += len;
                return REND_SUCCESS;
        }
        default:
                return -E_IN_PARAM;
        }
}

error_t ipc_serial_decode(const void *buf, u32 buf_len, const char *fmt, ...)
{
        if (!buf || buf_len < 4u)
                return -E_IN_PARAM;

        u32 n_wire;
        memcpy(&n_wire, buf, sizeof(n_wire));
        int n_fmt = fmt_param_count(fmt);
        if (n_wire != (u32)n_fmt)
                return -E_IN_PARAM;

        va_list ap;
        va_start(ap, fmt);
        u32 off = 4u;
        for (const char *f = fmt; *f; ++f) {
                if (is_fmt_space(*f))
                        continue;
                if (one_read((const u8 *)buf, buf_len, &off, *f, &ap)
                    != REND_SUCCESS) {
                        va_end(ap);
                        return -E_IN_PARAM;
                }
        }
        va_end(ap);

        if (off != buf_len)
                return -E_IN_PARAM;
        return REND_SUCCESS;
}
