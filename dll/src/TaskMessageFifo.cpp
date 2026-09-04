#include "TaskMessageFifo.h"

#include "Hooks.h"
#include "Log.h"

// Resolved addresses of the game's FIFO routines (WA 3.8.1).
static DWORD addrTaskMessageSend = 0;
static DWORD addrFifoMakeSpace = 0;

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

int TaskMessageFifo::callFifoMakeSpace(TaskMessageFifo* fifo, size_t tsize) {
    int retv;
    _asm mov eax, fifo
    _asm mov ecx, tsize
    _asm call addrFifoMakeSpace
    _asm mov retv, eax
    return retv;
}

void TaskMessageFifo::install() {
    addrTaskMessageSend = _ScanPattern(
        "TaskMessageSend",
        "\x56\x8B\xF1\x83\xC6\x03\x83\xE6\xFC\x8D\x4E\x04\xE8\x00\x00\x00\x00\x85\xC0\x75\x04\x5E\xC2\x08\x00\x85\xF6\x8B\x4C\x24\x08\x89\x08\x74\x12\x8B\x54\x24\x0C\x56",
        "??????xxxxxxx????xxxxxxxxxxxxxxxxxxxxxxx");
    addrFifoMakeSpace = _ScanPattern(
        "FifoMakeSpace",
        "\x53\x56\x8B\x70\x08\x57\x8B\xF9\x8B\x48\x0C\x8D\x57\x0B\x83\xE2\xFC\x3B\xF1\x8D\x1C\x16\x7D\x0A\x3B\xD9\x7C\x17\x5F\x5E\x33\xC0\x5B\xC3",
        "??????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    Log::info("TaskMessageFifo::install (input FIFO send ready)");
}
