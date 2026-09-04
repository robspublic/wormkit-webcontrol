#ifndef WKWEBCONTROL_CGAMETASK_H
#define WKWEBCONTROL_CGAMETASK_H

#include "CTask.h"

// Base for in-world tasks (worms, projectiles, etc.). Adds physics-related
// vtable slots and common motion fields (position/speed).
//
// TODO(offsets): confirm layout against WA 3.8.1.
class CGameTask : public CTask {
public:
    virtual int vtable1C(int a2, int a3) = 0;
    virtual int vtable20(int a2, int a3) = 0;
    virtual int vtable24(int a2) = 0;
    virtual int vtable28(int a2) = 0;
    virtual int vtable2C_OnSink(int a2, int a3) = 0;
    virtual int vtable30() = 0;
    virtual int vtable34() = 0;
    virtual int vtable38_SetState(int a2) = 0;
    virtual int vtable3C() = 0;
    virtual int vtable40() = 0;
    virtual int vtable44(int a2, int a3, int a4) = 0;
    virtual int vtable48() = 0;

    char _pad30[0x44 - 0x30];
    int  state_dword44;              // 0x44
    int  suspended_physics_dword48;  // 0x48
    char _pad4C[0x84 - 0x4C];
    int  posX;                       // 0x84
    int  posY;                       // 0x88
    char _pad8C[0x90 - 0x8C];
    int  speedX;                     // 0x90
    int  speedY;                     // 0x94
    char _pad98[0xF0 - 0x98];
};

#endif // WKWEBCONTROL_CGAMETASK_H
