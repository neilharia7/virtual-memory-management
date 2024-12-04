#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <array>
#include <iomanip>
#include <string>
#include <sstream>

#define PAGE_TABLE_SIZE 256
#define PAGE_SIZE 256
#define TLB_SIZE 16
#define FRAME_SIZE 256
#define FRAMES 256
#define PHYSICAL_MEMORY FRAMES * PAGE_SIZE

int main() {
    // Initialize physical memory
    std::vector<unsigned char> physical_memory(PHYSICAL_MEMORY);

    // Initialize TLB as a deque of pairs (page_number, frame_number)
    std::deque<std::pair<int, int>> TLB;

    // Initialize Page Table with all entries set to -1 (indicating that the page is not in memory)
    std::array<int, PAGE_TABLE_SIZE> page_table;
    page_table.fill(-1);

    // Variables to keep track of statistics
    int tlb_hits = 0;
    int page_faults = 0;
    int total_addresses = 0;
    int next_frame = 0;  // Next free frame in physical memory

    // Open the backing store file
    std::ifstream backing_store("BACKING_STORE.bin", std::ios::in | std::ios::binary);
    if (!backing_store) {
        std::cerr << "Error: Unable to open BACKING_STORE.bin" << std::endl;
        return 1;
    }

    // Open the addresses file
    std::ifstream address_file("addresses.txt");
    if (!address_file) {
        std::cerr << "Error: Unable to open addresses.txt" << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(address_file, line)) {
        total_addresses++;

        // Read the logical address and mask to 16 bits
        int logical_address = std::stoi(line);
        logical_address = logical_address & 0xFFFF;

        // Extract the page number and offset from logical address
        int page_number = (logical_address >> 8) & 0xFF;
        int offset = logical_address & 0xFF;

        // Check if page_number is in TLB
        bool tlb_hit = false;
        int frame_number = -1;
        for (const auto& entry : TLB) {
            if (entry.first == page_number) {
                frame_number = entry.second;
                tlb_hit = true;
                tlb_hits++;
                break;
            }
        }

        // If TLB miss, check page table
        if (!tlb_hit) {
            if (page_table[page_number] != -1) {
                // Page is in memory, get the frame number from page table
                frame_number = page_table[page_number];
            } else {
                // Page fault occurs
                page_faults++;

                // Read page from backing store
                backing_store.seekg(page_number * PAGE_SIZE, std::ios::beg);
                if (backing_store.fail()) {
                    std::cerr << "Error: Unable to seek in BACKING_STORE.bin" << std::endl;
                    return 1;
                }

                std::vector<unsigned char> page_data(PAGE_SIZE);
                backing_store.read(reinterpret_cast<char*>(page_data.data()), PAGE_SIZE);
                if (backing_store.fail()) {
                    std::cerr << "Error: Unable to read from BACKING_STORE.bin" << std::endl;
                    return 1;
                }

                // Load page into physical memory
                if (next_frame >= FRAMES) {
                    std::cerr << "Error: Physical memory is full." << std::endl;
                    return 1;
                }
                int start_address = next_frame * PAGE_SIZE;
                std::copy(page_data.begin(), page_data.end(), physical_memory.begin() + start_address);

                // Update page table
                frame_number = next_frame;
                page_table[page_number] = frame_number;
                next_frame++;
            }

            // Add the page number and frame number to the TLB
            if (TLB.size() >= TLB_SIZE) {
                TLB.pop_front();  // Remove the oldest entry
            }
            TLB.emplace_back(page_number, frame_number);
        }

        // Calculate the physical address
        int physical_address = (frame_number << 8) | offset;

        // Get the value stored at the physical address
        unsigned char value = physical_memory[physical_address];

        // Output the result
        std::cout << "Logical Address: " << logical_address
                  << " Physical Address: " << physical_address
                  << " Value: " << static_cast<int>(value) << std::endl;
    }

    // Print statistics
    std::cout << "\nStatistics:" << std::endl;
    std::cout << "Total addresses translated: " << total_addresses << std::endl;
    std::cout << "TLB Hits: " << tlb_hits << std::endl;
    std::cout << "TLB Hit Rate: " << std::fixed << std::setprecision(2)
              << static_cast<double>(tlb_hits) / total_addresses * 100 << "%" << std::endl;
    std::cout << "Page Faults: " << page_faults << std::endl;
    std::cout << "Page Fault Rate: " << std::fixed << std::setprecision(2)
              << static_cast<double>(page_faults) / total_addresses * 100 << "%" << std::endl;

    return 0;
}