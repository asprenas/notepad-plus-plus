// <GUIConfig name="..."> fields of config.xml (schema of upstream NppParameters::feedGUIParameters /
// createXmlTreeFromGUIParams, v8.9.8.1). X-macro, included by GuiConfig.h/.cpp.
//
//   NPP_BOOL_ATTR(member, "GUIConfig name", "attribute", default)   yes/no attribute
//   NPP_INT_ATTR (member, "GUIConfig name", "attribute", default)   integer attribute
//   NPP_STR_ATTR (member, "GUIConfig name", "attribute", default)   string attribute
//   NPP_BOOL_TEXT(member, "GUIConfig name", default)                element text "yes"/"no"
//   NPP_INT_TEXT (member, "GUIConfig name", default)                element text integer
//   NPP_STR_TEXT (member, "GUIConfig name", default)                element text string
//
// Entries with non-trivial encodings (Auto-detection, ColorPickerCustomColors, auto-insert pairs,
// ScintillaViewsSplitter, StatusBar/MenuBar show|hide, UserDefineDlg, DockingManager) are handled
// in GuiConfig.cpp.

// ToolBar
NPP_BOOL_ATTR(toolbarShow,            "ToolBar", "visible", true)
NPP_INT_ATTR (toolbarFluentColor,     "ToolBar", "fluentColor", 0)
NPP_INT_ATTR (toolbarFluentCustomColor, "ToolBar", "fluentCustomColor", 0)
NPP_BOOL_ATTR(toolbarFluentMono,      "ToolBar", "fluentMono", false)
NPP_STR_TEXT (toolbarIconSet,         "ToolBar", "standard")            // small|large|small2|large2|standard

// TabBar
NPP_BOOL_ATTR(tabDragAndDrop,         "TabBar", "dragAndDrop", true)
NPP_BOOL_ATTR(tabDrawTopBar,          "TabBar", "drawTopBar", true)
NPP_BOOL_ATTR(tabDrawInactiveTab,     "TabBar", "drawInactiveTab", true)
NPP_BOOL_ATTR(tabReduce,              "TabBar", "reduce", true)
NPP_BOOL_ATTR(tabCloseButton,         "TabBar", "closeButton", true)
NPP_BOOL_ATTR(tabPinButton,           "TabBar", "pinButton", true)
NPP_BOOL_ATTR(tabShowOnlyPinnedButton, "TabBar", "showOnlyPinnedButton", false)
NPP_BOOL_ATTR(tabButtonsOnInactiveTabs, "TabBar", "buttonsOninactiveTabs", false)
NPP_BOOL_ATTR(tabDoubleClickToClose,  "TabBar", "doubleClick2Close", false)
NPP_BOOL_ATTR(tabVertical,            "TabBar", "vertical", false)
NPP_BOOL_ATTR(tabMultiLine,           "TabBar", "multiLine", false)
NPP_BOOL_ATTR(tabHide,                "TabBar", "hide", false)
NPP_BOOL_ATTR(tabQuitOnEmpty,         "TabBar", "quitOnEmpty", false)
NPP_INT_ATTR (tabCompactLabelLen,     "TabBar", "tabCompactLabelLen", 0)

// TabSetting
NPP_BOOL_ATTR(tabReplacedBySpace,     "TabSetting", "replaceBySpace", false)
NPP_INT_ATTR (tabSize,                "TabSetting", "size", 4)
NPP_BOOL_ATTR(backspaceUnindent,      "TabSetting", "backspaceUnindent", false)

// AppPosition / FindWindowPosition
NPP_INT_ATTR (appX,                   "AppPosition", "x", 0)
NPP_INT_ATTR (appY,                   "AppPosition", "y", 0)
NPP_INT_ATTR (appWidth,               "AppPosition", "width", 1024)
NPP_INT_ATTR (appHeight,              "AppPosition", "height", 700)
NPP_BOOL_ATTR(appMaximized,           "AppPosition", "isMaximized", false)
NPP_INT_ATTR (findWindowLeft,         "FindWindowPosition", "left", 0)
NPP_INT_ATTR (findWindowTop,          "FindWindowPosition", "top", 0)
NPP_INT_ATTR (findWindowRight,        "FindWindowPosition", "right", 0)
NPP_INT_ATTR (findWindowBottom,       "FindWindowPosition", "bottom", 0)
NPP_BOOL_ATTR(findWindowLessMode,     "FindWindowPosition", "isLessModeOn", false)

// FinderConfig
NPP_BOOL_ATTR(finderWrappedLines,     "FinderConfig", "wrappedLines", false)
NPP_BOOL_ATTR(finderPurgeBeforeEverySearch, "FinderConfig", "purgeBeforeEverySearch", false)
NPP_BOOL_ATTR(finderShowOnlyOneEntryPerFoundLine, "FinderConfig", "showOnlyOneEntryPerFoundLine", true)
NPP_BOOL_ATTR(fifIgnoreUnsavedChanges, "FinderConfig", "FiF_ignoreunsavedChangesInOpenedFiles", false)

// noUpdate (auto updater: kept for config compatibility, not used by the Qt port)
NPP_BOOL_TEXT(noUpdate,               "noUpdate", false)
NPP_INT_ATTR (autoUpdateIntervalDays, "noUpdate", "intervalDays", 15)
NPP_STR_ATTR (autoUpdateNextDate,     "noUpdate", "nextUpdateDate", "")
NPP_INT_ATTR (autoUpdateMode,         "noUpdate", "autoUpdateMode", 1)

// simple text entries
NPP_BOOL_TEXT(checkHistoryFiles,      "CheckHistoryFiles", false)
NPP_INT_TEXT (minimizeToTray,         "TrayIcon", 0)
NPP_INT_TEXT (maintainIndent,         "MaintainIndent", 2)               // 0 none, 1 basic, 2 advanced
NPP_BOOL_TEXT(tagsMatchHighlight,     "TagsMatchHighLight", true)
NPP_BOOL_ATTR(tagAttrHighlight,       "TagsMatchHighLight", "TagAttrHighLight", true)
NPP_BOOL_ATTR(highlightNonHtmlZone,   "TagsMatchHighLight", "HighLightNonHtmlZone", false)
NPP_BOOL_TEXT(rememberLastSession,    "RememberLastSession", true)
NPP_BOOL_TEXT(keepSessionAbsentFileEntries, "KeepSessionAbsentFileEntries", false)
NPP_BOOL_TEXT(detectEncoding,         "DetectEncoding", true)
NPP_BOOL_TEXT(saveAllConfirm,         "SaveAllConfirm", true)
NPP_BOOL_TEXT(doTaskList,             "TaskList", true)
NPP_BOOL_TEXT(styleMru,               "MRU", true)
NPP_INT_TEXT (styleUrl,               "URL", 2)                          // 0 none, 1 fg, 2 underline+fg
NPP_STR_TEXT (uriSchemes,             "uriCustomizedSchemes",
              "svn:// cvs:// git:// imap:// irc:// irc6:// ircs:// ldap:// ldaps:// news: telnet:// gopher:// ssh:// sftp:// smb:// skype: snmp:// spotify: steam:// sms: slack:// chrome:// bitcoin:")
NPP_STR_TEXT (sessionExt,             "sessionExt", "")
NPP_STR_TEXT (workspaceExt,           "workspaceExt", "")

// NewDocDefaultSettings
NPP_INT_ATTR (newDocFormat,           "NewDocDefaultSettings", "format", NPP_DEFAULT_EOL_FORMAT)
NPP_INT_ATTR (newDocEncoding,         "NewDocDefaultSettings", "encoding", 4)   // UniMode::Utf8
NPP_INT_ATTR (newDocLang,             "NewDocDefaultSettings", "lang", 0)       // L_TEXT
NPP_INT_ATTR (newDocCodepage,         "NewDocDefaultSettings", "codepage", -1)
NPP_BOOL_ATTR(newDocOpenAnsiAsUtf8,   "NewDocDefaultSettings", "openAnsiAsUTF8", true)
NPP_BOOL_ATTR(newDocAddOnStartup,     "NewDocDefaultSettings", "addNewDocumentOnStartup", false)
NPP_BOOL_ATTR(newDocUseContentAsTabName, "NewDocDefaultSettings", "useContentAsTabName", false)

NPP_BOOL_ATTR(langMenuCompact,        "langsExcluded", "langMenuCompact", true)

// Print
NPP_BOOL_ATTR(printLineNumber,        "Print", "lineNumber", true)
NPP_BOOL_ATTR(printFormFeedPageBreak, "Print", "formFeedPageBreak", false)
NPP_INT_ATTR (printOption,            "Print", "printOption", 3)               // SC_PRINT_COLOURONWHITE
NPP_STR_ATTR (printHeaderLeft,        "Print", "headerLeft", "")
NPP_STR_ATTR (printHeaderMiddle,      "Print", "headerMiddle", "")
NPP_STR_ATTR (printHeaderRight,       "Print", "headerRight", "")
NPP_STR_ATTR (printFooterLeft,        "Print", "footerLeft", "")
NPP_STR_ATTR (printFooterMiddle,      "Print", "footerMiddle", "")
NPP_STR_ATTR (printFooterRight,       "Print", "footerRight", "")
NPP_STR_ATTR (printHeaderFontName,    "Print", "headerFontName", "")
NPP_STR_ATTR (printFooterFontName,    "Print", "footerFontName", "")
NPP_INT_ATTR (printHeaderFontStyle,   "Print", "headerFontStyle", 0)
NPP_INT_ATTR (printFooterFontStyle,   "Print", "footerFontStyle", 0)
NPP_INT_ATTR (printHeaderFontSize,    "Print", "headerFontSize", 0)
NPP_INT_ATTR (printFooterFontSize,    "Print", "footerFontSize", 0)
NPP_INT_ATTR (printMarginLeft,        "Print", "margeLeft", 0)
NPP_INT_ATTR (printMarginTop,         "Print", "margeTop", 0)
NPP_INT_ATTR (printMarginRight,       "Print", "margeRight", 0)
NPP_INT_ATTR (printMarginBottom,      "Print", "margeBottom", 0)

// Backup
NPP_INT_ATTR (backupAction,           "Backup", "action", 0)                   // 0 none, 1 simple, 2 verbose
NPP_BOOL_ATTR(backupUseCustomDir,     "Backup", "useCustumDir", false)
NPP_STR_ATTR (backupDir,              "Backup", "dir", "")
NPP_BOOL_ATTR(snapshotMode,           "Backup", "isSnapshotMode", true)
NPP_INT_ATTR (snapshotBackupTiming,   "Backup", "snapshotBackupTiming", 7000)

// globalOverride
NPP_BOOL_ATTR(globalOverrideFg,       "globalOverride", "fg", false)
NPP_BOOL_ATTR(globalOverrideBg,       "globalOverride", "bg", false)
NPP_BOOL_ATTR(globalOverrideFont,     "globalOverride", "font", false)
NPP_BOOL_ATTR(globalOverrideFontSize, "globalOverride", "fontSize", false)
NPP_BOOL_ATTR(globalOverrideBold,     "globalOverride", "bold", false)
NPP_BOOL_ATTR(globalOverrideItalic,   "globalOverride", "italic", false)
NPP_BOOL_ATTR(globalOverrideUnderline, "globalOverride", "underline", false)

// auto-completion / auto-insert
NPP_INT_ATTR (autocAction,            "auto-completion", "autoCAction", 3)     // 0 none, 1 func, 2 word, 3 both
NPP_INT_ATTR (autocFromLen,           "auto-completion", "triggerFromNbChar", 1)
NPP_BOOL_ATTR(autocIgnoreNumbers,     "auto-completion", "autoCIgnoreNumbers", true)
NPP_BOOL_ATTR(autocInsertWithEnter,   "auto-completion", "insertSelectedItemUseENTER", true)
NPP_BOOL_ATTR(autocInsertWithTab,     "auto-completion", "insertSelectedItemUseTAB", true)
NPP_BOOL_ATTR(autocBrief,             "auto-completion", "autoCBrief", false)
NPP_BOOL_ATTR(autocFuncParams,        "auto-completion", "funcParams", true)
NPP_BOOL_ATTR(autoInsertParentheses,  "auto-insert", "parentheses", false)
NPP_BOOL_ATTR(autoInsertBrackets,     "auto-insert", "brackets", false)
NPP_BOOL_ATTR(autoInsertCurlyBrackets, "auto-insert", "curlyBrackets", false)
NPP_BOOL_ATTR(autoInsertQuotes,       "auto-insert", "quotes", false)
NPP_BOOL_ATTR(autoInsertDoubleQuotes, "auto-insert", "doubleQuotes", false)
NPP_BOOL_ATTR(autoInsertHtmlXmlTag,   "auto-insert", "htmlXmlTag", false)

// misc single entries
NPP_INT_ATTR (caretWidth,             "Caret", "width", 1)
NPP_INT_ATTR (caretBlinkRate,         "Caret", "blinkRate", 600)
NPP_INT_ATTR (openSaveDir,            "openSaveDir", "value", 0)                // 0 follow current, 1 last used, 2 defaultDirPath
NPP_STR_ATTR (defaultDir,             "openSaveDir", "defaultDirPath", "")
NPP_STR_ATTR (lastUsedDir,            "openSaveDir", "lastUsedDirPath", "")
NPP_BOOL_ATTR(shortTitlebar,          "titleBar", "short", false)
NPP_STR_ATTR (dateTimeFormat,         "insertDateTime", "customizedFormat", "yyyy-MM-dd HH:mm:ss")
NPP_BOOL_ATTR(dateTimeReverseOrder,   "insertDateTime", "reverseDefaultOrder", false)
NPP_BOOL_ATTR(wordCharDefault,        "wordCharList", "useDefault", true)
NPP_STR_ATTR (customWordChars,        "wordCharList", "charsAdded", "")
NPP_INT_ATTR (leftmostDelimiter,      "delimiterSelection", "leftmostDelimiter", 40)   // '('
NPP_INT_ATTR (rightmostDelimiter,     "delimiterSelection", "rightmostDelimiter", 41)  // ')'
NPP_BOOL_ATTR(delimiterOnEntireDocument, "delimiterSelection", "delimiterSelectionOnEntireDocument", false)

// largeFileRestriction
NPP_INT_ATTR (largeFileSizeMB,        "largeFileRestriction", "fileSizeMB", 200)
NPP_BOOL_ATTR(largeFileEnabled,       "largeFileRestriction", "isEnabled", true)
NPP_BOOL_ATTR(largeFileAllowAutoCompletion, "largeFileRestriction", "allowAutoCompletion", false)
NPP_BOOL_ATTR(largeFileAllowBraceMatch, "largeFileRestriction", "allowBraceMatch", false)
NPP_BOOL_ATTR(largeFileAllowSmartHilite, "largeFileRestriction", "allowSmartHilite", false)
NPP_BOOL_ATTR(largeFileAllowClickableLink, "largeFileRestriction", "allowClickableLink", false)
NPP_BOOL_ATTR(largeFileDeactivateWordWrap, "largeFileRestriction", "deactivateWordWrap", true)
NPP_BOOL_ATTR(largeFileSuppress2GBWarning, "largeFileRestriction", "suppress2GBWarning", false)

// multiInst
NPP_INT_ATTR (multiInstSetting,       "multiInst", "setting", 0)               // 0 mono, 1 multi on session, 2 multi
NPP_BOOL_ATTR(keepClipboardHistoryPanel, "multiInst", "clipboardHistory", false)
NPP_BOOL_ATTR(keepDocumentListPanel,  "multiInst", "documentList", false)
NPP_BOOL_ATTR(keepCharacterPanel,     "multiInst", "characterPanel", false)
NPP_BOOL_ATTR(keepFolderAsWorkspacePanel, "multiInst", "folderAsWorkspace", false)
NPP_BOOL_ATTR(keepProjectPanels,      "multiInst", "projectPanels", false)
NPP_BOOL_ATTR(keepDocumentMapPanel,   "multiInst", "documentMap", false)
NPP_BOOL_ATTR(keepFunctionListPanel,  "multiInst", "fuctionList", false)        // sic (upstream spelling)
NPP_BOOL_ATTR(keepPluginPanels,       "multiInst", "pluginPanels", false)

// MISC
NPP_BOOL_ATTR(fileSwitcherWithoutExtColumn, "MISC", "fileSwitcherWithoutExtColumn", false)
NPP_INT_ATTR (fileSwitcherExtWidth,   "MISC", "fileSwitcherExtWidth", 50)
NPP_BOOL_ATTR(fileSwitcherWithoutPathColumn, "MISC", "fileSwitcherWithoutPathColumn", true)
NPP_INT_ATTR (fileSwitcherPathWidth,  "MISC", "fileSwitcherPathWidth", 50)
NPP_BOOL_ATTR(fileSwitcherNoGroups,   "MISC", "fileSwitcherNoGroups", false)
NPP_BOOL_ATTR(backslashIsEscapeForSql, "MISC", "backSlashIsEscapeCharacterForSql", true)
NPP_INT_ATTR (writeTechnologyEngine,  "MISC", "writeTechnologyEngine", 1)
NPP_BOOL_ATTR(folderDroppedOpenFiles, "MISC", "isFolderDroppedOpenFiles", false)
NPP_BOOL_ATTR(docPeekOnTab,           "MISC", "docPeekOnTab", false)
NPP_BOOL_ATTR(docPeekOnMap,           "MISC", "docPeekOnMap", false)
NPP_BOOL_ATTR(sortFunctionList,       "MISC", "sortFunctionList", false)
NPP_BOOL_ATTR(saveDlgExtFilterToAllTypes, "MISC", "saveDlgExtFilterToAllTypes", false)
NPP_BOOL_ATTR(muteSounds,             "MISC", "muteSounds", false)
NPP_BOOL_ATTR(enableFoldCmdToggable,  "MISC", "enableFoldCmdToggable", false)
NPP_BOOL_ATTR(hideMenuRightShortcuts, "MISC", "hideMenuRightShortcuts", false)
NPP_BOOL_ATTR(fawSymlinkAllowed,      "MISC", "isFawSymlinkAllowed", false)

// Searching
NPP_BOOL_ATTR(monospacedFontFindDlg,  "Searching", "monospacedFontFindDlg", false)
NPP_BOOL_ATTR(fillFindFieldWithSelected, "Searching", "fillFindFieldWithSelected", true)
NPP_BOOL_ATTR(fillFindFieldSelectCaret, "Searching", "fillFindFieldSelectCaret", true)
NPP_BOOL_ATTR(findDlgAlwaysVisible,   "Searching", "findDlgAlwaysVisible", false)
NPP_BOOL_ATTR(confirmReplaceInAllOpenDocs, "Searching", "confirmReplaceInAllOpenDocs", true)
NPP_BOOL_ATTR(replaceStopsWithoutFindingNext, "Searching", "replaceStopsWithoutFindingNext", false)
NPP_INT_ATTR (inSelectionAutocheckThreshold, "Searching", "inSelectionAutocheckThreshold", 1024)
NPP_INT_ATTR (fillFindWhatThreshold,  "Searching", "fillFindWhatThreshold", 1024)
NPP_BOOL_ATTR(fillDirFieldFromActiveDoc, "Searching", "fillDirFieldFromActiveDoc", false)

NPP_INT_ATTR (searchEngineChoice,     "searchEngine", "searchEngineChoice", 2)  // google
NPP_STR_ATTR (searchEngineCustom,     "searchEngine", "searchEngineCustom", "")
NPP_STR_ATTR (shortcutsXmlHmac,       "shortcutsXmlHMAC", "value", "")

// MarkAll / SmartHighLight
NPP_BOOL_ATTR(markAllMatchCase,       "MarkAll", "matchCase", false)
NPP_BOOL_ATTR(markAllWholeWord,       "MarkAll", "wholeWordOnly", true)
NPP_BOOL_TEXT(smartHighlight,         "SmartHighLight", true)
NPP_BOOL_ATTR(smartHighlightMatchCase, "SmartHighLight", "matchCase", false)
NPP_BOOL_ATTR(smartHighlightWholeWord, "SmartHighLight", "wholeWordOnly", true)
NPP_BOOL_ATTR(smartHighlightUseFindSettings, "SmartHighLight", "useFindSettings", false)
NPP_BOOL_ATTR(smartHighlightOnAnotherView, "SmartHighLight", "onAnotherView", false)

// DarkMode (colors are Win32 COLORREF ints 0x00BBGGRR, kept as stored)
NPP_BOOL_ATTR(darkModeEnabled,        "DarkMode", "enable", false)
NPP_INT_ATTR (darkModeColorTone,      "DarkMode", "colorTone", 0)
NPP_INT_ATTR (darkCustomColorTop,     "DarkMode", "customColorTop", 0x202020)
NPP_INT_ATTR (darkCustomColorMenuHotTrack, "DarkMode", "customColorMenuHotTrack", 0x454545)
NPP_INT_ATTR (darkCustomColorActive,  "DarkMode", "customColorActive", 0x383838)
NPP_INT_ATTR (darkCustomColorMain,    "DarkMode", "customColorMain", 0x202020)
NPP_INT_ATTR (darkCustomColorError,   "DarkMode", "customColorError", 0x0000B0)
NPP_INT_ATTR (darkCustomColorText,    "DarkMode", "customColorText", 0xE0E0E0)
NPP_INT_ATTR (darkCustomColorDarkText, "DarkMode", "customColorDarkText", 0xC0C0C0)
NPP_INT_ATTR (darkCustomColorDisabledText, "DarkMode", "customColorDisabledText", 0x808080)
NPP_INT_ATTR (darkCustomColorLinkText, "DarkMode", "customColorLinkText", 0x00FFFF)
NPP_INT_ATTR (darkCustomColorEdge,    "DarkMode", "customColorEdge", 0x646464)
NPP_INT_ATTR (darkCustomColorHotEdge, "DarkMode", "customColorHotEdge", 0x9B9B9B)
NPP_INT_ATTR (darkCustomColorDisabledEdge, "DarkMode", "customColorDisabledEdge", 0x484848)
NPP_BOOL_ATTR(darkFollowSystem,       "DarkMode", "enableWindowsMode", false)
NPP_STR_ATTR (darkThemeName,          "DarkMode", "darkThemeName", "DarkModeDefault.xml")
NPP_INT_ATTR (darkToolBarIconSet,     "DarkMode", "darkToolBarIconSet", 0)
NPP_INT_ATTR (darkTbFluentColor,      "DarkMode", "darkTbFluentColor", 0)
NPP_INT_ATTR (darkTbFluentCustomColor, "DarkMode", "darkTbFluentCustomColor", 0)
NPP_BOOL_ATTR(darkTbFluentMono,       "DarkMode", "darkTbFluentMono", false)
NPP_INT_ATTR (darkTabIconSet,         "DarkMode", "darkTabIconSet", 2)
NPP_BOOL_ATTR(darkTabUseTheme,        "DarkMode", "darkTabUseTheme", false)
NPP_STR_ATTR (lightThemeName,         "DarkMode", "lightThemeName", "")
NPP_INT_ATTR (lightToolBarIconSet,    "DarkMode", "lightToolBarIconSet", 4)
NPP_INT_ATTR (lightTbFluentColor,     "DarkMode", "lightTbFluentColor", 0)
NPP_INT_ATTR (lightTbFluentCustomColor, "DarkMode", "lightTbFluentCustomColor", 0)
NPP_BOOL_ATTR(lightTbFluentMono,      "DarkMode", "lightTbFluentMono", false)
NPP_INT_ATTR (lightTabIconSet,        "DarkMode", "lightTabIconSet", 0)
NPP_BOOL_ATTR(lightTabUseTheme,       "DarkMode", "lightTabUseTheme", true)

// ScintillaPrimaryView: editor view settings (upstream ScintillaViewParams / writeScintillaParams)
//   NPP_SHOWHIDE_ATTR(member, node, attribute, default)   "show"/"hide" attribute
NPP_SHOWHIDE_ATTR(lineNumberMargin,   "ScintillaPrimaryView", "lineNumberMargin", true)
NPP_BOOL_ATTR(lineNumberDynamicWidth, "ScintillaPrimaryView", "lineNumberDynamicWidth", true)
NPP_SHOWHIDE_ATTR(bookmarkMargin,     "ScintillaPrimaryView", "bookMarkMargin", true)
NPP_SHOWHIDE_ATTR(indentGuideLine,    "ScintillaPrimaryView", "indentGuideLine", true)
NPP_STR_ATTR (folderMarkStyle,        "ScintillaPrimaryView", "folderMarkStyle", "box")   // simple|arrow|circle|box|none
NPP_INT_ATTR (changeHistory,          "ScintillaPrimaryView", "isChangeHistoryEnabled", 1) // 0 off, 1 margin, 2 indicator, 3 both
NPP_STR_ATTR (lineWrapMethod,         "ScintillaPrimaryView", "lineWrapMethod", "aligned") // default|aligned|indent
NPP_INT_ATTR (currentLineIndicator,   "ScintillaPrimaryView", "currentLineIndicator", 1)   // 0 none, 1 hilite, 2 frame
NPP_INT_ATTR (currentLineFrameWidth,  "ScintillaPrimaryView", "currentLineFrameWidth", 1)
NPP_BOOL_ATTR(virtualSpace,           "ScintillaPrimaryView", "virtualSpace", false)
NPP_BOOL_ATTR(scrollBeyondLastLine,   "ScintillaPrimaryView", "scrollBeyondLastLine", true)
NPP_BOOL_ATTR(rightClickKeepsSelection, "ScintillaPrimaryView", "rightClickKeepsSelection", false)
NPP_BOOL_ATTR(selectedTextFgSingleColor, "ScintillaPrimaryView", "selectedTextForegroundSingleColor", false)
NPP_BOOL_ATTR(disableAdvancedScrolling, "ScintillaPrimaryView", "disableAdvancedScrolling", false)
NPP_SHOWHIDE_ATTR(wrapSymbolShow,     "ScintillaPrimaryView", "wrapSymbolShow", false)
NPP_BOOL_ATTR(wrap,                   "ScintillaPrimaryView", "Wrap", false)
NPP_BOOL_ATTR(borderEdge,             "ScintillaPrimaryView", "borderEdge", true)
NPP_BOOL_ATTR(edgeBgMode,             "ScintillaPrimaryView", "isEdgeBgMode", false)
NPP_STR_ATTR (edgeMultiColumnPos,     "ScintillaPrimaryView", "edgeMultiColumnPos", "")    // space separated columns
NPP_INT_ATTR (zoom,                   "ScintillaPrimaryView", "zoom", 0)
NPP_INT_ATTR (zoom2,                  "ScintillaPrimaryView", "zoom2", 0)
NPP_BOOL_ATTR(zoomSync,               "ScintillaPrimaryView", "zoomSync", false)
NPP_SHOWHIDE_ATTR(whiteSpaceShow,     "ScintillaPrimaryView", "whiteSpaceShow", false)
NPP_SHOWHIDE_ATTR(eolShow,            "ScintillaPrimaryView", "eolShow", false)
NPP_INT_ATTR (eolMode,                "ScintillaPrimaryView", "eolMode", 1)                // roundedRectangleText
NPP_SHOWHIDE_ATTR(npcShow,            "ScintillaPrimaryView", "npcShow", false)
NPP_INT_ATTR (npcMode,                "ScintillaPrimaryView", "npcMode", 1)                // abbreviation
NPP_BOOL_ATTR(npcCustomColor,         "ScintillaPrimaryView", "npcCustomColor", false)
NPP_BOOL_ATTR(npcIncludeCcUniEol,     "ScintillaPrimaryView", "npcIncludeCcUniEOL", false)
NPP_BOOL_ATTR(npcNoInputC0,           "ScintillaPrimaryView", "npcNoInputC0", true)
NPP_BOOL_ATTR(ccUniEolShow,           "ScintillaPrimaryView", "ccShow", true)
NPP_INT_ATTR (borderWidth,            "ScintillaPrimaryView", "borderWidth", 2)
NPP_BOOL_ATTR(smoothFont,             "ScintillaPrimaryView", "smoothFont", false)
NPP_INT_ATTR (paddingLeft,            "ScintillaPrimaryView", "paddingLeft", 0)
NPP_INT_ATTR (paddingRight,           "ScintillaPrimaryView", "paddingRight", 0)
NPP_INT_ATTR (distractionFreeDivPart, "ScintillaPrimaryView", "distractionFreeDivPart", 4)
NPP_BOOL_ATTR(lineCopyCutWithoutSelection, "ScintillaPrimaryView", "lineCopyCutWithoutSelection", true)
NPP_BOOL_ATTR(disableSelectedTextDragDrop, "ScintillaPrimaryView", "disableSelectedTextDragDrop", false)
NPP_BOOL_ATTR(multiSelection,         "ScintillaPrimaryView", "multiSelection", true)
NPP_BOOL_ATTR(columnSelToMultiEdit,   "ScintillaPrimaryView", "columnSel2MultiEdit", true)
