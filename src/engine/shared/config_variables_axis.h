// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

// Flags
MACRO_CONFIG_INT(ClAClientSettingsTabs, ac_aclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags to disable settings tabs")
MACRO_CONFIG_INT(ClAClientShopAutoSet, ac_aclient_shop_auto_set, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply downloaded shop assets automatically")
MACRO_CONFIG_INT(AcNameplateSkins, ac_nameplate_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows skin names in nameplates, good for finding missing skins")