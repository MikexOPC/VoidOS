#include "kernel/drivers/video/virtio/resources/scanout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,calls,fail_at;
static const uint32_t commands[12]={0x101,0x106,0x105,0x103,0x104,0x105,0x104,0x103,0x107,0x102};
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL presentation:%u call:%u\n",(unsigned)__LINE__,calls);exit(1);} } while (0)

// ====== word_private ======

static uint32_t word_private(const unsigned char *body,uint32_t at)
{
    return (uint32_t)body[at]|(uint32_t)body[at+1]<<8|(uint32_t)body[at+2]<<16|(uint32_t)body[at+3]<<24;
}

// ====== kernel_vgpu_queue_control ======

int kernel_vgpu_queue_control(VGPU_QUEUE *queue,uint32_t command,const unsigned char *body,uint32_t bytes)
{
    CHECK(calls<10 && commands[calls]==command && queue->State==1); ++calls;
    if (command==0x103) {
        CHECK(bytes==24 && word_private(body,16)==7 && word_private(body,20)==(calls==8?0U:1U));
        CHECK(word_private(body,8)==(calls==8?0U:1280U) && word_private(body,12)==(calls==8?0U:800U));
    }
    if (calls==5) CHECK(command==0x104 && bytes==24 && word_private(body,8)==1280 && word_private(body,12)==800 &&
        word_private(body,16)==1 && !word_private(body,20));
    if (calls==6 || calls==7) {
        CHECK(word_private(body,0)==16 && word_private(body,4)==24 && word_private(body,8)==64 && word_private(body,12)==32);
        if (calls==6) CHECK(bytes==32 && word_private(body,16)==(24*1280+16)*4 && !word_private(body,20) && word_private(body,24)==1 && !word_private(body,28));
        else CHECK(bytes==24 && word_private(body,16)==1 && !word_private(body,20));
    }
    return calls!=fail_at;
}

// ====== main ======

int main(void)
{
    for (unsigned cycle=0; cycle<100; ++cycle) for (fail_at=0; fail_at<=10; ++fail_at) {
        VGPU_QUEUE queue={0}; VGPU_RESOURCE resource={0}; calls=0;
        queue.Self=(uintptr_t)&queue; queue.State=1; queue.Buffers.Bits=52; queue.Scanout=7; queue.Width=1280; queue.Height=800;
        for (unsigned index=0; index<4; ++index) { queue.Buffers.Physical[index]=0x100000+4096*index; queue.Buffers.Device[index]=0x1001000+8192*index; }
        if (!kernel_vgpu_resource_open(&queue,&resource,0x200000,0x1100000,1280,800)) { CHECK(fail_at<=3 && resource.State==UINT32_MAX); continue; }
        CHECK(resource.State==3 && !resource.Bound);
        VGPU_RESOURCE copy=resource;
        CHECK(!kernel_vgpu_scanout_bind(&queue,&copy));
        queue.Width=1024; CHECK(!kernel_vgpu_scanout_bind(&queue,&resource) && !resource.Bound); queue.Width=1280;
        queue.Scanout=16; CHECK(!kernel_vgpu_scanout_bind(&queue,&resource) && !resource.Bound); queue.Scanout=7;
        if (!kernel_vgpu_scanout_bind(&queue,&resource)) {
            CHECK(fail_at==4 || fail_at==5); CHECK(resource.Bound==UINT32_MAX);
            CHECK(!kernel_vgpu_resource_close(&queue,&resource)); continue;
        }
        CHECK(resource.Bound==1 && !kernel_vgpu_resource_close(&queue,&resource));
        unsigned before=calls;
        CHECK(!kernel_vgpu_scanout_update(&queue,&resource,1279,0,2,1) && calls==before && resource.Bound==1);
        CHECK(!kernel_vgpu_scanout_update(&queue,&resource,0,0,0,1) && calls==before);
        queue.Scanout=6; CHECK(!kernel_vgpu_scanout_update(&queue,&resource,0,0,1,1) && calls==before); queue.Scanout=7;
        if (!kernel_vgpu_scanout_update(&queue,&resource,16,24,64,32)) {
            CHECK(fail_at==6 || fail_at==7); CHECK(resource.Bound==UINT32_MAX);
            CHECK(!kernel_vgpu_scanout_unbind(&queue,&resource) && !kernel_vgpu_resource_close(&queue,&resource)); continue;
        }
        if (!kernel_vgpu_scanout_unbind(&queue,&resource)) {
            CHECK(fail_at==8 && resource.Bound==UINT32_MAX && !kernel_vgpu_resource_close(&queue,&resource)); continue;
        }
        CHECK(!resource.Bound);
        CHECK(kernel_vgpu_resource_close(&queue,&resource)==(fail_at==0));
        CHECK(resource.State==(fail_at?UINT32_MAX:5) && calls==(fail_at?fail_at:10));
    }
    printf("PASS %u presentation checks, 1100 lifecycles, error at every command, unsafe close denied\n",checks);
    return 0;
}
