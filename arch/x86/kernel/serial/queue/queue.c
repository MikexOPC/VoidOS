#include "queue.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/mm/window/page.h"
#include "lib/memory/address_ranges.h"

// ====== buffer kernel fidati con intervalli distinti dalle viste dispositivi ===== //

static int range(const void *pointer, BOOT_U64 bytes)
{
    BOOT_U64 address = (BOOT_U64)pointer;
    // Non prova il mapping di puntatori utente: questa API accetta memoria kernel.
    return pointer && bytes && address <= ~0ULL-bytes &&
        !os_address_ranges_overlap(address,bytes,X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(address,bytes,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(address,bytes,X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES);
}

// ====== intervalli disgiunti prima di copiare o pubblicare output ===== //

static int separate(const void *left, BOOT_U64 left_bytes,
    const void *right, BOOT_U64 right_bytes)
{
    return !os_address_ranges_overlap((BOOT_U64)left,left_bytes,
        (BOOT_U64)right,right_bytes);
}

// ====== azzeramento esplicito senza dipendenze dal runtime C ===== //

static void clear(void *pointer, BOOT_U32 bytes)
{
    BOOT_U32 byte;
    for (byte = 0; byte < bytes; ++byte) {
        ((volatile unsigned char *)pointer)[byte] = 0;
    }
}

// ====== copia byte per byte verso memoria posseduta dal destinatario ===== //

static void copy(void *output, const void *source, BOOT_U32 bytes)
{
    BOOT_U32 byte;
    for (byte = 0; byte < bytes; ++byte) {
        ((volatile unsigned char *)output)[byte] = ((const unsigned char *)source)[byte];
    }
}

// ====== nessun dato residuo negli slot non occupati ===== //

static int zero(const void *pointer, BOOT_U32 bytes)
{
    BOOT_U32 byte;
    for (byte = 0; byte < bytes; ++byte) {
        if (((const unsigned char *)pointer)[byte]) {
            return 0;
        }
    }
    return 1;
}

// ====== owner stabile allineato prima di leggere i metadati ===== //

static int owner(const X86_UART_QUEUE *queue)
{
    return range(queue,sizeof(*queue)) && !((BOOT_U64)queue & 7);
}

// ====== payload attivo con identita FIFO e coda di byte azzerata ===== //

static int packet(const X86_UART_PACKET *item, BOOT_U64 id)
{
    if (item->Id != id || item->Reserved ||
        !x86_uart_tx_bounds(item->Length,0,0)) {
        return 0;
    }
    return zero(item->Data+item->Length,X86_UART_TX_CAPACITY-item->Length);
}

// ====== validazione completa bounded dei tre slot senza mutazioni ===== //

int x86_uart_queue_validate(const X86_UART_QUEUE *queue)
{
    BOOT_U32 slot, ordinal;
    if (!owner(queue) || queue->Self != (BOOT_U64)queue ||
        !x86_uart_queue_state(queue->Head,queue->Count,queue->Next,queue->Completed)) {
        return 0;
    }
    for (slot = 0; slot < X86_UART_QUEUE_CAPACITY; ++slot) {
        int active = 0;
        for (ordinal = 0; ordinal < queue->Count; ++ordinal) {
            if (x86_uart_queue_slot(queue->Head,ordinal) != slot) {
                continue;
            }
            // Rust ha gia provato Completed+Count+1 senza overflow.
            if (!packet(&queue->Slots[slot],queue->Completed+ordinal+1)) {
                return 0;
            }
            active = 1;
            break;
        }
        if (!active && !zero(&queue->Slots[slot],sizeof(queue->Slots[slot]))) {
            return 0;
        }
    }
    return 1;
}

// ====== inizializzazione soltanto su memoria vergine con IF zero ===== //

int x86_uart_queue_initialize(X86_UART_QUEUE *queue)
{
    if (!x86_exception_environment() || !owner(queue) || !zero(queue,sizeof(*queue))) {
        return 0;
    }
    queue->Self = (BOOT_U64)queue;
    queue->Next = 1;
    return x86_uart_queue_validate(queue);
}

// ====== contratto di ingresso e ricevuta senza alias della copia ===== //

static int submission(const X86_UART_QUEUE *queue, const unsigned char *source,
    BOOT_U32 bytes, const BOOT_U64 *receipt)
{
    return x86_exception_environment() && x86_uart_queue_validate(queue) &&
        x86_uart_tx_bounds(bytes,0,0) && range(source,bytes) &&
        range(receipt,sizeof(*receipt)) && !((BOOT_U64)receipt & 7) &&
        separate(source,bytes,queue,sizeof(*queue)) &&
        separate(receipt,sizeof(*receipt),queue,sizeof(*queue)) &&
        separate(source,bytes,receipt,sizeof(*receipt));
}

// ====== coda piena e identificatori esauriti non alterano dati o ricevuta ===== //

int x86_uart_queue_submit(X86_UART_QUEUE *queue, const unsigned char *source,
    BOOT_U32 bytes, BOOT_U64 *receipt)
{
    X86_UART_PACKET draft;
    BOOT_U32 slot;
    if (!submission(queue,source,bytes,receipt)) {
        return X86_UART_QUEUE_INVALID;
    }
    if (queue->Count == X86_UART_QUEUE_CAPACITY) {
        return X86_UART_QUEUE_FULL;
    }
    if (queue->Next == ~0ULL) {
        return X86_UART_QUEUE_EXHAUSTED;
    }
    slot = x86_uart_queue_slot(queue->Head,queue->Count);
    // Anche il risultato FFI viene controllato prima dell'indicizzazione C.
    if (slot >= X86_UART_QUEUE_CAPACITY) {
        return X86_UART_QUEUE_INVALID;
    }
    clear(&draft,sizeof(draft));
    draft.Id = queue->Next;
    draft.Length = bytes;
    copy(draft.Data,source,bytes);
    // Nessun IRQ o altro produttore accede alla coda durante questa pubblicazione.
    copy(&queue->Slots[slot],&draft,sizeof(draft));
    ++queue->Next;
    ++queue->Count;
    *receipt = draft.Id;
    return X86_UART_QUEUE_OK;
}

// ====== fotografia posseduta della testa senza estrarre il messaggio ===== //

int x86_uart_queue_front(const X86_UART_QUEUE *queue, X86_UART_PACKET *output)
{
    if (!x86_exception_environment() ||
        !x86_uart_queue_validate(queue) ||
        !queue->Count ||
        !range(output,sizeof(*output)) || ((BOOT_U64)output & 7) ||
        !separate(output,sizeof(*output),queue,sizeof(*queue))) {
        return 0;
    }
    copy(output,&queue->Slots[queue->Head],sizeof(*output));
    return 1;
}

// ====== rimozione solo della testa confermata dal chiamante dopo TX EOI TEMT ===== //

int x86_uart_queue_complete(X86_UART_QUEUE *queue, BOOT_U64 id)
{
    BOOT_U32 head;
    if (!x86_exception_environment() ||
        !x86_uart_queue_validate(queue) ||
        !queue->Count ||
        queue->Slots[queue->Head].Id != id) {
        return 0;
    }
    head = x86_uart_queue_slot(queue->Head,1);
    if (head >= X86_UART_QUEUE_CAPACITY) {
        return 0;
    }
    clear(&queue->Slots[queue->Head],sizeof(queue->Slots[queue->Head]));
    queue->Completed = id;
    queue->Head = head;
    --queue->Count;
    return 1;
}
