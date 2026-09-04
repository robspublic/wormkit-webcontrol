#ifndef WKWEBCONTROL_CTASK_H
#define WKWEBCONTROL_CTASK_H

#include <functional>

#include "../Constants.h"

// Base class for all game tasks. WA's task tree is walked from the turn-game
// task; each task holds a child list. We read (never mutate) the tree to build
// the monitor snapshot.
//
// Layout mirrors WA 3.8.1 (wkRealTime reference):
//   +0x00 vtable
//   +0x04 parent
//   +0x08 children list (CTaskList overlay below)
//   +0x1C unknown
//   +0x20 classtype
//   +0x2C gameglobal pointer stored on the task
class CTask;

// Overlay matching the game's intrusive list. The child array is `data` with
// `count` entries. Field order/sizes must match the game (see reference CList).
struct CTaskList {
    int   max_size;   // +0x00 (list+0x08 overall)
    int   unk4;       // +0x04
    int   count;      // +0x08  (== CTask+0x10)
    CTask** data;     // +0x0C  (== CTask+0x14)
    void* hash_list;  // +0x10
};

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

    CTask* parent;          // 0x04
    CTaskList children;      // 0x08 .. 0x1B (5 dwords)
    int unknown1C;           // 0x1C
    Constants::ClassType classtype; // 0x20
    int unknown24;           // 0x24
    int unknown28;           // 0x28
    int gameglobal2c;        // 0x2C

    // Depth-first visit of this task and all descendants. Read-only.
    void traverse(const std::function<void(CTask*)>& cb) const {
        cb(const_cast<CTask*>(this));
        for (int i = 0; i < children.count; ++i) {
            CTask* child = children.data ? children.data[i] : nullptr;
            if (child) child->traverse(cb);
        }
    }
};

#endif // WKWEBCONTROL_CTASK_H
