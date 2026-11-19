#include <windows.h>
#include <string.h>
#include "text_direction.h"
#include "lang_overlay.h"

#define FONT_INFO_PATH "./juidata/font/FontInfo.ini"

#define EXE_IMAGE_BASE 0x00400000u
#define MAX_PATCH_SIZE 8

struct code_patch {
    DWORD address;
    unsigned char size;
    unsigned char expected[MAX_PATCH_SIZE];
    unsigned char replacement[MAX_PATCH_SIZE];
    void (*call)(void);
    unsigned char push_frame;
};

#define FLIPPED_RIGHT 0x40000000u

static int text_inset = 8;

/* Left branch of FUN_00bdd9f0: lines that were right-aligned get a margin. */
static void __stdcall align_left(unsigned char *frame)
{
    unsigned int flags = *(unsigned int *)(frame + 0x14);
    const int *rect = *(const int **)(frame + 0x10);

    *(int *)(frame - 0x10) = rect[0] + ((flags & FLIPPED_RIGHT) ? text_inset : 0);
}


/* List widgets (FUN_00bd36e0) merge column and widget align flags;
   these do the same merge, swapping right-align for FLIPPED_RIGHT. */
__attribute__((naked)) static void list_flags_edx(void)
{
    __asm__("orl 0x4dc(%eax), %edx\n\t"
            "testl $2, %edx\n\t"
            "jz 1f\n\t"
            "andl $0xfffffffd, %edx\n\t"
            "orl $0x40000000, %edx\n"
            "1:\n\t"
            "ret");
}

__attribute__((naked)) static void list_flags_eax(void)
{
    __asm__("orl 0x4dc(%ecx), %eax\n\t"
            "testl $2, %eax\n\t"
            "jz 1f\n\t"
            "andl $0xfffffffd, %eax\n\t"
            "orl $0x40000000, %eax\n"
            "1:\n\t"
            "ret");
}

static const struct code_patch ltr_patches[] = {
    /* Bidi pass: every run gets level 0 (LTR). */
    { 0x00BDB3A9, 3,
      { 0x8B, 0x4D, 0xEC },
      { 0x31, 0xC9, 0x90 } },
    /* Bracket mirroring when drawing. */
    { 0x00BD9D3A, 7,
      { 0x0F, 0xB6, 0x91, 0xA8, 0x01, 0x00, 0x00 },
      { 0xBA, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90 } },
    /* Bracket mirroring in word wrap (chat). */
    { 0x00BDE51E, 7,
      { 0x0F, 0xB6, 0x91, 0xA8, 0x01, 0x00, 0x00 },
      { 0xBA, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90 } },
    /* Text widgets (FUN_00bf4660) align left instead of right. */
    { 0x00BF46B6, 3,
      { 0x83, 0xC8, 0x02 },
      { 0x83, 0xE0, 0xFD } },
    { 0x00BD4776, 6,
      { 0x0B, 0x90, 0xDC, 0x04, 0x00, 0x00 },
      { 0 }, list_flags_edx },
    { 0x00BD4867, 6,
      { 0x0B, 0x81, 0xDC, 0x04, 0x00, 0x00 },
      { 0 }, list_flags_eax },
    { 0x00BDDA97, 8,
      { 0x8B, 0x55, 0x10, 0x8B, 0x02, 0x89, 0x45, 0xF0 },
      { 0 }, (void (*)(void))align_left, 1 },
    /* Radio groups (chat tabs, FUN_00bd76e0) add each new button to the right. */
    { 0x00BD7859, 6,
      { 0x2B, 0x95, 0x5C, 0xFF, 0xFF, 0xFF },
      { 0x03, 0x95, 0x5C, 0xFF, 0xFF, 0xFF } },
    { 0x00BD786D, 6,
      { 0x2B, 0x8D, 0x5C, 0xFF, 0xFF, 0xFF },
      { 0x03, 0x8D, 0x5C, 0xFF, 0xFF, 0xFF } },
};

#define PATCH_COUNT (sizeof(ltr_patches) / sizeof(ltr_patches[0]))

static int write_code(unsigned char *code, const unsigned char *bytes, SIZE_T size)
{
    DWORD old_protect;

    if (!VirtualProtect(code, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return 0;
    }
    memcpy(code, bytes, size);
    VirtualProtect(code, size, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), code, size);
    return 1;
}

/* A patch with a call target becomes call rel32 padded with nops. */
static void build_replacement(const struct code_patch *patch, unsigned char *code,
                              unsigned char *bytes)
{
    DWORD rel;

    if (patch->call == NULL) {
        memcpy(bytes, patch->replacement, patch->size);
        return;
    }
    if (patch->push_frame) {
        *bytes++ = 0x55;
        code++;
    }
    rel = (DWORD)patch->call - (DWORD)(code + 5);
    bytes[0] = 0xE8;
    memcpy(bytes + 1, &rel, sizeof(rel));
    memset(bytes + 5, 0x90, patch->size - 5 - patch->push_frame);
}

static unsigned char *patch_target(unsigned char *base, const struct code_patch *patch)
{
    return base + (patch->address - EXE_IMAGE_BASE);
}

/* Writes nothing unless every site matches. */
static void apply_ltr_patches(void)
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    size_t i;

    if (base == NULL) {
        return;
    }

    for (i = 0; i < PATCH_COUNT; i++) {
        const struct code_patch *patch = &ltr_patches[i];

        if (memcmp(patch_target(base, patch), patch->expected, patch->size) != 0) {
            OutputDebugStringA("bridge: unknown exe, text direction unchanged\n");
            return;
        }
    }

    for (i = 0; i < PATCH_COUNT; i++) {
        const struct code_patch *patch = &ltr_patches[i];
        unsigned char *code = patch_target(base, patch);
        unsigned char bytes[MAX_PATCH_SIZE];

        build_replacement(patch, code, bytes);
        write_code(code, bytes, patch->size);
    }
}

void apply_text_direction(void)
{
    char direction[8];
    char font_info[MAX_PATH];

    /* The language folder's FontInfo.ini, as the game will read it. */
    if (!lang_overlay_resolve_a(FONT_INFO_PATH, font_info, sizeof(font_info))) {
        lstrcpynA(font_info, FONT_INFO_PATH, sizeof(font_info));
    }
    GetPrivateProfileStringA("FontInfo", "TextDirection", "",
                             direction, sizeof(direction), font_info);

    if (lstrcmpiA(direction, "LTR") == 0) {
        text_inset = GetPrivateProfileIntA("FontInfo", "TextInset", 8, font_info);
        apply_ltr_patches();
    }
}
