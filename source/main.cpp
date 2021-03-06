#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "device.h"

#include "console.h"
#include "binaries.h"
#include "nds_platform.h"

using namespace flashcart_core;

// FIXME: not fully overwrite
u8 orig_flashrom[0xA0000] = {0};
u8 curr_flashrom[0xA0000] = {0};

PrintConsole topScreen;
PrintConsole bottomScreen;

void printBootMessage() {
    consoleSelect(&topScreen);

    iprintf("=    NDS NTRBOOT FLASHER    =\n\n");

    consoleSelect(&bottomScreen);

    iprintf("* Your cart cannot be used  *\n");
    iprintf("* as a flashcart after it   *\n");
    iprintf("* is flashed (except AK2i)  *\n");
#ifndef NDSI_MODE
    iprintf("* DO NOT CLOSE THIS APP IF  *\n");
    iprintf("* YOU WISH TO RESTORE THE   *\n");
    iprintf("* FLASH                     *\n");
#else
    iprintf("* WARNING: ONLY TESTED ON   *\n");
    iprintf("* 3DS TWL MODE              *\n");
#endif
    iprintf("* AT YOUR OWN RISK          *\n");

    waitPressA();
    consoleClear();
}

void printWarningEject() {
    iprintf("DO NOT EJECT FLASHCART\n");
    iprintf("UNTIL GET SPECIAL ORDER\n\n");
}

Flashcart* selectCart() {
    uint32_t idx = 0;

    consoleSelect(&bottomScreen);
    consoleClear();

    iprintf("<UP/DOWN> Select flashcart\n");
    iprintf("<A> Confirm\n");
#ifndef NDSI_MODE
    iprintf("<B> Cancel");
#else
    iprintf("<B> Exit");
#endif
    while (true) {
        consoleSelect(&topScreen);
        consoleClear();
        Flashcart *cart = flashcart_list->at(idx);
        iprintf("Flashcart: %s\n", cart->getName());
        //iprintf("    - %s\n", cart->getAuthor());
        iprintf("\n%s", cart->getDescription());

        while (true) {
            scanKeys();
            uint32_t keys = keysDown();
            if (keys & KEY_UP) {
                if (idx > 0) {
                    idx--;
                }
                break;
            }
            if (keys & KEY_DOWN) {
                idx++;
                if (idx >= flashcart_list->size()) {
                    idx = flashcart_list->size() - 1;
                }
                break;
            }
            if (keys & KEY_A) {
                return cart;
            }
            if (keys & KEY_B) {
                return NULL;
            }
            swiWaitForVBlank();
        }
    }
}

u8 dump(Flashcart *cart) {
    consoleSelect(&bottomScreen);
    consoleClear();
    iprintf("Reading cart flash\n");

    // FIXME: we need to check flashcart's data position
    u32 length = cart->getMaxLength();
    if (length > 0xA0000) {
        length = 0xA0000;
    }
    memset(orig_flashrom, 0, 0xA0000);
    u8 *temp = orig_flashrom;

    if (!cart->readFlash(0, length, temp)) {
        return 1;
    }

#if defined(DEBUG_DUMP)
    for (int i = 0; i < length; i+=16) {
        iprintf("%05X:", i);
        for (int j = 0; j < 16; j++) {
            iprintf("%02X", orig_flashrom[i + j]);
        }
        iprintf("\n");
#if DEBUG_DUMP == 2
        waitPressA();
#else
        break;
#endif
    }
#endif
    return 0;
}

int8_t selectDeviceType() {
    consoleSelect(&bottomScreen);
    consoleClear();

    iprintf("Select 3DS device type\n\n");
    iprintf("<A> RETAIL\n");
    iprintf("<X> DEV\n");
    iprintf("<B> Cancel");

    while (true) {
        scanKeys();
        u32 keys = keysDown();
        if (keys & KEY_A) {
            return 0;
        }
        if (keys & KEY_X) {
            return 1;
        }
        if (keys & KEY_B) {
            return -1;
        }
        swiWaitForVBlank();
    }
}

int inject(Flashcart *cart) {
    int8_t deviceType = selectDeviceType();
    if (deviceType < 0) {
        return -1;
    }

    u8 *blowfish_key = deviceType ? blowfish_dev_bin : blowfish_retail_bin;
    u8 *firm = deviceType ? boot9strap_ntr_dev_firm : boot9strap_ntr_firm;
    u32 firm_size = deviceType ? boot9strap_ntr_dev_firm_size : boot9strap_ntr_firm_size;

    consoleSelect(&bottomScreen);
    consoleClear();

    iprintf("Inject ntrboothax\n\n");
    printWarningEject();

    cart->injectNtrBoot(blowfish_key, firm, firm_size);
    iprintf("\nDone\n\n");

    waitPressA();
    return 0;
}

int compareBuf(u8 *buf1, u8 *buf2, u32 len) {
    for (uint32_t i = 0; i < len; i++) {
        if (buf1[i] != buf2[i]) {
            return 0;
        }
    }
    return 1;
}

int restore(Flashcart *cart) {
    u32 length = cart->getMaxLength();
    if (length > 0xA0000) {
        length = 0xA0000;
    }

    memset(curr_flashrom, 0, 0xA0000);
    u8 *temp = curr_flashrom;

    consoleSelect(&bottomScreen);
    consoleClear();

    iprintf("Restore original flash\n\n");
    printWarningEject();

    iprintf("reading current flash\n");
    cart->readFlash(0, length, temp);

    iprintf("\n\nrestoring original flash\n");

    const int chunk_size = 64 * 1024;
    int chunk = 0;
    disablePrintProgress();
    for (uint32_t i = 0; i < length; i += chunk_size) {
        if (!compareBuf(orig_flashrom + i, curr_flashrom + i, chunk_size)) {
            iprintf("\rWriting chunk          %08X", chunk);
            cart->writeFlash(i, chunk_size, orig_flashrom + i);
        }
        chunk += 1;
    }
    enablePrintProgress();

    memset(curr_flashrom, 0, 0xA0000);
    temp = curr_flashrom;

    iprintf("\n\nverifying...  ");
    cart->readFlash(0, length, temp);

    for (uint32_t i = 0; i < length; i += chunk_size) {
        if (!compareBuf(orig_flashrom + i, curr_flashrom + i, chunk_size)) {
            iprintf("\nfail");
            goto exit;
        }
    }
    iprintf("\nok");

exit:
    iprintf("\nDone\n\n");

    waitPressA();

    return 0;
}

int waitConfirmLostDump() {
    consoleSelect(&bottomScreen);
    consoleClear();
    iprintf("If you continue, the cart's\n");
    iprintf("flash will be lost.\n\n");
    iprintf("<Y> Continue\n");
    iprintf("<B> Cancel\n");

    while (true) {
        scanKeys();
        u32 keys = keysDown();

        if (keys & KEY_Y) {
            return 1;
        }
        if (keys & KEY_B) {
            return 0;
        }
        swiWaitForVBlank();
    }
}

int main(void) {
    videoSetMode(MODE_0_2D);
	videoSetModeSub(MODE_0_2D);

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&topScreen, 3,BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
	consoleInit(&bottomScreen, 3,BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

    sysSetBusOwners(true, true); // give ARM9 access to the cart

    printBootMessage();

    Flashcart *cart;

select_cart:
    cart = NULL;
    while (true) {
        cart = selectCart();
        if (cart == NULL) {
#ifdef NDSI_MODE
            return 0;
#else
            continue;
#endif
        }

        consoleSelect(&bottomScreen);
        consoleClear();
        reset();
        if (cart->initialize()) {
            break;
        }
        iprintf("Flashcart setup failed\n");
        waitPressA();
    }

    bool support_restore = true;
#ifndef NDSI_MODE
    if (!strcmp(cart->getName(), "R4iSDHC family")) {
        support_restore = false;
    }

    if (support_restore && dump(cart)) {
        iprintf("Flash read failed\n");
        waitPressA();
        goto select_cart;
    }
#else
    support_restore = false;
#endif

    while (true) {
flash_menu:
        consoleSelect(&bottomScreen);
        consoleClear();
#ifndef NDSI_MODE
        if (support_restore) {
            iprintf("SUPPORT RESTORE\n");
            iprintf("You can swap another\n");
            iprintf("same type cartridge,\n");
            iprintf("without the SD card.\n\n");
        }
#endif
        if (!support_restore) {
            iprintf("NOT SUPPORT RESTORE\n");
            iprintf("Flashcart functionality will\n");
            iprintf("be lost.\n\n");
        }

        iprintf("<A> Inject ntrboothax\n");
#ifndef NDSI_MODE
        if (support_restore) {
            iprintf("<X> Restore flash\n");
            iprintf("<Y> Change cartridge\n");
        }
        iprintf("<B> Return\n");
#else
        iprintf("<B> Exit\n");
#endif

        while (true) {
            scanKeys();
            u32 keys = keysDown();

            if (keys & KEY_A) {
                inject(cart);
                break;
            }

#ifndef NDSI_MODE
            if (support_restore) {
                if (keys & KEY_X) {
                    restore(cart);
                    break;
                }
                if (keys & KEY_Y) {
                    cart->shutdown();
                    do {
                        reset();
                    } while(!cart->initialize());
                    goto flash_menu;
                }
                if (keys & KEY_B) {
                    if (waitConfirmLostDump()) {
                        cart->shutdown();
                        goto select_cart;
                    }
                    goto flash_menu;
                }
            } else {
                if (keys & KEY_B) {
                    cart->shutdown();
                    goto flash_menu;
                }
            }
#else
            if (keys & KEY_B) {
                cart->shutdown();
                return 0;
            }
#endif
            swiWaitForVBlank();
        }
    }

    return 0;
}
