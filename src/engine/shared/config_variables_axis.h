// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

// Flags
MACRO_CONFIG_INT(ClAClientSettingsTabs, ec_aclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags to disable settings tabs")