#pragma once

#include "libtphd/assert.h"
#include "libtphd/types.h"

typedef u32 fpc_ProcID;

#define fpcM_ERROR_PROCESS_ID 0xFFFFFFFFu

struct process_profile_definition {
    u32 layer_id;
    u16 list_id;
    u16 list_priority;
    s16 name;
    u8  field_0x0a[2];
    void* methods;
    u32 process_size;
    u32 unk_size;
    u32 parameters;
};

TPHD_ASSERT_OFFSET(process_profile_definition, process_size, 0x10);
TPHD_ASSERT_SIZE(process_profile_definition, 0x1C);

struct base_process_class {
    int   type;
    fpc_ProcID id;
    s16   name;
    s8    field_0x0a;
    u8    pause_flag;
    s8    init_state;
    u8    create_phase;
    s16   profname;
    process_profile_definition* profile;
    void* create_req;
    u8    field_0x18[0xA8 - 0x18];
    void* methods;
    void* append;
    u32   parameters;
    int   subtype;
};

TPHD_ASSERT_OFFSET(base_process_class, id, 0x04);
TPHD_ASSERT_OFFSET(base_process_class, name, 0x08);
TPHD_ASSERT_OFFSET(base_process_class, init_state, 0x0C);
TPHD_ASSERT_OFFSET(base_process_class, profile, 0x10);
TPHD_ASSERT_OFFSET(base_process_class, append, 0xAC);
TPHD_ASSERT_OFFSET(base_process_class, parameters, 0xB0);
TPHD_ASSERT_SIZE(base_process_class, 0xB8);
