#include <windows.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <wchar.h>
#include "lang_overlay.h"

#define PATH_CAP 1024

static wchar_t game_root[PATH_CAP];     /* the folder the game runs from */
static size_t game_root_len;
static wchar_t overlay_root[PATH_CAP];  /* game_root\lang\<language> */
static size_t overlay_root_len;

/* Lowercase paths relative to overlay_root, sorted for bsearch. */
static wchar_t **overlay_files;
static size_t overlay_count;
static size_t overlay_capacity;

static HMODULE self_module;

static int compare_paths(const void *a, const void *b)
{
    return wcscmp(*(wchar_t *const *)a, *(wchar_t *const *)b);
}

static void add_overlay_file(const wchar_t *relative)
{
    size_t size = (wcslen(relative) + 1) * sizeof(wchar_t);
    wchar_t *copy;

    if (overlay_count == overlay_capacity) {
        size_t capacity = overlay_capacity ? overlay_capacity * 2 : 256;
        wchar_t **grown = realloc(overlay_files, capacity * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        overlay_files = grown;
        overlay_capacity = capacity;
    }
    copy = malloc(size);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, relative, size);
    CharLowerW(copy);
    overlay_files[overlay_count++] = copy;
}

/* path holds a folder of length path_len; its buffer is reused for children. */
static void index_folder(wchar_t *path, size_t path_len)
{
    WIN32_FIND_DATAW entry;
    HANDLE find;

    if (path_len + 3 > PATH_CAP) {
        return;
    }
    wcscpy(path + path_len, L"\\*");
    find = FindFirstFileW(path, &entry);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        size_t name_len = wcslen(entry.cFileName);

        if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0 ||
            path_len + 1 + name_len >= PATH_CAP) {
            continue;
        }
        path[path_len] = L'\\';
        wcscpy(path + path_len + 1, entry.cFileName);
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            index_folder(path, path_len + 1 + name_len);
        } else {
            add_overlay_file(path + overlay_root_len + 1);
        }
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

static int is_under_game_root(const wchar_t *full)
{
    return _wcsnicmp(full, game_root, game_root_len) == 0 && full[game_root_len] == L'\\';
}

static int resolve_w(const wchar_t *path, wchar_t *out, size_t capacity)
{
    wchar_t full[PATH_CAP];
    wchar_t key[PATH_CAP];
    const wchar_t *relative;
    const wchar_t *key_ptr = key;
    DWORD length;

    if (overlay_count == 0 || path == NULL) {
        return 0;
    }
    length = GetFullPathNameW(path, PATH_CAP, full, NULL);
    if (length == 0 || length >= PATH_CAP || !is_under_game_root(full)) {
        return 0;
    }
    relative = full + game_root_len + 1;
    wcscpy(key, relative);
    CharLowerW(key);
    if (bsearch(&key_ptr, overlay_files, overlay_count, sizeof(*overlay_files), compare_paths) == NULL) {
        return 0;
    }
    if (overlay_root_len + 1 + wcslen(relative) + 1 > capacity) {
        return 0;
    }
    wcscpy(out, overlay_root);
    out[overlay_root_len] = L'\\';
    wcscpy(out + overlay_root_len + 1, relative);
    return 1;
}

static int resolve_a_to_w(const char *path, wchar_t *out, size_t capacity)
{
    wchar_t wide[PATH_CAP];

    if (path == NULL || !MultiByteToWideChar(CP_ACP, 0, path, -1, wide, PATH_CAP)) {
        return 0;
    }
    return resolve_w(wide, out, capacity);
}

int lang_overlay_resolve_a(const char *path, char *out, size_t capacity)
{
    wchar_t mapped[PATH_CAP];
    BOOL lossy = FALSE;

    if (!resolve_a_to_w(path, mapped, PATH_CAP)) {
        return 0;
    }
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, mapped, -1, out, (int)capacity,
                               NULL, &lossy) && !lossy;
}

/* Profile calls without a folder mean the Windows folder, not the game's. */
static int resolve_profile_a(const char *path, char *out, size_t capacity)
{
    return path != NULL && (strchr(path, '\\') || strchr(path, '/')) &&
           lang_overlay_resolve_a(path, out, capacity);
}

static int opens_for_reading(DWORD access, DWORD disposition)
{
    const DWORD writes = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE;
    return disposition == OPEN_EXISTING && (access & writes) == 0;
}

static HANDLE WINAPI hook_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                      DWORD disposition, DWORD flags, HANDLE template_file)
{
    wchar_t mapped[PATH_CAP];

    if (opens_for_reading(access, disposition) && resolve_a_to_w(name, mapped, PATH_CAP)) {
        return CreateFileW(mapped, access, share, security, disposition, flags, template_file);
    }
    return CreateFileA(name, access, share, security, disposition, flags, template_file);
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                      DWORD disposition, DWORD flags, HANDLE template_file)
{
    wchar_t mapped[PATH_CAP];

    if (opens_for_reading(access, disposition) && resolve_w(name, mapped, PATH_CAP)) {
        name = mapped;
    }
    return CreateFileW(name, access, share, security, disposition, flags, template_file);
}

static HANDLE WINAPI hook_FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA found)
{
    char mapped[PATH_CAP];

    if (name != NULL && strpbrk(name, "*?") == NULL && lang_overlay_resolve_a(name, mapped, PATH_CAP)) {
        name = mapped;
    }
    return FindFirstFileA(name, found);
}

static DWORD WINAPI hook_GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback,
                                                  LPSTR out, DWORD size, LPCSTR file)
{
    char mapped[PATH_CAP];

    if (resolve_profile_a(file, mapped, PATH_CAP)) {
        file = mapped;
    }
    return GetPrivateProfileStringA(section, key, fallback, out, size, file);
}

static UINT WINAPI hook_GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT fallback, LPCSTR file)
{
    char mapped[PATH_CAP];

    if (resolve_profile_a(file, mapped, PATH_CAP)) {
        file = mapped;
    }
    return GetPrivateProfileIntA(section, key, fallback, file);
}

static DWORD WINAPI hook_GetPrivateProfileSectionA(LPCSTR section, LPSTR out, DWORD size, LPCSTR file)
{
    char mapped[PATH_CAP];

    if (resolve_profile_a(file, mapped, PATH_CAP)) {
        file = mapped;
    }
    return GetPrivateProfileSectionA(section, out, size, file);
}

static void patch_game_modules(void);

#define DATA_ONLY_FLAGS (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)

/* Rescans only when the call mapped a new module. */
static HMODULE after_load(HMODULE before, HMODULE module, DWORD flags)
{
    if (module != NULL && module != before && (flags & DATA_ONLY_FLAGS) == 0) {
        patch_game_modules();
    }
    return module;
}

static HMODULE WINAPI hook_LoadLibraryA(LPCSTR name)
{
    HMODULE before = GetModuleHandleA(name);
    return after_load(before, LoadLibraryA(name), 0);
}

static HMODULE WINAPI hook_LoadLibraryW(LPCWSTR name)
{
    HMODULE before = GetModuleHandleW(name);
    return after_load(before, LoadLibraryW(name), 0);
}

static HMODULE WINAPI hook_LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags)
{
    HMODULE before = GetModuleHandleA(name);
    return after_load(before, LoadLibraryExA(name, file, flags), flags);
}

static HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    HMODULE before = GetModuleHandleW(name);
    return after_load(before, LoadLibraryExW(name, file, flags), flags);
}

struct import_hook {
    const char *name;
    void *hook;
    DWORD original;
};

static struct import_hook import_hooks[] = {
    { "CreateFileA", hook_CreateFileA },
    { "CreateFileW", hook_CreateFileW },
    { "FindFirstFileA", hook_FindFirstFileA },
    { "GetPrivateProfileStringA", hook_GetPrivateProfileStringA },
    { "GetPrivateProfileIntA", hook_GetPrivateProfileIntA },
    { "GetPrivateProfileSectionA", hook_GetPrivateProfileSectionA },
    { "LoadLibraryA", hook_LoadLibraryA },
    { "LoadLibraryW", hook_LoadLibraryW },
    { "LoadLibraryExA", hook_LoadLibraryExA },
    { "LoadLibraryExW", hook_LoadLibraryExW },
};

#define IMPORT_HOOK_COUNT (sizeof(import_hooks) / sizeof(import_hooks[0]))

static void write_slot(DWORD *slot, DWORD value)
{
    DWORD old_protect;

    if (VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &old_protect)) {
        *slot = value;
        VirtualProtect(slot, sizeof(*slot), old_protect, &old_protect);
    }
}

/* Matches import slots by address, so any import name that resolves to
   these functions (kernel32 or an api-ms-win set) is covered. */
static void patch_module(unsigned char *base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_DATA_DIRECTORY imports;
    IMAGE_IMPORT_DESCRIPTOR *descriptor;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0) {
        return;
    }
    for (descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(base + imports.VirtualAddress); descriptor->Name; descriptor++) {
        DWORD *slot = (DWORD *)(base + descriptor->FirstThunk);

        for (; *slot; slot++) {
            size_t i;
            for (i = 0; i < IMPORT_HOOK_COUNT; i++) {
                if (import_hooks[i].original && *slot == import_hooks[i].original) {
                    write_slot(slot, (DWORD)import_hooks[i].hook);
                }
            }
        }
    }
}

static wchar_t system_folders[2][MAX_PATH];

static int is_system_module(const wchar_t *path)
{
    size_t i;

    for (i = 0; i < 2; i++) {
        size_t length = wcslen(system_folders[i]);
        if (length && _wcsnicmp(path, system_folders[i], length) == 0 && path[length] == L'\\') {
            return 1;
        }
    }
    return 0;
}

/* System32/SysWOW64 DLLs keep their imports; the game's DLLs and the
   VC runtime (which may load from WinSxS) are patched. */
static void patch_game_modules(void)
{
    MODULEENTRY32W module;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (module.hModule != self_module && !is_system_module(module.szExePath)) {
                patch_module(module.modBaseAddr);
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
}

int lang_overlay_install(const char *language)
{
    wchar_t walk[PATH_CAP];
    wchar_t language_w[64];
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    DWORD length;
    size_t i;

    if (overlay_count || language == NULL || language[0] == '\0' ||
        strpbrk(language, "\\/:.") != NULL ||
        !MultiByteToWideChar(CP_ACP, 0, language, -1, language_w, 64)) {
        return 0;
    }
    length = GetCurrentDirectoryW(PATH_CAP, game_root);
    if (length == 0 || length >= PATH_CAP - 64) {
        return 0;
    }
    if (game_root[length - 1] == L'\\') {
        game_root[--length] = L'\0';
    }
    game_root_len = length;

    _snwprintf(overlay_root, PATH_CAP, L"%ls\\lang\\%ls", game_root, language_w);
    overlay_root[PATH_CAP - 1] = L'\0';
    overlay_root_len = wcslen(overlay_root);

    wcscpy(walk, overlay_root);
    index_folder(walk, overlay_root_len);
    if (overlay_count == 0) {
        return 0;
    }
    qsort(overlay_files, overlay_count, sizeof(*overlay_files), compare_paths);

    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)lang_overlay_install, &self_module);
    GetSystemDirectoryW(system_folders[0], MAX_PATH);
    GetSystemWow64DirectoryW(system_folders[1], MAX_PATH);
    for (i = 0; i < IMPORT_HOOK_COUNT; i++) {
        import_hooks[i].original = (DWORD)GetProcAddress(kernel32, import_hooks[i].name);
    }
    patch_game_modules();
    return (int)overlay_count;
}
