#ifndef RETAINS_VARS_H_
#define RETAINS_VARS_H_
#include "patcher/function_patcher.h"

extern replacement_data_t gbl_replacement_data;
extern u8 gAppStatus;
extern u64 gGameTitleID;
extern volatile u8 gSDInitDone;


extern void * ntfs_mounts;
extern int ntfs_mount_count;

extern struct buffer_store drc_store;
extern struct buffer_store tv_store;

#endif // RETAINS_VARS_H_
