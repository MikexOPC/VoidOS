#include "arch/x86/kernel/serial/queue/queue.h"
static X86_UART_QUEUE queue, saved;
static BOOT_U32 environment = 1;
int x86_exception_environment(void) { return (int)environment; }

// ====== fixture vergine e copia byte per byte senza runtime host implicito ===== //

static int reset(void)
{
    BOOT_U32 byte;
    environment = 1;
    for (byte = 0; byte < sizeof(queue); ++byte) ((unsigned char *)&queue)[byte] = 0;
    return x86_uart_queue_initialize(&queue);
}
static void save(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(queue); ++byte)
        ((unsigned char *)&saved)[byte] = ((unsigned char *)&queue)[byte];
}
static int unchanged(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(queue); ++byte)
        if (((unsigned char *)&saved)[byte] != ((unsigned char *)&queue)[byte]) return 0;
    return 1;
}

// ====== coda piena copia privata e conferma soltanto della testa ===== //

static int lifecycle(void)
{
    unsigned char source[64];
    X86_UART_PACKET packet;
    BOOT_U64 id = 99;
    BOOT_U32 index, byte;
    if (!reset() || x86_uart_queue_initialize(&queue)) return 1;
    for (index = 0; index < 3; ++index) {
        for (byte = 0; byte < 64; ++byte) source[byte] = (unsigned char)(index+byte);
        if (x86_uart_queue_submit(&queue,source,64,&id) != X86_UART_QUEUE_OK || id != index+1) return 2;
    }
    save(); id = 99;
    if (x86_uart_queue_submit(&queue,source,1,&id) != X86_UART_QUEUE_FULL || id != 99 || !unchanged()) return 3;
    for (byte = 0; byte < 64; ++byte) source[byte] = 0;
    for (index = 0; index < 3; ++index) {
        if (!x86_uart_queue_front(&queue,&packet) || packet.Id != index+1 || packet.Length != 64) return 4;
        for (byte = 0; byte < 64; ++byte) if (packet.Data[byte] != (unsigned char)(index+byte)) return 5;
        save();
        if (x86_uart_queue_complete(&queue,index+2) || !unchanged()) return 6;
        if (!x86_uart_queue_complete(&queue,packet.Id) || x86_uart_queue_complete(&queue,packet.Id)) return 7;
    }
    if (queue.Count || queue.Completed != 3 || queue.Next != 4 || queue.Head ||
        x86_uart_queue_front(&queue,&packet) || !x86_uart_queue_validate(&queue)) return 8;
    for (byte = 0; byte < sizeof(queue.Slots); ++byte)
        if (((unsigned char *)queue.Slots)[byte]) return 9;
    return 0;
}

// ====== mille rotazioni con FIFO e copia del front che non muta la coda ===== //

static int rotations(void)
{
    unsigned char source[64];
    X86_UART_PACKET packet;
    BOOT_U64 id, oldest = 1;
    BOOT_U32 cycle, index, length;
    if (!reset()) return 1;
    for (cycle = 0; cycle < 1000; ++cycle) {
        while (queue.Count < 3) {
            length = (BOOT_U32)(queue.Next % 64)+1;
            for (index = 0; index < length; ++index) source[index] = (unsigned char)(queue.Next+index);
            if (x86_uart_queue_submit(&queue,source,length,&id) != X86_UART_QUEUE_OK) return 2;
        }
        if (!x86_uart_queue_front(&queue,&packet) || packet.Id != oldest) return 3;
        for (index = 0; index < packet.Length; ++index)
            if (packet.Data[index] != (unsigned char)(oldest+index)) return 4;
        for (index = packet.Length; index < 64; ++index) if (packet.Data[index]) return 5;
        packet.Data[0] ^= 255;
        if (!x86_uart_queue_front(&queue,&packet) || packet.Data[0] != (unsigned char)oldest) return 6;
        if (!x86_uart_queue_complete(&queue,oldest++)) return 7;
    }
    while (queue.Count) {
        if (!x86_uart_queue_complete(&queue,oldest++)) return 8;
    }
    return x86_uart_queue_validate(&queue) && queue.Next == oldest ? 0 : 9;
}

// ====== alias bounds IF e mutazioni fallite conservano output e coda ===== //

static int invalid(void)
{
    unsigned char source[64] = {7};
    BOOT_U64 id = 99;
    X86_UART_PACKET packet;
    if (!reset()) return 1;
    save();
    if (x86_uart_queue_submit(&queue,source,0,&id) ||
        x86_uart_queue_submit(&queue,source,65,&id) ||
        x86_uart_queue_submit(&queue,0,1,&id) ||
        x86_uart_queue_submit(&queue,queue.Slots[0].Data,1,&id) ||
        x86_uart_queue_submit(&queue,source,1,&queue.Next) ||
        x86_uart_queue_submit(&queue,(const unsigned char *)(~0ULL-1),64,&id) ||
        x86_uart_queue_front(&queue,(X86_UART_PACKET *)&queue) || id != 99 || !unchanged()) return 2;
    if (x86_uart_queue_submit(&queue,source,1,&id) != X86_UART_QUEUE_OK) return 3;
    save(); environment = 0;
    if (x86_uart_queue_submit(&queue,source,1,&id) || x86_uart_queue_front(&queue,&packet) ||
        x86_uart_queue_complete(&queue,1) || !unchanged()) return 4;
    environment = 1;
    return 0;
}

// ====== metadati e slot inutilizzati verificati bit per bit ===== //

static int corruption(void)
{
    unsigned char source = 8;
    BOOT_U64 id;
    BOOT_U32 bit;
    X86_UART_QUEUE moved;
    if (!reset()) return 1;
    for (bit = 0; bit < 3; ++bit)
        if (x86_uart_queue_submit(&queue,&source,1,&id) != X86_UART_QUEUE_OK) return 2;
    moved = queue;
    if (x86_uart_queue_validate(&moved)) return 3;
    for (bit = 0; bit < 32*8; ++bit) {
        ((unsigned char *)&queue)[bit/8] ^= (unsigned char)(1U<<(bit%8));
        if (x86_uart_queue_validate(&queue)) return 4;
        ((unsigned char *)&queue)[bit/8] ^= (unsigned char)(1U<<(bit%8));
    }
    for (bit = 0; bit < 32; ++bit) {
        queue.Slots[0].Reserved = 1U<<bit;
        if (x86_uart_queue_validate(&queue)) return 5;
    }
    queue.Slots[0].Reserved = 0;
    queue.Slots[0].Data[63] = 1;
    if (x86_uart_queue_validate(&queue)) return 6;
    queue.Slots[0].Data[63] = 0;
    if (!x86_uart_queue_complete(&queue,1)) return 7;
    queue.Slots[0].Id = 1;
    if (x86_uart_queue_validate(&queue)) return 8;
    return 0;
}

// ====== identificatori esauriti non vengono riutilizzati tramite wrap ===== //

static int exhaustion(void)
{
    unsigned char source = 3;
    BOOT_U64 id = 99;
    if (!reset()) return 1;
    queue.Next = ~0ULL-1; queue.Completed = ~0ULL-2;
    if (!x86_uart_queue_validate(&queue) ||
        x86_uart_queue_submit(&queue,&source,1,&id) != X86_UART_QUEUE_OK || id != ~0ULL-1) return 2;
    save(); id = 99;
    if (x86_uart_queue_submit(&queue,&source,1,&id) != X86_UART_QUEUE_EXHAUSTED ||
        id != 99 || !unchanged()) return 3;
    if (!x86_uart_queue_complete(&queue,~0ULL-1) || !x86_uart_queue_validate(&queue)) return 4;
    return 0;
}

// ====== output fallito preservato e ricevute disgiunte allineate ===== //

static int output_contract(void)
{
    X86_UART_PACKET output;
    BOOT_U64 id = 99;
    BOOT_U32 byte;
    unsigned char source = 7;
    if (!reset()) return 1;
    for (byte = 0; byte < sizeof(output); ++byte) ((unsigned char *)&output)[byte] = 0x55;
    if (x86_uart_queue_front(&queue,&output)) return 2;
    for (byte = 0; byte < sizeof(output); ++byte)
        if (((unsigned char *)&output)[byte] != 0x55) return 3;
    save();
    if (x86_uart_queue_submit(&queue,(const unsigned char *)&id,1,&id) ||
        x86_uart_queue_submit(&queue,&source,1,(BOOT_U64 *)((unsigned char *)&id+1)) ||
        id != 99 || !unchanged()) return 4;
    if (x86_uart_queue_submit(&queue,&source,1,&id) != X86_UART_QUEUE_OK) return 5;
    environment = 0;
    if (x86_uart_queue_front(&queue,&output)) return 6;
    for (byte = 0; byte < sizeof(output); ++byte)
        if (((unsigned char *)&output)[byte] != 0x55) return 7;
    environment = 1;
    return 0;
}

// ====== esiti distinti dei gruppi FIFO e ownership ===== //

__declspec(dllexport) int run_uart_queue_tests(void)
{
    int result = lifecycle();
    if (result) return 100+result;
    result = rotations(); if (result) return 200+result;
    result = invalid(); if (result) return 300+result;
    result = corruption(); if (result) return 400+result;
    result = exhaustion(); if (result) return 500+result;
    result = output_contract();
    if (result) return 600+result;
    return 0;
}
