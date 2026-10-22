/* PCM waves in the store flash: the image check, the entries, the samples and the wave list (pcm_store.h). */
#include <stddef.h>
#include "pcm_store.h"
#include "wavetables.h"                     /* wavetable_crc32 */

static const uint8_t *pcm_img[PCM_AREAS];
static uint32_t pcm_n[PCM_AREAS];
static const PcmEntry *pcm_waves[PCM_AREAS * PCM_MAX_ENTRIES];   /* the WAVE entries in order, rebuilt on (de)attach */
static uint32_t pcm_nwaves;

static const PcmEntry *entry_at(const uint8_t *img, uint32_t i)
{
    return (const PcmEntry *)(const void *)(img + PCM_HDR_SIZE + i * PCM_ENTRY_SIZE);
}

static void list_waves(void)
{
    uint32_t a, i;
    pcm_nwaves = 0;
    for (a = 0; a < PCM_AREAS; a++)
        for (i = 0; i < pcm_n[a]; i++)
            if (entry_at(pcm_img[a], i)->kind == PCM_WAVE)
                pcm_waves[pcm_nwaves++] = entry_at(pcm_img[a], i);
}

/* a reference from an entry of area a, checked as far as area a can be: into a itself, it must name an entry of
 * `kind` there; into the other area, only its shape (that area is checked when the reference is used) */
static int ref_ok(uint32_t a, const uint8_t *img, uint32_t count, uint8_t ref, uint8_t kind)
{
    uint32_t ra = ref >> 6u, ri = ref & 63u;
    if (ra >= PCM_AREAS || ri == 63u)
        return 0;
    return ra != a || (ri < count && entry_at(img, ri)->kind == kind);
}

int pcm_store_check(uint32_t a, const uint8_t *img, uint32_t size)
{
    const PcmHdr *h = (const PcmHdr *)(const void *)img;
    uint32_t i, dir_end;
    if (!img || ((uintptr_t)img & 3u) || size < PCM_HDR_SIZE || a >= PCM_AREAS)
        return -1;
    if (h->magic != PCM_MAGIC || h->hcrc != wavetable_crc32(0, h, PCM_HDR_SIZE - 4u))
        return -1;
    if (h->version != PCM_VERSION || h->count > PCM_MAX_ENTRIES || h->len > size ||
        h->rsv[0] || h->rsv[1] || h->rsv[2])
        return -2;
    dir_end = PCM_HDR_SIZE + (uint32_t)h->count * PCM_ENTRY_SIZE;
    if (h->len < dir_end)
        return -2;
    for (i = 0; i < h->count; i++) {
        const PcmEntry *e = entry_at(img, i);
        if (e->rsv0 || e->rsv1)
            return -3;
        if (e->kind == PCM_LOOP || e->kind == PCM_ONESHOT) {
            uint32_t bytes = e->u.s.len + (e->kind == PCM_LOOP ? 1u : 0u);   /* a LOOP's guard byte */
            if (e->root > 127u || e->u.s.rate < 4000u || e->u.s.rate > 48000u || e->u.s.len < 2u ||
                e->u.s.len > 65535u ||              /* the oscillator's read positions are Q16 in 32 bits */
                e->u.s.rsv[0] || e->u.s.rsv[1])
                return -3;
            if (e->u.s.off < dir_end || e->u.s.off > h->len || bytes > h->len - e->u.s.off)
                return -3;                          /* the samples inside the image, after the entries */
        } else if (e->kind == PCM_WAVE) {
            const uint8_t *nm = (const uint8_t *)e->name;
            if (!nm[0] || e->root || e->u.w.rsv || e->u.w.rsv2 || e->u.w.rsv3[0] || e->u.w.rsv3[1])
                return -3;
            if (!ref_ok(a, img, h->count, e->u.w.loop_a, PCM_LOOP))
                return -3;
            if (e->u.w.loop_b != PCM_REF_NONE && !ref_ok(a, img, h->count, e->u.w.loop_b, PCM_LOOP))
                return -3;
            if (e->u.w.atk_ref != PCM_REF_NONE &&
                (e->u.w.atk_fw != PCM_REF_NONE || !ref_ok(a, img, h->count, e->u.w.atk_ref, PCM_ONESHOT)))
                return -3;
            if (e->u.w.atk_level > 127u || e->u.w.b_rate_q12 < 1024u || e->u.w.b_rate_q12 > 16384u)
                return -3;
        } else {
            return -3;
        }
    }
    if (wavetable_crc32(0, img + PCM_HDR_SIZE, h->len - PCM_HDR_SIZE) != h->crc)
        return -4;
    return h->count;
}

int pcm_store_attach(uint32_t a, const uint8_t *img, uint32_t size)
{
    int n;
    if (a >= PCM_AREAS)
        return -1;
    pcm_store_detach(a);
    n = pcm_store_check(a, img, size);
    if (n > 0) {
        pcm_img[a] = img;
        pcm_n[a] = (uint32_t)n;
        list_waves();
    }
    return n;
}

void pcm_store_detach(uint32_t a)
{
    if (a < PCM_AREAS) {
        pcm_n[a] = 0;
        pcm_img[a] = NULL;
        list_waves();
    }
}

uint32_t pcm_area_count(uint32_t a) { return a < PCM_AREAS ? pcm_n[a] : 0u; }

uint32_t pcm_area_used(uint32_t a)
{
    return a < PCM_AREAS && pcm_img[a] ? ((const PcmHdr *)(const void *)pcm_img[a])->len : 0u;
}

uint32_t pcm_entry_count(void)
{
    uint32_t a, n = 0;
    for (a = 0; a < PCM_AREAS; a++)
        n += pcm_n[a];
    return n;
}

const PcmEntry *pcm_entry(uint32_t i, const uint8_t **data)
{
    uint32_t a;
    for (a = 0; a < PCM_AREAS; a++) {
        if (i < pcm_n[a]) {
            const PcmEntry *e = entry_at(pcm_img[a], i);
            if (data)
                *data = e->kind == PCM_WAVE ? NULL : pcm_img[a] + e->u.s.off;
            return e;
        }
        i -= pcm_n[a];
    }
    if (data)
        *data = NULL;
    return NULL;
}

const PcmEntry *pcm_sample(uint8_t ref, uint8_t kind, const uint8_t **data)
{
    uint32_t a = ref >> 6u, i = ref & 63u;
    const PcmEntry *e;
    if (data)
        *data = NULL;
    if (a >= PCM_AREAS || i >= pcm_n[a])
        return NULL;
    e = entry_at(pcm_img[a], i);
    if (e->kind != kind)
        return NULL;
    if (data)
        *data = pcm_img[a] + e->u.s.off;
    return e;
}

uint32_t pcm_wave_count(void) { return pcm_nwaves; }

const PcmEntry *pcm_wave(uint32_t w) { return w < pcm_nwaves ? pcm_waves[w] : NULL; }
