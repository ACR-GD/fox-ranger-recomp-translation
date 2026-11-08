#include "native_dialog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

int native_dialog_browse_folder(const char *title, char *out_path, size_t max_len)
{
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanCreateDirectories:NO];
        if (title) {
            [panel setMessage:[NSString stringWithUTF8String:title]];
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }
        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] firstObject];
            if (url) {
                const char *path = [[url path] UTF8String];
                if (path && strlen(path) < max_len) {
                    strncpy(out_path, path, max_len - 1);
                    out_path[max_len - 1] = '\0';
                    return 0;
                }
            }
        }
    }
    return -1;
}

int native_dialog_browse_file(const char *title, char *out_path, size_t max_len)
{
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if (title) {
            [panel setMessage:[NSString stringWithUTF8String:title]];
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }
        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] firstObject];
            if (url) {
                const char *path = [[url path] UTF8String];
                if (path && strlen(path) < max_len) {
                    strncpy(out_path, path, max_len - 1);
                    out_path[max_len - 1] = '\0';
                    return 0;
                }
            }
        }
    }
    return -1;
}

#elif defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

int native_dialog_browse_folder(const char *title, char *out_path, size_t max_len)
{
    (void)max_len;
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = title ? title : "Select Fox Ranger DOS Game Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl && SHGetPathFromIDListA(pidl, out_path)) {
        CoTaskMemFree(pidl);
        return 0;
    }
    return -1;
}

int native_dialog_browse_file(const char *title, char *out_path, size_t max_len)
{
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = out_path;
    ofn.nMaxFile = (DWORD)max_len;
    ofn.lpstrFilter = "DOS Executable (*.exe)\0*.exe;*.EXE\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = title ? title : "Select FOX.EXE";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) return 0;
    return -1;
}

#else
/* Linux / Unix fallback using zenity or kdialog */
int native_dialog_browse_folder(const char *title, char *out_path, size_t max_len)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "zenity --file-selection --directory --title=\"%s\" 2>/dev/null",
             title ? title : "Select Fox Ranger DOS Game Folder");
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    if (fgets(out_path, (int)max_len, f)) {
        size_t l = strlen(out_path);
        while (l > 0 && (out_path[l - 1] == '\n' || out_path[l - 1] == '\r')) out_path[--l] = '\0';
        pclose(f);
        return (l > 0) ? 0 : -1;
    }
    pclose(f);
    return -1;
}

int native_dialog_browse_file(const char *title, char *out_path, size_t max_len)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "zenity --file-selection --file-filter=\"*.exe *.EXE\" --title=\"%s\" 2>/dev/null",
             title ? title : "Select FOX.EXE");
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    if (fgets(out_path, (int)max_len, f)) {
        size_t l = strlen(out_path);
        while (l > 0 && (out_path[l - 1] == '\n' || out_path[l - 1] == '\r')) out_path[--l] = '\0';
        pclose(f);
        return (l > 0) ? 0 : -1;
    }
    pclose(f);
    return -1;
}
#endif
