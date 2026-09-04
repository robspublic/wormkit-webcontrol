#ifndef WKWEBCONTROL_CTASK_H
#define WKWEBCONTROL_CTASK_H

#include <functional>

#include "../Constants.h"

// Base class for all game tasks. WA's task tree is walked from GameGlobal; each
// task dispatches messages to its children via a virtual HandleMessage.
//
// Only the vtable slots and fields we actually use are named. Field offsets
// mirror the reference RE of WA 3.8.1.
// TODO(offsets): confirm the vtable slot indices and the CTask layout below
// against WA 3.8.1 before relying on them.
class CTask {
public:
    virtual int vtable0(int a2) = 0;
    virtual int vtable4_Free(int heap) = 0;
    virtual int vtable8_HandleMessage(CTask* sender, Constants::TaskMessage mtype,
                                      size_t size, void* data) = 0;
    virtual int vtableC(int a2, int a3, int a4) = 0;
    virtual int vtable10(int a2, int a3, int a4) = 0;
    virtual int vtable14(int a2) = 0;
    virtual int vtable18() = 0;

    // 0x04: parent task pointer.
    CTask* parent;

    // 0x08..0x1F: intrusive child list. Layout matches the game's CList; kept
    // opaque here since we don't traverse children in the initial scaffold.
    char _pad08[0x20 - 0x08];

    // 0x20: class-type tag identifying the concrete task type.
    Constants::ClassType classtype;

    char _pad24[0x30 - 0x24];
};

#endif // WKWEBCONTROL_CTASK_H
