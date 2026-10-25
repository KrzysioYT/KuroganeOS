#ifndef KUROGANE_FLUX_APP_CATALOG_H
#define KUROGANE_FLUX_APP_CATALOG_H

// Canonical application window identity and installed image paths.
// Keep titles synchronized across WindowManager, Home and the Ring-3
// application. IDs remain defined by <kurogane/desktop.h>; no new ABI.
// This table deliberately contains only metadata, not authorization.
// Session ownership MUST continue to use process ancestry/credentials.
#define KU_FLUX_TITLE_HOME          "RED FLUX HOME"
#define KU_FLUX_TITLE_TERMINAL      "RED FLUX TERMINAL"
#define KU_FLUX_TITLE_FILES         "FILES"
#define KU_FLUX_TITLE_PERFORMANCE   "FORGE CONTROL"
#define KU_FLUX_TITLE_BROWSER       "KUROGANE WEB"
#define KU_FLUX_TITLE_MONITOR       "SYSTEM MONITOR"
#define KU_FLUX_TITLE_SETTINGS      "SETTINGS"
#define KU_FLUX_TITLE_ABOUT         "ABOUT KUROGANEOS"
#define KU_FLUX_TITLE_NOTIFICATIONS "NOTIFICATIONS"
#define KU_FLUX_TITLE_LOGIN         "KUROGANE LOGIN"

#define KU_FLUX_EXEC_HOME          "/gui/launcher"
#define KU_FLUX_EXEC_TERMINAL      "/gui/terminal"
#define KU_FLUX_EXEC_FILES         "/gui/files"
#define KU_FLUX_EXEC_PERFORMANCE   "/gui/perf"
#define KU_FLUX_EXEC_BROWSER       "/gui/browser"
#define KU_FLUX_EXEC_MONITOR       "/gui/sysmon"
#define KU_FLUX_EXEC_SETTINGS      "/gui/settings"
#define KU_FLUX_EXEC_ABOUT         "/gui/about"
#define KU_FLUX_EXEC_NOTIFICATIONS "/gui/notify"

#endif
