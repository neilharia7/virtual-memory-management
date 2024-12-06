//
// Created by Neil Haria on 04/12/24.
//
#include <stdio.h>
#include <stdlib.h>

int main() {
    FILE *file = fopen("BACKING_STORE.bin", "wb");
    if (!file) {
        perror("Failed to open file");
        return 1;
    }

    // Write 256 pages of 256 bytes each (e.g., random data)
    for (int i = 0; i < 256; ++i) {
        unsigned char page[256];
        for (int j = 0; j < 256; ++j) {
            page[j] = rand() % 256; // Random byte
        }
        fwrite(page, sizeof(unsigned char), 256, file);
    }

    fclose(file);
    return 0;
}
