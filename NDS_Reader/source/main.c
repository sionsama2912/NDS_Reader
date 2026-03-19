#include <nds.h>
#include <fat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfont_0.h"
#include "font_data.h"

// Biến toàn cục
extern const unsigned int vfont_0Bitmap[];
char* full_text = NULL;
const char* current_page_ptr = NULL;
const char* page_history[100];
int page_idx = 0;
bool is_reading = false;

#define MAX_FILES 30
char file_names[MAX_FILES][256];
int total_files = 0;
int selected_idx = 0;

// --- KHAI BÁO NGUYÊN MẪU HÀM (Để tránh lỗi implicit declaration) ---
void updateMenu();
void initMenu();
void initReadingMode();
const char* renderDualScreen(const char* text);
int getWordWidth(const char* text);

// --- [HÀM HỖ TRỢ HIỂN THỊ] ---

const char* decode_utf8(const char* s, unsigned int* code) {
    unsigned char b = (unsigned char)*s++;
    if (b < 0x80) { *code = b; return s; }
    if ((b & 0xe0) == 0xc0) {
        *code = (b & 0x1f) << 6 | (unsigned char)(*s++ & 0x3f);
        return s;
    }
    if ((b & 0xf0) == 0xe0) {
        unsigned char b2 = (unsigned char)*s++;
        unsigned char b3 = (unsigned char)*s++;
        *code = (b & 0x0f) << 12 | (unsigned char)(b2 & 0x3f) << 6 | (unsigned char)(b3 & 0x3f);
        return s;
    }
    *code = 32; return s;
}

const CharDef* getCharDef(unsigned int id) {
    for (int i = 0; i < FONT_CHAR_COUNT; i++) {
        if (vietnamese_font[i].id == (unsigned short)id) return &vietnamese_font[i];
    }
    return NULL;
}

int getWordWidth(const char* text) {
    int width = 0;
    unsigned int code;
    while (*text && *text != ' ' && *text != '\n' && *text != '\r') {
        const char* next = decode_utf8(text, &code);
        const CharDef* ch = getCharDef(code);
        width += (ch ? ch->xadvance : 4);
        text = next;
    }
    return width;
}

void drawCharBook(unsigned int id, int bX, int bY, u16* vram) {
    const CharDef* ch = getCharDef(id);
    if (!ch) return;
    u8* bitmap = (u8*)vfont_0Bitmap;
    for (int cy = 0; cy < ch->height; cy++) {
        for (int cx = 0; cx < ch->width; cx++) {
            u8 colorIdx = bitmap[(ch->y + cy) * 256 + (ch->x + cx)];
            if (colorIdx > 0) {
                int drawX = bX + cx + ch->xoffset;
                int drawY = bY + cy + ch->yoffset;
                if (drawX >= 0 && drawX < 192 && drawY >= 0 && drawY < 256) {
                    vram[drawX * 256 + (255 - drawY)] = RGB15(31, 31, 31);
                }
            }
        }
    }
}

// --- [HÀM DÀN TRANG DUAL SCREEN] ---

const char* renderDualScreen(const char* text) {
    // Địa chỉ Bank D trong chế độ LCD mode
    u16* vramD = (u16*)0x06860000;

    // Xóa màn hình
    for(int i=0; i<256*192; i++) {
        VRAM_A[i] = RGB15(0,0,0);
        vramD[i] = RGB15(0,0,0);
    }

    if (!text || *text == '\0') return NULL;

    unsigned int code;
    int curX = 12, curY = 20;
    u16* current_vram = VRAM_A;
    bool on_second_screen = false;

    while (*text) {
        const char* saved_pos = text;

        if (*text == '\n' || *text == '\r') {
            text++; curX = 12; curY += 16;
        } else if (*text == ' ') {
            text++; curX += 4;
        } else {
            // Kiểm tra word wrap
            int wWidth = getWordWidth(text);
            if (curX + wWidth > 175) {
                curX = 12; curY += 16;
            }

            // Chuyển màn hình nếu hết chỗ
            if (curY > 240) {
                if (!on_second_screen) {
                    current_vram = vramD;
                    on_second_screen = true;
                    curX = 12; curY = 20;
                } else {
                    return saved_pos; // Trả về để lật trang tiếp theo
                }
            }

            text = decode_utf8(text, &code);
            drawCharBook(code, curX, curY, current_vram);
            const CharDef* ch = getCharDef(code);
            curX += (ch ? ch->xadvance : 4);
        }

        // Cần kiểm tra curY một lần nữa sau khi xuống dòng do \n
        if (curY > 240 && !on_second_screen) {
            current_vram = vramD;
            on_second_screen = true;
            curX = 12; curY = 20;
        } else if (curY > 240 && on_second_screen) {
            return text;
        }
    }
    return NULL;
}

// --- [MENU & FILE SYSTEM] ---

void scanFiles() {
    DIR *dir = opendir("/");
    struct dirent *ent;
    total_files = 0;
    if (dir) {
        while ((ent = readdir(dir)) != NULL && total_files < MAX_FILES) {
            if (strstr(ent->d_name, ".txt") || strstr(ent->d_name, ".TXT")) {
                snprintf(file_names[total_files], 255, "%s", ent->d_name);
                total_files++;
            }
        }
        closedir(dir);
    }
}

void updateMenu() {
    consoleClear();
    printf("\n === NDS READER V4 - DUAL SCREEN ===\n\n");
    if (total_files == 0) printf(" Khong tim thay file .txt!\n");
    for (int i = 0; i < total_files; i++) {
        printf("%c [%s]\n", (i == selected_idx ? '>' : ' '), file_names[i]);
    }
    printf("\n\n A: Mo sach | B: Menu | L/R: Lat trang");
}

void initMenu() {
    is_reading = false;
    videoSetModeSub(MODE_0_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleDemoInit();
    updateMenu();
}

void initReadingMode() {
    videoSetMode(MODE_FB0);
    vramSetBankA(VRAM_A_LCD);

    videoSetModeSub(MODE_FB0);
    // Sửa lỗi: Sử dụng VRAM_D_LCD để map Bank D cho màn hình dưới
    vramSetBankD(VRAM_D_LCD);
    is_reading = true;
}

int main(void) {
    powerOn(POWER_ALL_2D);
    if (fatInitDefault()) { scanFiles(); }
    initMenu();

    while(1) {
        scanKeys();
        u32 keys = keysDown();

        if (!is_reading) {
            if (keys & KEY_UP && selected_idx > 0) { selected_idx--; updateMenu(); }
            if (keys & KEY_DOWN && selected_idx < total_files - 1) { selected_idx++; updateMenu(); }
            if (keys & KEY_A && total_files > 0) {
                FILE* f = fopen(file_names[selected_idx], "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long size = ftell(f);
                    rewind(f);
                    full_text = (char*)malloc(size + 1);
                    if (full_text) {
                        fread(full_text, 1, size, f);
                        full_text[size] = '\0';
                        fclose(f);
                        initReadingMode();
                        page_idx = 0;
                        page_history[page_idx] = full_text;
                        current_page_ptr = renderDualScreen(full_text);
                    }
                }
            }
        } else {
            if (keys & KEY_B) {
                if (full_text) { free(full_text); full_text = NULL; }
                initMenu();
            }
            if (keys & KEY_R && current_page_ptr != NULL) {
                if (page_idx < 99) {
                    page_idx++;
                    page_history[page_idx] = current_page_ptr;
                    current_page_ptr = renderDualScreen(current_page_ptr);
                }
            }
            if (keys & KEY_L && page_idx > 0) {
                page_idx--;
                current_page_ptr = renderDualScreen(page_history[page_idx]);
            }
        }
        swiWaitForVBlank();
    }
    return 0;
}
