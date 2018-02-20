#include <wups.h>
#include <string.h>
#include <dynamic_libs/os_functions.h>
#include <dynamic_libs/vpad_functions.h>
#include <dynamic_libs/padscore_functions.h>
#include <dynamic_libs/socket_functions.h>
#include <dynamic_libs/proc_ui_functions.h>
#include <controller_patcher/ControllerPatcher.hpp>
#include <utils/logger.h>
#include <fs/sd_fat_devoptab.h>

WUPS_PLUGIN_NAME("HID to VPAD lite");
WUPS_PLUGIN_DESCRIPTION("Enables HID devices as controllers on your Wii U");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Maschell");
WUPS_PLUGIN_LICENSE("GPL");

#define SD_PATH                     "sd:"
#define WIIU_PATH "/wiiu"
#define DEFAULT_HID_TO_VPAD_PATH SD_PATH WIIU_PATH "/apps/hidtovpad"

INITIALIZE(args){
    InitOSFunctionPointers();
    InitSocketFunctionPointers();
    InitVPadFunctionPointers();
    InitProcUIFunctionPointers();
    InitPadScoreFunctionPointers();

    log_init();
    log_print("Init of hid_to_vpad!\n");    
    if(args != NULL && (args->device_mounted & WUPS_SD_MOUNTED) > 0){
        DEBUG_FUNCTION_LINE("Loading with SD Access\n");
        ControllerPatcher::Init(CONTROLLER_PATCHER_PATH);
    } else {
        DEBUG_FUNCTION_LINE("Loading without SD Access\n");
        ControllerPatcher::Init(NULL);
    }
    
    ControllerPatcher::disableControllerMapping();
    
    log_print("Start network server\n");
    ControllerPatcher::startNetworkServer();
    //TODO: ControllerPatcher::DeInit() on restore.
} 


DECL_FUNCTION(void, __PPCExit, void){    
    ControllerPatcher::resetCallbackData();

    ControllerPatcher::destroyConfigHelper();
    ControllerPatcher::stopNetworkServer();
    real___PPCExit();
}

DECL_FUNCTION(s32, VPADRead, s32 chan, VPADData *buffer, u32 buffer_size, s32 *error) {
    s32 result = real_VPADRead(chan, buffer, buffer_size, error);
    //A keyboard only sends data when the state changes. We force it to call the sampling callback on each frame!
    ControllerPatcher::sampleKeyboardData();
	
    bool do_callback = (result > 0 && (buffer[0].btns_h & VPAD_BUTTON_TV));
    ControllerPatcher::handleCallbackData(do_callback);

    if(ControllerPatcher::areControllersConnected() && buffer_size > 0){
        ControllerPatcher::setRumble(UController_Type_Gamepad,!!VPADBASEGetMotorOnRemainingCount(0));

        if(ControllerPatcher::setControllerDataFromHID(buffer) == CONTROLLER_PATCHER_ERROR_NONE){

            if(buffer[0].btns_h & VPAD_BUTTON_HOME){
                //You can open the home menu this way, but not close it. Need a proper way to close it using the same button...
                //OSSendAppSwitchRequest(5,0,0); //Open the home menu!
            }

            if(error != NULL){
                *error = 0;
            }
            result = 1; // We want the WiiU to ignore everything else.
        }
    }

    if(ControllerPatcher::isButtonRemappingDone()){
        ControllerPatcher::buttonRemapping(buffer,result);
        //ControllerPatcher::printVPADButtons(buffer); //Leads to random crashes on app transitions.
    }

    return result;
}

WUPS_MUST_REPLACE(VPADRead,                            WUPS_LOADER_LIBRARY_VPAD,      VPADRead);
WUPS_MUST_REPLACE(__PPCExit,                           WUPS_LOADER_LIBRARY_COREINIT,  __PPCExit);
