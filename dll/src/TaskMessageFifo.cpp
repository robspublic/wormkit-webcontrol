#include "TaskMessageFifo.h"

#include "Hooks.h"
#include "Log.h"

// Resolved address of the game's TaskMessageSend routine (WA 3.8.1). It grows
// the FIFO itself as needed, so we don't need to resolve FifoMakeSpace.
static DWORD addrTaskMessageSend = 0;

// The game's TaskMessageSend has a non-standard calling convention: the fifo
// pointer arrives in EAX and the payload size in ECX, with (mtype, data) pushed
// on the stack. Mirror the reference's inline-asm shim exactly.
DWORD TaskMessageFifo::callTaskMessageSend(TaskMessageFifo* fifo, DWORD msize,
                                           Constants::TaskMessage mtype,
                                           void* data) {
    DWORD retv;
    _asm mov eax, fifo
    _asm mov ecx, msize
    _asm push data
    _asm push mtype
    _asm call addrTaskMessageSend
    _asm mov retv, eax
    return retv;
}

void TaskMessageFifo::install() {
    addrTaskMessageSend = _ScanPattern(
        "TaskMessageSend",
        "\x56\x8B\xF1\x83\xC6\x03\x83\xE6\xFC\x8D\x4E\x04\xE8\x00\x00\x00\x00\x85\xC0\x75\x04\x5E\xC2\x08\x00\x85\xF6\x8B\x4C\x24\x08\x89\x08\x74\x12\x8B\x54\x24\x0C\x56",
        "??????xxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx");
    Log::info("TaskMessageFifo::install (input FIFO send ready)");
}
