#include <string>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <malloc.h>
#include <sys/types.h>
#include <dirent.h>

#include <sys/stat.h>
#include <unistd.h>

#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/ax_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/nn_nim_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/padscore_functions.h"
#include "dynamic_libs/proc_ui_functions.h"

#include <utils/logger.h>
#include <fs/FSUtils.h>
#include <fs/sd_fat_devoptab.h>
#include <utils/utils.h>
#include <system/exception_handler.h>
#include <system/memory.h>

#include "common/retain_vars.h"
#include "common/common.h"
#include "plugin/PluginLoader.h"
#include "plugin/PluginInformation.h"

#include <utils/function_patcher.h>

#include <wups.h>
#include <iosuhax.h>
#include <fat.h>
#include <ntfs.h>

#include "main.h"
#include "utils.h"
#include "Application.h"
#include "patcher/function_patcher.h"
#include "patcher/hooks_patcher.h"
#include "myutils/mocha.h"
#include "myutils/libntfs.h"
#include "myutils/libfat.h"
#include "version.h"
#include "settings/CSettings.h"

static void ApplyPatches();
void CallHook(wups_loader_hook_type_t hook_type);
static void RestorePatches();
s32 isInMiiMakerHBL();

/* Entry point */
extern "C" int Menu_Main(int argc, char **argv){
    if(gAppStatus == 2){
        //"No, we don't want to patch stuff again.");
        return EXIT_RELAUNCH_ON_LOAD;
    }
    InitOSFunctionPointers();
    InitSocketFunctionPointers(); //For logging
    InitSysFunctionPointers();
    InitFSFunctionPointers();
    InitGX2FunctionPointers();
    InitSysFunctionPointers();
    InitVPadFunctionPointers();
    InitPadScoreFunctionPointers();
    InitAXFunctionPointers();
    InitProcUIFunctionPointers();

    log_init();

    DEBUG_FUNCTION_LINE("We have %d kb for plugins.\n",(PLUGIN_LOCATION_END_ADDRESS-getApplicationEndAddr())/1024);

    DEBUG_FUNCTION_LINE("Wii U Plugin System Loader %s\n",APP_VERSION);

    //setup_os_exceptions();

    DEBUG_FUNCTION_LINE("Mount SD partition\n");
    Init_SD_USB();

    s32 result = 0;

    //Reset everything when were going back to the Mii Maker
    if(isInMiiMakerHBL()){
        // Restore patches as the patched functions could change.
        RestorePatches();

        PluginLoader * pluginLoader  = PluginLoader::getInstance();
        std::vector<PluginInformation *> pluginList = pluginLoader->getPluginInformation("sd:/wiiu/plugins/");
        pluginLoader->loadAndLinkPlugins(pluginList);

        //!*******************************************************************
        //!                    Initialize heap memory                        *
        //!*******************************************************************
        DEBUG_FUNCTION_LINE("Initialize memory management\n");
        memoryInitialize();

        DEBUG_FUNCTION_LINE("Start main application\n");
        result = Application::instance()->exec();
        DEBUG_FUNCTION_LINE("Main application stopped result: %d\n",result);
        Application::destroyInstance();

        DEBUG_FUNCTION_LINE("Release memory\n");
        memoryRelease();
        CSettings::destroyInstance();
        PluginLoader::destroyInstance();
    }

    DEBUG_FUNCTION_LINE("Apply patches.\n");
    ApplyPatches();

    if(!isInMiiMakerHBL()){
        CallHook(WUPS_LOADER_HOOK_INIT_FUNCTION);
        return EXIT_RELAUNCH_ON_LOAD;
    }

    if(result == APPLICATION_CLOSE_APPLY){
        DEBUG_FUNCTION_LINE("Loading the system menu.\n");
        SYSLaunchMenu();
        return EXIT_RELAUNCH_ON_LOAD;
    }

    DEBUG_FUNCTION_LINE("Going back to the Homebrew Launcher\n");
    RestorePatches();
    DeInit();
    return EXIT_SUCCESS;
}

void ApplyPatches(){
    PatchInvidualMethodHooks(method_hooks_hooks, method_hooks_size_hooks, method_calls_hooks);
    for(int plugin_index=0;plugin_index<gbl_replacement_data.number_used_plugins;plugin_index++){
        new_PatchInvidualMethodHooks(&gbl_replacement_data.plugin_data[plugin_index]);
    }
}

void CallHook(wups_loader_hook_type_t hook_type){
    for(int plugin_index=0;plugin_index<gbl_replacement_data.number_used_plugins;plugin_index++){
        replacement_data_plugin_t * plugin_data = &gbl_replacement_data.plugin_data[plugin_index];
        DEBUG_FUNCTION_LINE("Checking hook functions for %s.\n",plugin_data->plugin_name);
        DEBUG_FUNCTION_LINE("Found hooks: %d\n",plugin_data->number_used_hooks);
        for(int j=0;j<plugin_data->number_used_hooks;j++){
            replacement_data_hook_t * hook_data = &plugin_data->hooks[j];
            if(hook_data->type == hook_type){
                DEBUG_FUNCTION_LINE("Calling hook of type %d\n",hook_data->type);
                void * func_ptr = hook_data->func_pointer;
                //TODO: Switch cases depending on arguments etc.
                // Adding arguments!

                if(func_ptr != NULL){
                    if(hook_type == WUPS_LOADER_HOOK_INIT_FUNCTION){
                        DEBUG_FUNCTION_LINE("Calling it! %08X\n",func_ptr);
                        wups_loader_init_args_t args;
                        args.device_mounted = gSDInitDone;
                        args.fs_wrapper.open_repl = (const void*)&open;
                        args.fs_wrapper.close_repl = (const void*)&close;
                        args.fs_wrapper.write_repl = (const void*)&write;
                        args.fs_wrapper.read_repl = (const void*)&read;
                        args.fs_wrapper.lseek_repl = (const void*)&lseek;
                        args.fs_wrapper.stat_repl = (const void*)&stat;
                        args.fs_wrapper.fstat_repl = (const void*)&fstat;
                        args.fs_wrapper.opendir_repl = (const void*)&opendir;
                        args.fs_wrapper.closedir_repl = (const void*)&closedir;
                        args.fs_wrapper.readdir_repl = (const void*)&readdir;

                        ( (void (*)(wups_loader_init_args_t *))((unsigned int*)func_ptr) )(&args);
                    }
                }else{
                    DEBUG_FUNCTION_LINE("Was not defined\n");
                }
            }
        }
    }
}

void DeInit(){
    DeInit_SD_USB();
}

void RestorePatches(){
    for(int plugin_index=gbl_replacement_data.number_used_plugins-1;plugin_index>=0;plugin_index--){
        DEBUG_FUNCTION_LINE("Restoring function for plugin: %d\n",plugin_index);
        new_RestoreInvidualInstructions(&gbl_replacement_data.plugin_data[plugin_index]);
    }
    RestoreInvidualInstructions(method_hooks_hooks, method_hooks_size_hooks);
}

s32 isInMiiMakerHBL(){
    if (OSGetTitleID != 0 && (
            OSGetTitleID() == 0x000500101004A200 || // mii maker eur
            OSGetTitleID() == 0x000500101004A100 || // mii maker usa
            OSGetTitleID() == 0x000500101004A000 ||// mii maker jpn
            OSGetTitleID() == 0x0005000013374842))
        {
            return 1;
    }
    return 0;
}

void Init_SD_USB() {
    int res = IOSUHAX_Open(NULL);
    if(res < 0){
        ExecuteIOSExploitWithDefaultConfig();
    }
    deleteDevTabsNames();
    mount_fake();
    gSDInitDone |= WUPS_SDUSB_MOUNTED_FAKE;

    if(res < 0){
        DEBUG_FUNCTION_LINE("IOSUHAX_open failed\n");
        if((res = mount_sd_fat("sd")) >= 0){
            DEBUG_FUNCTION_LINE("mount_sd_fat success\n");
            gSDInitDone |= WUPS_SDUSB_MOUNTED_OS_SD;
        }else{
            DEBUG_FUNCTION_LINE("mount_sd_fat failed %d\n",res);
        }
    }else{
        DEBUG_FUNCTION_LINE("Using IOSUHAX for SD/USB access\n");
        gSDInitDone |= WUPS_SDUSB_LIBIOSU_LOADED;
        int ntfs_mounts = mountAllNTFS();
        if(ntfs_mounts > 0){
            gSDInitDone |= WUPS_USB_MOUNTED_LIBNTFS;
        }

        if(mount_libfatAll() == 0){
            gSDInitDone |= WUPS_SD_MOUNTED_LIBFAT;
            gSDInitDone |= WUPS_USB_MOUNTED_LIBFAT;
        }
    }
    DEBUG_FUNCTION_LINE("%08X\n",gSDInitDone);
}

void DeInit_SD_USB(){
    DEBUG_FUNCTION_LINE("Called this function.\n");

    if(gSDInitDone & WUPS_SDUSB_MOUNTED_FAKE){
       DEBUG_FUNCTION_LINE("Unmounting fake\n");
       unmount_fake();
       gSDInitDone &= ~WUPS_SDUSB_MOUNTED_FAKE;
    }
    if(gSDInitDone & WUPS_SDUSB_MOUNTED_OS_SD){
        DEBUG_FUNCTION_LINE("Unmounting OS SD\n");
        unmount_sd_fat("sd");
        gSDInitDone &= ~WUPS_SDUSB_MOUNTED_OS_SD;
    }

    if(gSDInitDone & WUPS_SD_MOUNTED_LIBFAT){
        DEBUG_FUNCTION_LINE("Unmounting LIBFAT SD\n");
        unmount_libfat("sd");
        gSDInitDone &= ~WUPS_SD_MOUNTED_LIBFAT;
    }

    if(gSDInitDone & WUPS_USB_MOUNTED_LIBFAT){
        DEBUG_FUNCTION_LINE("Unmounting LIBFAT USB\n");
        unmount_libfat("usb");
        gSDInitDone &= ~WUPS_USB_MOUNTED_LIBFAT;
    }

    if(gSDInitDone & WUPS_USB_MOUNTED_LIBNTFS){
        DEBUG_FUNCTION_LINE("Unmounting LIBNTFS USB\n");
        unmountAllNTFS();
        gSDInitDone &= ~WUPS_USB_MOUNTED_LIBNTFS;
    }

    if(gSDInitDone & WUPS_SDUSB_LIBIOSU_LOADED){
        DEBUG_FUNCTION_LINE("Calling IOSUHAX_Close\n");
        IOSUHAX_Close();
        gSDInitDone &= ~WUPS_SDUSB_LIBIOSU_LOADED;

    }
    deleteDevTabsNames();
    if(gSDInitDone != WUPS_SDUSB_MOUNTED_NONE){
        DEBUG_FUNCTION_LINE("WARNING. Some devices are still mounted.\n");
    }
    DEBUG_FUNCTION_LINE("Function end.\n");
}
