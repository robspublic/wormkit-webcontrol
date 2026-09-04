#ifndef WKWEBCONTROL_CTASKWORM_H
#define WKWEBCONTROL_CTASKWORM_H

#include "CGameTask.h"
#include "../Constants.h"

// A worm task. Read-only here: fields (position, facing, weapon, active) are
// read to build the monitor snapshot. Control input is not written to these
// fields; it is injected through the game's input FIFO (see TaskMessageFifo).
//
// TODO(offsets): confirm every field offset against WA 3.8.1.
class CTaskWorm : public CGameTask {
public:
    char _padF0[0xFC - 0xF0];
    int  teamnumber_dwordFC;             // 0xFC
    int  wormnumber_dword100;            // 0x100
    int  active_dword104;                // 0x104
    char _pad108[0x164 - 0x108];
    int  state_counter_dword164;         // 0x164
    char _pad168[0x170 - 0x168];
    int  selected_weapon_unknown170;     // 0x170
    char _pad174[0x1A8 - 0x174];
    int  facing_direction_dword1A8;      // 0x1A8  (+1 / -1: move right / left)
    char _pad1AC[0x270 - 0x1AC];
    int  shooting_angle_dword270;        // 0x270  (aim)
    char _pad274[0x36C - 0x274];
    int  selected_weapon_entry_ptr36C;   // 0x36C  (ptr into weapon table)
    char _pad370[0x3FC - 0x370];

    static void install();

    bool isOwnedByMe();
    bool isTurnHolder();
};

#endif // WKWEBCONTROL_CTASKWORM_H
