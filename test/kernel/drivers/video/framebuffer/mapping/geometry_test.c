#include "kernel/drivers/video/framebuffer/mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static OS_FRAMEBUFFER framebuffer = {0x80000000ULL,3145728,3145728,1024,768,1024,1};

// ====== check ======

static void check(int condition)
{
    ++checks;
    if (!condition) {
        fprintf(stderr,"FAIL: mapping geometry check %u\n",checks);
        exit(1);
    }
}

// ====== rejected_input ======

static void rejected_input(const OS_FRAMEBUFFER *input, uint32_t bits)
{
    FB_MAPPING_GEOMETRY output, before;
    memset(&output,0xa5,sizeof(output));
    before = output;
    check(!fb_mapping_geometry(input,bits,&output));
    check(!memcmp(&output,&before,sizeof(output)));
}

// ====== positive_and_absent ======

static void positive_and_absent(void)
{
    struct {
        uint64_t Low;
        FB_MAPPING_GEOMETRY Plan;
        uint64_t High;
    } guarded = {0x0123456789abcdefULL,{0},0xfedcba9876543210ULL};
    OS_FRAMEBUFFER input = framebuffer, absent = {0};
    check(fb_mapping_geometry(&input,36,&guarded.Plan));
    check(guarded.Plan.Physical == input.Base && !guarded.Plan.Offset);
    check(guarded.Plan.Pages == 768 && guarded.Plan.TablePages == 4);
    check(guarded.Plan.Used == input.Used);
    input.Base += 4092;
    check(fb_mapping_geometry(&input,36,&guarded.Plan));
    check(guarded.Plan.Physical == framebuffer.Base && guarded.Plan.Offset == 4092);
    check(guarded.Plan.Pages == 769 && guarded.Plan.TablePages == 4);
    check(fb_mapping_geometry(&absent,36,&guarded.Plan));
    check(!guarded.Plan.Physical && !guarded.Plan.Offset && !guarded.Plan.Pages &&
        !guarded.Plan.TablePages && !guarded.Plan.Used);
    check(guarded.Low == 0x0123456789abcdefULL && guarded.High == 0xfedcba9876543210ULL);
}

// ====== invalid_and_alias ======

static void invalid_and_alias(void)
{
    OS_FRAMEBUFFER input;
    FB_MAPPING_GEOMETRY output;
    unsigned index;
    rejected_input(NULL,36);
    rejected_input(&framebuffer,31);
    rejected_input(&framebuffer,53);
    check(!fb_mapping_geometry(&framebuffer,36,NULL));
    for (index = 0; index < sizeof(input); ++index) {
        input = framebuffer;
        ((unsigned char *)&input)[index] ^= 0x80;
        memset(&output,0x5a,sizeof(output));
        if (!fb_mapping_geometry(&input,36,&output)) {
            unsigned byte;
            for (byte = 0; byte < sizeof(output); ++byte)
                check(((unsigned char *)&output)[byte] == 0x5a);
        } else {
            check(output.Physical + output.Offset == input.Base);
            check(output.Used == input.Used && output.Pages > 0 && output.Pages <= 4321);
        }
    }
    input = framebuffer;
    check(!fb_mapping_geometry(&input,36,(FB_MAPPING_GEOMETRY *)(void *)&input));
    check(!memcmp(&input,&framebuffer,sizeof(input)));
    input.Used -= 4;
    rejected_input(&input,36);
    input = (OS_FRAMEBUFFER){0};
    input.Width = 1;
    rejected_input(&input,36);
}

// ====== maximum_and_repeated_plans ======

static void maximum_and_repeated_plans(void)
{
    OS_FRAMEBUFFER input = {8188,33554432,17694720,1920,1080,4096,0};
    FB_MAPPING_GEOMETRY output;
    unsigned index;
    check(fb_mapping_geometry(&input,52,&output));
    check(output.Physical == 4096 && output.Offset == 4092 && output.Pages == 4321);
    check(output.TablePages == 11);
    for (index = 0; index < 10000; ++index) {
        input.Base = 0x80000000ULL + (index % 1024) * 4;
        check(fb_mapping_geometry(&input,36,&output));
        check(output.Offset == input.Base % 4096);
        check(output.Pages == (input.Base % 4096 + input.Used + 4095) / 4096);
        check(output.TablePages == 11 && output.Used == input.Used);
    }
}

// ====== main ======

int main(void)
{
    positive_and_absent();
    invalid_and_alias();
    maximum_and_repeated_plans();
    printf("PASS: %u mapping checks; 10000 plans; no allocation\n",checks);
    return 0;
}
