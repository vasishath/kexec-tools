#include <stdint.h>
#include <stdio.h>
#include <libfdt.h>

#include "../../kexec.h"
#include "mach.h"

#define INVALID_SOC_REV_ID 0xFFFFFFFF

struct msm_id
{
    uint32_t platform_id;
    uint32_t hardware_id;
    uint32_t soc_rev;
    uint32_t board_rev;
};

struct board_id
{
    uint32_t b_id;
    uint32_t reserved;
};

static uint32_t hammerhead_dtb_compatible(void *dtb, struct msm_id *devid, struct msm_id *dtb_id, struct board_id *mi_id, struct board_id *xm_id)
{
    int root_offset;
    const void *prop, *prop2;
    int len, len2;

    root_offset = fdt_path_offset(dtb, "/");
    if (root_offset < 0)
    {
        fprintf(stderr, "DTB: Couldn't find root path in dtb!\n");
        return 0;
    }

    prop2 = fdt_getprop(dtb, root_offset, "qcom,board-id", &len2);
    prop = fdt_getprop(dtb, root_offset, "qcom,msm-id", &len);
    if (!prop2 || len2 <= 0) {
        printf("DTB: qcom,board-id entry not found\n");
        return 0;
    } else if (len2 == 8) {
        printf("DTB: qcom,board-id entry found\n");
        printf("DTB: size is %d\n", len2);
    }
    
    
    
    if (!prop || len <= 0) {
        printf("DTB: qcom,msm-id entry not found\n");
        return 0;
    } else if(len == 12) {
        printf("DTB: qcom,msm-id entry found\n");
        dtb_id->board_rev = 0;
    } else if (len < (int)sizeof(struct msm_id)) {
        printf("DTB: qcom,msm-id entry size mismatch (%d != %d)\n",
            len, sizeof(struct msm_id));
        return 0;
    } else {
        printf("DTB: qcom,msm-id size is %d\n", len);
    }


    xm_id->b_id = fdt32_to_cpu(((const struct board_id *)prop2)->b_id);
    xm_id->reserved = fdt32_to_cpu(((const struct board_id *)prop2)->reserved);
    dtb_id->platform_id = fdt32_to_cpu(((const struct msm_id *)prop)->platform_id);
    dtb_id->hardware_id = fdt32_to_cpu(((const struct msm_id *)prop)->hardware_id);
    dtb_id->soc_rev = fdt32_to_cpu(((const struct msm_id *)prop)->soc_rev);
    if(len > 12)
        dtb_id->board_rev = fdt32_to_cpu(((const struct msm_id *)prop)->board_rev);

    //printf("DTB: found dtb platform %u hw %u soc 0x%x board %u\n",
    //      dtb_id->platform_id, dtb_id->hardware_id, dtb_id->soc_rev, dtb_id->board_rev);

    if (dtb_id->platform_id != devid->platform_id ||
        dtb_id->hardware_id != devid->hardware_id) {
        printf("Hardware or platform id not equal\n");
        return 0;
    }
    if (xm_id->b_id != mi_id->b_id || 
        xm_id->reserved != mi_id->reserved) {
        printf("Board id not equal\n");
        return 0;
    }

    return 1;
}

static int hammerhead_choose_dtb(const char *dtb_img, off_t dtb_len, char **dtb_buf, off_t *dtb_length)
{
    char *dtb = (char*)dtb_img;
    char *dtb_end = dtb + dtb_len;
    FILE *f, *f2;
    struct msm_id devid, dtb_id;
    struct board_id xm_id, mi_id;
    char *bestmatch_tag = NULL;
    size_t id_read = 0;
    size_t board_read = 0;
    uint32_t bestmatch_tag_size;
    uint32_t bestmatch_soc_rev_id = INVALID_SOC_REV_ID;
    uint32_t bestmatch_board_rev_id = INVALID_SOC_REV_ID;

    f = fopen("/proc/device-tree/qcom,msm-id", "r");
    f2 = fopen("/proc/device-tree/qcom,board-id", "r");
    if(!f || !f2)
    {
        fprintf(stderr, "DTB: Couldn't open device tree!\n");
        return 0;
    }

    id_read = fread(&devid, 1, sizeof(struct msm_id), f);
    board_read = fread(&mi_id, 1, sizeof(struct board_id), f2);
    fclose(f);

    mi_id.b_id = fdt32_to_cpu(mi_id.b_id);
    mi_id.reserved = fdt32_to_cpu(mi_id.reserved);
    devid.platform_id = fdt32_to_cpu(devid.platform_id);
    devid.hardware_id = fdt32_to_cpu(devid.hardware_id);
    devid.soc_rev = fdt32_to_cpu(devid.soc_rev);
    if(id_read > 12)
        devid.board_rev = fdt32_to_cpu(devid.board_rev);
    else
        devid.board_rev = 0;

    printf("DTB: platform %u hw %u soc 0x%x board %u\n",
            devid.platform_id, devid.hardware_id, devid.soc_rev, devid.board_rev);
    printf("DTB: board_id %u reserved %u\n",
            mi_id.b_id, mi_id.reserved);
    
    while(dtb + sizeof(struct fdt_header) < dtb_end)
    {
        uint32_t dtb_soc_rev_id;
        struct fdt_header dtb_hdr;
        uint32_t dtb_size;

        /* the DTB could be unaligned, so extract the header,
         * and operate on it separately */
        memcpy(&dtb_hdr, dtb, sizeof(struct fdt_header));
        if (fdt_check_header((const void *)&dtb_hdr) != 0 ||
            (dtb + fdt_totalsize((const void *)&dtb_hdr) > dtb_end))
        {
            fprintf(stderr, "DTB: Invalid dtb header!\n");
            break;
        }
        dtb_size = fdt_totalsize(&dtb_hdr);

        if(hammerhead_dtb_compatible(dtb, &devid, &dtb_id, &mi_id, &xm_id))
        {
            if (dtb_id.soc_rev == devid.soc_rev &&
                dtb_id.board_rev == devid.board_rev &&
                xm_id.b_id == mi_id.b_id &&
                xm_id.reserved == mi_id.reserved)
            {
                *dtb_buf = xmalloc(dtb_size);
                memcpy(*dtb_buf, dtb, dtb_size);
                *dtb_length = dtb_size;
                printf("DTB: match 0x%x %u, my id 0x%x %u, len %u\n",
                        dtb_id.soc_rev, dtb_id.board_rev,
                        devid.soc_rev, devid.board_rev, dtb_size);
                printf("DTB: board match %x %u, my id %x %u, len %u\n",
                        xm_id.b_id, xm_id.reserved,
                        mi_id.b_id, mi_id.reserved, dtb_size);
                return 1;
            }
            else if(dtb_id.soc_rev <= devid.soc_rev &&
                    dtb_id.board_rev < devid.board_rev &&
                    xm_id.b_id == mi_id.b_id &&
                   xm_id.reserved == mi_id.reserved)
            {
                if((bestmatch_soc_rev_id == INVALID_SOC_REV_ID) ||
                    (bestmatch_soc_rev_id < dtb_id.soc_rev) ||
                    (bestmatch_soc_rev_id == dtb_id.soc_rev &&
                    bestmatch_board_rev_id < dtb_id.board_rev))
                {
                    bestmatch_tag = dtb;
                    bestmatch_tag_size = dtb_size;
                    bestmatch_soc_rev_id = dtb_id.soc_rev;
                    bestmatch_board_rev_id = dtb_id.board_rev;
                }
            }
        }

        /* goto the next device tree if any */
        dtb += dtb_size;

        // try to skip padding in standalone dtb.img files
        while(dtb < dtb_end && *dtb == 0)
            ++dtb;
    }

    if(bestmatch_tag) {
        printf("DTB: bestmatch 0x%x %u, my id 0x%x %u\n",
                bestmatch_soc_rev_id, bestmatch_board_rev_id,
                devid.soc_rev, devid.board_rev);
        *dtb_buf = xmalloc(bestmatch_tag_size);
        memcpy(*dtb_buf, bestmatch_tag, bestmatch_tag_size);
        *dtb_length = bestmatch_tag_size;
        return 1;
    }
    printf("No matching DTBs found\n");
    return 0;
}

static int hammerhead_add_extra_regs(void *dtb_buf)
{
    FILE *f;
    uint32_t reg;
    int res;
    int off;

    off = fdt_path_offset(dtb_buf, "/memory");
    if (off < 0)
    {
        fprintf(stderr, "DTB: Could not find memory node.\n");
        return -1;
    }

    f = fopen("/proc/device-tree/memory/reg", "r");
    if(!f)
    {
        fprintf(stderr, "DTB: Failed to open /proc/device-tree/memory/reg!\n");
        return -1;
    }

    fdt_delprop(dtb_buf, off, "reg");

    while(fread(&reg, sizeof(reg), 1, f) == 1)
        fdt_appendprop(dtb_buf, off, "reg", &reg, sizeof(reg));

    fclose(f);
    return 0;
}

const struct arm_mach arm_mach_hammerhead = {
    .boardnames = { "hammerhead", "bacon", "d851", "cancro", NULL },
    .choose_dtb = hammerhead_choose_dtb,
    .add_extra_regs = hammerhead_add_extra_regs,
};
