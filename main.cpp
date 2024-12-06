#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <array>
#include <string>
#include <iomanip>
#include <cstdint>
#include <optional>
#include <algorithm>

#define PAGE_TABLE_SIZE 256     // max number of pages in the virtual mem
#define PAGE_SIZE 256           // size of each page in bytes
#define TLB_SIZE 16             // buffer size
#define FRAME_SIZE 256          // size of each physical mem frame
#define FRAMES 256              // total frames in the physical mem
#define BITSHIFT 8
#define MASK 0xFFFF             // mask to extract the lower 16 bits from the logical address
#define OFFSET_MASK 0xFF        // mask to extract the lowest 8 bits from logical address

using namespace std;


/** @class TLB
 *  @brief Translational Lookaside Buffer implementation
 *
 * Purpose: To provide a fast cache for page number to frame number translation
 *
 */
class TLB {
private:
    // internal storage for TLB entries
    // using deque to facilitate FIFO replacement strategy
    // entry -> pair of <page number, frame number>
    std::deque<std::pair<uint8_t, uint8_t>> tlbEntries;
public:
    /**
     * Searches for a page number in the TLB
     * @param pageNumber the virtual page number to lookup
     * @return optional frame number on TLB hit, empty otherwise
     */
    std::optional<uint8_t> getFrameNumber(uint8_t pageNumber) {
        // iterate through the TLB to find a matching page number
        for (auto iterator = tlbEntries.begin(); iterator != tlbEntries.end(); ++iterator) {
            if (iterator->first == pageNumber) {
                // got it, capture frame
                uint8_t frameNumber = iterator->second;

                // remove current entry and add it to the back
                // make it work like LRU
                tlbEntries.erase(iterator);
                tlbEntries.emplace_back(pageNumber, frameNumber);
                return frameNumber;
            }
        }
        return std::nullopt; // TLB miss
    }

    /**
     * This func adds a new entry to the TLB, while managing capacity
     * @param pageNumber virtual page number to add
     * @param frameNumber corresponding physical frame number
     */
    void addEntry(uint8_t pageNumber, uint8_t frameNumber) {

        // remove any existing entry for the same page to prevent duplicates
        auto iterator = std::find_if(tlbEntries.begin(), tlbEntries.end(),
                               [&](const auto& entry) { return entry.first == pageNumber; });
        if (iterator != tlbEntries.end()) {
            tlbEntries.erase(iterator);
        }

        // enforce TLB size limit using FIFO
        if (tlbEntries.size() >= TLB_SIZE) {
            tlbEntries.pop_front(); // nuke the oldest entry
        }

        // add new entry to the end of queue
        tlbEntries.emplace_back(pageNumber, frameNumber);
    }
};

/** @class PageTable
 *  @brief Manages virtual-to-physical memory mapping
 */
class PageTable {
private:
    // stores frame numbers, with -1 -> invalid/unloaded page
    std::array<int16_t, PAGE_TABLE_SIZE> pageTable{};
public:
    /**
     * Constructor: initialize all entries as invalid (-1)
     */
    PageTable() {
        pageTable.fill(-1);
    }

    /**
     * Fetches the frame number for a given page
     * @param pageNumber virtual page number to lookup
     * @return optional frame number or empty in case of page fault
     */
    std::optional<uint8_t> getFrameNumber(uint8_t pageNumber) {
        int16_t frameNumber = pageTable[pageNumber];
        return frameNumber == -1 ? std::nullopt : static_cast<uint8_t>(frameNumber);
    }

    /**
     * Updates page table with a new page <> frame mapping
     * @param pageNumber virtual page number
     * @param frameNumber physical frame number
     */
    void setFrameNumber(uint8_t pageNumber, uint8_t frameNumber) {
        pageTable[pageNumber] = frameNumber;
    }
};

/** @class PhysicalMemory
 *  @brief Simulates physical memory organization
 */
class PhysicalMemory {
private:
    // 2D array to represent physical memory frames
    // each frame -> fixed size array of bytes
    std::array<std::array<int8_t, FRAME_SIZE>, FRAMES> memory{}; // Memory frames
public:
    /**
     *  Loads a complete page into a specific memory frame
     * @param frameNumber target frame to load the page into
     * @param pageData pointer of the source pageData
     */
    void loadPage(uint8_t frameNumber, const int8_t* pageData) {
        std::copy_n(pageData, FRAME_SIZE, memory[frameNumber].begin());
    }

    /**
     * Retrieves single byte from a specific physicalAddress
     * @param physicalAddress fully translated memory address
     * @return byte value of the specified address
     *
     * Translation:
     *  high order bits -> frame number
     *  low order bits -> offset within the frame
     */
    int8_t getValue(uint16_t physicalAddress) const {
        uint8_t frameNumber = (physicalAddress >> 8) & 0xFF;
        uint8_t offset = physicalAddress & 0xFF;
        return memory[frameNumber][offset];
    }
};

/** @class BackingStore
 *  @brief Simulates secondary storage for page loading
 *
 */
class BackingStore {
private:
    std::ifstream backingStoreFile;
public:
    /**
     * Constructor: Open BACKING_STORE.bin file
     * @param fileName path to the BACKING_STORE.bin file
     *
     * Terminate if file cannot be opened
     */
    explicit BackingStore(const std::string& fileName) {
        backingStoreFile.open(fileName, std::ios::binary);
        if (!backingStoreFile) {
            std::cerr << "Error opening backing store file: " << fileName << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    /**
     * Destructor: Ensure file is closed
     */
    ~BackingStore() {
        if (backingStoreFile.is_open()) {
            backingStoreFile.close();
        }
    }

    /**
     * Read a specific page from the backing store
     * @param pageNumber page to fetch
     * @param buffer output buffer to store the page contents
     */
    void readPage(uint8_t pageNumber, int8_t* buffer) {
        backingStoreFile.clear(); // Clear any error flags
        backingStoreFile.seekg(pageNumber * PAGE_SIZE, std::ios::beg);
        backingStoreFile.read(reinterpret_cast<char*>(buffer), 256);

        if (!backingStoreFile) {
            std::cerr << "Error reading page from backing store" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
};

int main(int argc, char* argv[]) {
    // validate cmd line args
    if (argc != 2) {
        std::cerr << "Usage: ./a.out addresses.txt" << std::endl;
        return EXIT_FAILURE;
    }

    std::string addressFileName = argv[1];

    TLB tlb;
    PageTable pageTable;
    PhysicalMemory physicalMemory;
    BackingStore backingStore("BACKING_STORE.bin");

    std::ifstream addressFile(addressFileName);
    if (!addressFile) {
        std::cerr << "Error opening address file: " << addressFileName << std::endl;
        return EXIT_FAILURE;
    }

    uint16_t nextAvailableFrame = 0;
    int totalAddresses = 0;
    int tlbHits = 0;
    int pageFaults = 0;

    // processing each logical address
    std::string line;
    while (std::getline(addressFile, line)) {
        totalAddresses++;

        // convert string to 32-bit unsigned int
        // ensure that we are working with 16-bit logical address
        // extract the lower 16 bits of the logical address using 0xFFFF ('1' * 16 in bin)
        uint32_t logicalAddress = std::stoul(line) & MASK;

        // shift the logical address 8 bits to right i.e. (8-15 -> 0-7)
        // mask the result to ensure only lower 8 bits are kept
        uint8_t pageNumber = (logicalAddress >> BITSHIFT) & OFFSET_MASK;    // (8-15) bits of logical address
        uint8_t offset = logicalAddress & OFFSET_MASK;  // (0-7) bits of logical address

        // check in TLB
        std::optional<uint8_t> frameNumberOpt = tlb.getFrameNumber(pageNumber);

        // TLB miss -> proceed to page table
        if (bool _ = frameNumberOpt.has_value()) {
            tlbHits++;
        } else {
            frameNumberOpt = pageTable.getFrameNumber(pageNumber);

            // page fault -> load page from backing store
            if (!frameNumberOpt.has_value()) {
                pageFaults++;

                // read page from backing store
                int8_t pageData[PAGE_SIZE];
                backingStore.readPage(pageNumber, pageData);

                // load page into physical memory
                physicalMemory.loadPage(nextAvailableFrame, pageData);

                // update page table
                pageTable.setFrameNumber(pageNumber, nextAvailableFrame);

                // update TLB
                tlb.addEntry(pageNumber, nextAvailableFrame);

                frameNumberOpt = nextAvailableFrame;

                nextAvailableFrame++;
                if (nextAvailableFrame >= FRAME_SIZE) {
                    std::cerr << "Error: Physical memory is full." << std::endl;
                    return EXIT_FAILURE;
                }
            } else {
                // update TLB with page table result
                tlb.addEntry(pageNumber, frameNumberOpt.value());
            }
        }

        // translate to physical address and capture value
        uint8_t frameNumber = frameNumberOpt.value();
        uint16_t physicalAddress = (frameNumber << 8) | offset;
        int8_t value = physicalMemory.getValue(physicalAddress);

        // terminal logs
        std::cout << "Logical Address: 0x" << std::hex << std::setw(4) << std::setfill('0') << logicalAddress
                  << " Physical Address: 0x" << std::hex << std::setw(4) << std::setfill('0') << physicalAddress
                  << " Value: " << std::dec << static_cast<int>(value) << std::endl;
    }

    // compute stats for display
    double pageFaultRate = static_cast<double>(pageFaults) / totalAddresses * 100.0;
    double tlbHitRate = static_cast<double>(tlbHits) / totalAddresses * 100.0;

    // terminal logs
    std::cout << "Page Fault Rate = " << pageFaultRate << "%" << std::endl;
    std::cout << "TLB Hit Rate = " << tlbHitRate << "%" << std::endl;

    return EXIT_SUCCESS;
}