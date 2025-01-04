#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <map>
#include <vector>
#include <list>
#include <unordered_map>
#include <string>
#include <memory>
#include <sstream>
#include <limits>
#include <thread>
#include <queue>
#include <optional>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <cassert>
#include <cstring> 
#include <exception>
#include <atomic>

#define UNUSED(p)  ((void)(p))

enum FieldType { INT, FLOAT, STRING };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;
    std::unique_ptr<char[]> data;
    size_t data_length;

public:
    Field(int i) : type(INT) { 
        data_length = sizeof(int);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &i, data_length);
    }

    Field(float f) : type(FLOAT) { 
        data_length = sizeof(float);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &f, data_length);
    }

    Field(const std::string& s) : type(STRING) {
        data_length = s.size() + 1;  // include null-terminator
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), s.c_str(), data_length);
    }

    Field& operator=(const Field& other) {
        if (&other == this) {
            return *this;
        }
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
        return *this;
    }

    Field(Field&& other){
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
    }

    FieldType getType() const { return type; }
    int asInt() const { 
        return *reinterpret_cast<int*>(data.get());
    }
    float asFloat() const { 
        return *reinterpret_cast<float*>(data.get());
    }
    std::string asString() const { 
        return std::string(data.get());
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << type << ' ' << data_length << ' ';
        if (type == STRING) {
            buffer << data.get() << ' ';
        } else if (type == INT) {
            buffer << *reinterpret_cast<int*>(data.get()) << ' ';
        } else if (type == FLOAT) {
            buffer << *reinterpret_cast<float*>(data.get()) << ' ';
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Field> deserialize(std::istream& in) {
        int type; in >> type;
        size_t length; in >> length;
        if (type == STRING) {
            std::string val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == INT) {
            int val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == FLOAT) {
            float val; in >> val;
            return std::make_unique<Field>(val);
        }
        return nullptr;
    }

    void print() const{
        switch(getType()){
            case INT: std::cout << asInt(); break;
            case FLOAT: std::cout << asFloat(); break;
            case STRING: std::cout << asString(); break;
        }
    }
};

class Tuple {
public:
    std::vector<std::unique_ptr<Field>> fields;

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
    }

    size_t getSize() const {
        size_t size = 0;
        for (const auto& field : fields) {
            size += field->data_length;
        }
        return size;
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << fields.size() << ' ';
        for (const auto& field : fields) {
            buffer << field->serialize();
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Tuple> deserialize(std::istream& in) {
        auto tuple = std::make_unique<Tuple>();
        size_t fieldCount; in >> fieldCount;
        for (size_t i = 0; i < fieldCount; ++i) {
            tuple->addField(Field::deserialize(in));
        }
        return tuple;
    }

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

static constexpr size_t PAGE_SIZE = 4096;  // Fixed page size
static constexpr size_t MAX_SLOTS = 512;   // Fixed number of slots
static constexpr size_t MAX_PAGES= 1000;   // Total Number of pages that can be stored
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

struct Slot {
    bool empty = true;                  // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS;

    SlottedPage(){
        // Empty page -> initialize slot array inside page
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }
    }

    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple) {

        // Serialize the tuple into a char array
        auto serializedTuple = tuple->serialize();
        size_t tuple_size = serializedTuple.size();

        //std::cout << "Tuple size: " << tuple_size << " bytes\n";

        // Check for first slot with enough space
        size_t slot_itr = 0;
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());        
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == true and 
                slot_array[slot_itr].length >= tuple_size) {
                break;
            }
        }
        if (slot_itr == MAX_SLOTS){
            //std::cout << "Page does not contain an empty slot with sufficient space to store the tuple.";
            return false;
        }

        // Identify the offset where the tuple will be placed in the page
        // Update slot meta-data if needed
        slot_array[slot_itr].empty = false;
        size_t offset = INVALID_VALUE;
        if (slot_array[slot_itr].offset == INVALID_VALUE){
            if(slot_itr != 0){
                auto prev_slot_offset = slot_array[slot_itr - 1].offset;
                auto prev_slot_length = slot_array[slot_itr - 1].length;
                offset = prev_slot_offset + prev_slot_length;
            }
            else{
                offset = metadata_size;
            }

            slot_array[slot_itr].offset = offset;
        }
        else{
            offset = slot_array[slot_itr].offset;
        }

        if(offset + tuple_size >= PAGE_SIZE){
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            return false;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= metadata_size);
        assert(offset + tuple_size < PAGE_SIZE);

        if (slot_array[slot_itr].length == INVALID_VALUE){
            slot_array[slot_itr].length = tuple_size;
        }

        // Copy serialized data into the page
        std::memcpy(page_data.get() + offset, 
                    serializedTuple.c_str(), 
                    tuple_size);

        return true;
    }

    void deleteTuple(size_t index) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        size_t slot_itr = 0;
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if(slot_itr == index and
               slot_array[slot_itr].empty == false){
                slot_array[slot_itr].empty = true;
                break;
               }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void print() const{
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == false){
                assert(slot_array[slot_itr].offset != INVALID_VALUE);
                const char* tuple_data = page_data.get() + slot_array[slot_itr].offset;
                std::istringstream iss(tuple_data);
                auto loadedTuple = Tuple::deserialize(iss);
                std::cout << "Slot " << slot_itr << " : [";
                std::cout << (uint16_t)(slot_array[slot_itr].offset) << "] :: ";
                loadedTuple->print();
            }
        }
        std::cout << "\n";
    }
};

const std::string database_filename = "buzzdb.dat";

class StorageManager {
public:    
    std::fstream fileStream;
    size_t num_pages = 0;
    std::mutex io_mutex;

public:
    StorageManager(){
        fileStream.open(database_filename, std::ios::in | std::ios::out);
        if (!fileStream) {
            // If file does not exist, create it
            fileStream.clear(); // Reset the state
            fileStream.open(database_filename, std::ios::out);
        }
        fileStream.close(); 
        fileStream.open(database_filename, std::ios::in | std::ios::out); 

        fileStream.seekg(0, std::ios::end);
        num_pages = fileStream.tellg() / PAGE_SIZE;

        // std::cout << "Storage Manager :: Num pages: " << num_pages << "\n";        
        if(num_pages == 0){
            extend();
        }
    }

    ~StorageManager() {
        if (fileStream.is_open()) {
            fileStream.close();
        }
    }

    // Read a page from disk
    std::unique_ptr<SlottedPage> load(uint16_t page_id) {
        std::lock_guard<std::mutex>  io_guard(io_mutex);
        assert(page_id < num_pages);
        
        fileStream.seekg(page_id * PAGE_SIZE, std::ios::beg);
        auto page = std::make_unique<SlottedPage>();
        // Read the content of the file into the page
        if(fileStream.read(page->page_data.get(), PAGE_SIZE)){
            // std::cout << "Page read successfully from file."<< page_id<< std::endl;
        }
        else{
            std::cerr << "Error: Unable to read data from the file :: page id "<<page_id<<" \n";
            exit(-1);
        }
        return page;
    }

    // Write a page to disk
    void flush(uint16_t page_id, const std::unique_ptr<SlottedPage>& page) {
        std::lock_guard<std::mutex>  io_guard(io_mutex);
        size_t page_offset = page_id * PAGE_SIZE;        

        // Move the write pointer
        fileStream.seekp(page_offset, std::ios::beg);
        fileStream.write(page->page_data.get(), PAGE_SIZE);        
        fileStream.flush();
    }

    // Extend database file by one page
    void extend() {
        std::lock_guard<std::mutex>  io_guard(io_mutex);
        std::cout << "Extending database file \n";

        // Create a slotted page
        auto empty_slotted_page = std::make_unique<SlottedPage>();

        // Move the write pointer
        fileStream.seekp(0, std::ios::end);

        // Write the page to the file, extending it
        fileStream.write(empty_slotted_page->page_data.get(), PAGE_SIZE);
        fileStream.flush();

        // Update number of pages
        num_pages += 1;
    }

    void extend(uint64_t till_page_id) {
        std::lock_guard<std::mutex>  io_guard(io_mutex); 
        uint64_t write_size = std::max(static_cast<uint64_t>(0), till_page_id + 1 - num_pages) * PAGE_SIZE;
        if(write_size > 0 ) {
            std::cout << "Extending database file till page id : "<<till_page_id<<" \n";
            char* buffer = new char[write_size];
            std::memset(buffer, 0, write_size);

            fileStream.seekp(0, std::ios::end);
            fileStream.write(buffer, write_size);
            fileStream.flush();
            
            num_pages = till_page_id+1;
        }
    }

};

using PageID = uint64_t;
using FrameID = uint64_t;

class Policy {
public:
    virtual bool touch(PageID page_id) = 0;
    virtual PageID evict() = 0;
    virtual ~Policy() = default;

    friend class BufferManager;
};

void printList(std::string list_name, const std::list<PageID>& myList) {
        std::cout << list_name << " :: ";
        for (const PageID& value : myList) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
}

constexpr size_t MAX_PAGES_IN_MEMORY = 10;

class BufferFrame {
private:
    friend class BufferManager;
    
    // TODO: Add necessary member variables, e.g., page ID, frame ID, flags for dirty and exclusive status, etc.
    PageID page_id;
    bool is_dirty = false;
    std::atomic<int> pin_count{0};
    std::shared_mutex mtx;
    bool is_exclusive = false;

public:
    std::unique_ptr<SlottedPage> page;  // The actual page data in memory

    // TODO: Add constructor(s) and any necessary methods here to manage a BufferFrame object.
    // HINT: Think about how you will manage the page data, dirty status, and locking mechanisms (if needed).
    BufferFrame() = default;
    BufferFrame(PageID pid, std::unique_ptr<SlottedPage> p)
        : page_id(pid), page(std::move(p)) {}

};

class buffer_full_error : public std::exception {
    public:
        const char *what() const noexcept override { return "buffer is full"; }
};

class BufferManager {
private:
    StorageManager storage_manager;      // Responsible for I/O operations (reading and writing pages to disk)
    std::vector<std::unique_ptr<BufferFrame>> buffer_pool;  // The buffer pool containing loaded pages
    
    // TODO: Add your implementation here for page locks, FIFO/LRU queues, and page use counters.
    // HINT: Consider the role of synchronization and locking for thread safety.
    std::unordered_map<PageID, size_t> page_table;
    std::list<size_t> fifo_queue;
    std::list<size_t> lru_list;
    std::mutex manager_mutex;
    
public:
    uint64_t capacity_;  // Number of pages that can be stored in memory at once
    uint64_t page_size_; // The size of each page in bytes

    /// Constructor
    BufferManager() {
        capacity_ = MAX_PAGES_IN_MEMORY;
        // TODO: Preallocate buffer frames, locks, and other necessary structures.
        // HINT: Ensure that you handle the initialization of page metadata and ensure thread safety.
        storage_manager.extend(MAX_PAGES);  // Extend storage for disk pages
        buffer_pool.reserve(capacity_);
    }

    /// Flushes a specific page to disk.
    void flushPage(FrameID frame_id) {
        // TODO: Implement logic to write the page data to disk if it's dirty.
        // HINT: Use the `StorageManager` to perform disk operations.
        //UNUSED(frame_id);
        std::lock_guard<std::mutex> lock(manager_mutex);

        if (frame_id >= buffer_pool.size()) return;

        BufferFrame* frame = buffer_pool[frame_id].get();
        std::shared_lock<std::shared_mutex> frame_lock(frame->mtx);
        if (frame->is_dirty) {
            storage_manager.flush(frame->page_id, frame->page);
            frame->is_dirty = false;
        }
    }

    /// Destructor. Ensures that all dirty pages are flushed to disk.
    ~BufferManager() {
        // TODO: Iterate through the buffer pool and flush any dirty pages to disk.
        // HINT: Consider thread safety if there are multiple threads unfixing pages concurrently.
        std::lock_guard<std::mutex> lock(manager_mutex);
        for (auto& frame_ptr : buffer_pool) {
            BufferFrame* frame = frame_ptr.get();
            std::shared_lock<std::shared_mutex> frame_lock(frame->mtx);
            if (frame->is_dirty) {
                storage_manager.flush(frame->page_id, frame->page);
                frame->is_dirty = false;
            }
        }
    }

    /// Fixes a page in memory (loads it if not already present) and returns a reference to it.
    /// Is thread-safe w.r.t. other concurrent calls to `fix_page()` and
    /// `unfix_page()`.     
    /// @param[in] page_id   The ID of the page to be fixed (loaded into memory).
    /// @param[in] exclusive Whether the page should be locked exclusively (for writing).
    /// @return Reference to the BufferFrame object for the fixed page.
    BufferFrame& fix_page(PageID page_id, bool exclusive) {
        // TODO: Implement logic to load the page if it's not already in memory.
        // HINT: Handle eviction if the buffer is full and synchronize access for thread safety.
        // UNUSED(page_id); UNUSED(exclusive);
        // throw buffer_full_error{};
        std::unique_lock<std::mutex> lock(manager_mutex);

        auto it = page_table.find(page_id);
        if (it != page_table.end()) {
            size_t frame_id = it->second;
            BufferFrame* frame = buffer_pool[frame_id].get();
            frame->pin_count++;
            auto fifo_it = std::find(fifo_queue.begin(), fifo_queue.end(), frame_id);

            if (fifo_it != fifo_queue.end()) {
                fifo_queue.erase(fifo_it);
                lru_list.push_back(frame_id);
            } else {
                auto lru_it = std::find(lru_list.begin(), lru_list.end(), frame_id);
                if (lru_it != lru_list.end()) {
                    lru_list.erase(lru_it);
                    lru_list.push_back(frame_id);
                }
            }
            lock.unlock();
            if (exclusive) {
                frame->mtx.lock();
                frame->is_exclusive = true;
            } else {
                frame->mtx.lock_shared();
            }
            return *frame;
        }

        if (buffer_pool.size() >= capacity_) {
            bool evicted = false;
            for (auto it = fifo_queue.begin(); it != fifo_queue.end(); ) {
                size_t victim_id = *it;
                BufferFrame* victim = buffer_pool[victim_id].get();
                if (victim->pin_count == 0) {
                    if (victim->is_dirty) {
                        storage_manager.flush(victim->page_id, victim->page);
                    }
                    page_table.erase(victim->page_id);
                    it = fifo_queue.erase(it);
                    victim->page_id = page_id;
                    victim->page = storage_manager.load(page_id);
                    victim->is_dirty = false;
                    victim->pin_count = 1;
                    victim->is_exclusive = exclusive;
                    page_table[page_id] = victim_id;
                    fifo_queue.push_back(victim_id);
                    evicted = true;
                    lock.unlock();

                    if (exclusive) {
                        victim->mtx.lock();
                    } else {
                        victim->mtx.lock_shared();
                    }
                    return *victim;
                } else {
                    ++it;
                }
            }
            if (!evicted) {
                throw buffer_full_error{};
            }
        }

        auto new_frame = std::make_unique<BufferFrame>(page_id, storage_manager.load(page_id));
        new_frame->pin_count = 1;
        new_frame->is_exclusive = exclusive;
        size_t new_frame_id = buffer_pool.size();
        buffer_pool.push_back(std::move(new_frame));
        page_table[page_id] = new_frame_id;
        fifo_queue.push_back(new_frame_id);
        BufferFrame* frame = buffer_pool[new_frame_id].get();
        lock.unlock();

        if (exclusive) {
            frame->mtx.lock();
        } else {
            frame->mtx.lock_shared();
        }
        return *frame;
    }

    void unfix_page(BufferFrame& page, bool is_dirty) {
        if (is_dirty) {
            page.is_dirty = true;
        }
        if (page.is_exclusive) {
            page.mtx.unlock();
            page.is_exclusive = false;
        } else {
            page.mtx.unlock_shared();
        }
        std::lock_guard<std::mutex> lock(manager_mutex);
        page.pin_count--;
    }


    void extend(){
        storage_manager.extend();
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

    /// Returns the page IDs of all pages in the FIFO list, in FIFO order.
    /// Is not thread-safe.
    std::vector<PageID> get_fifo_list() const {
        // TODO: add your implementation here
        std::vector<PageID> fifo_pages;
        for (const auto& frame_id : fifo_queue) {
            fifo_pages.push_back(buffer_pool[frame_id]->page_id);
        }
        return fifo_pages;
    }

    /// Returns the page IDs of all pages in the LRU list, in LRU order.
    /// Not thread-safe.
    /// Is not thread-safe.
    std::vector<PageID> get_lru_list() const {
        // TODO: add your implementation here
        std::vector<PageID> lru_pages;
        for (const auto& frame_id : lru_list) {
            lru_pages.push_back(buffer_pool[frame_id]->page_id);
        }
        return lru_pages;
    }
};

class BuzzDB {
public:
    BufferManager buffer_manager;

public:
    BuzzDB(){
        // Storage Manager automatically created
    }
};

int main(int argc, char* argv[]) {    
    bool execute_all = false;
    std::string selected_test = "-1";

    if(argc < 2) {
        execute_all = true;
    } else {
        selected_test = argv[1];
    }


    if(execute_all || selected_test == "1") {
        // Test 1: FixSingle
        {
            std::cout<<"...Starting Test 1"<<std::endl;

            BuzzDB db;
            std::vector<uint64_t> expected_values(PAGE_SIZE / sizeof(uint64_t), 123);
            {
                auto& page_frame = db.buffer_manager.fix_page(0, true);
                auto& page = page_frame.page;
                std::memcpy(page->page_data.get(), expected_values.data(), PAGE_SIZE);
                db.buffer_manager.unfix_page(page_frame, true);

                assert(db.buffer_manager.get_lru_list().empty());
                assert(db.buffer_manager.get_fifo_list() == std::vector<FrameID>{0});
            }

            {
                std::vector<uint64_t> values(PAGE_SIZE / sizeof(uint64_t));
                auto& page_frame = db.buffer_manager.fix_page(0, false);
                auto& page = page_frame.page;
                std::memcpy(values.data(), page->page_data.get(), PAGE_SIZE);
                db.buffer_manager.unfix_page(page_frame, true);
                
                assert(db.buffer_manager.get_fifo_list().empty());
                assert(std::vector<uint64_t>{0} ==  db.buffer_manager.get_lru_list());
                assert(expected_values == values);
                
            }
            
            std::cout<<"Passed: Test 1"<<std::endl;
        }
    }
    
    if(execute_all || selected_test == "2") {
        // Test 2: PersistentRestart
        {
            std::cout<<"...Starting Test 2"<<std::endl;

            auto db = std::make_unique<BuzzDB>();
            for (uint16_t segment = 0; segment < 3; ++segment) {
                for (uint64_t segment_page = 0; segment_page < 10; ++segment_page) {
                    // assuming 20 pages per segment to get the page_id
                    uint64_t page_id = segment*200 + segment_page;
                    auto& page_frame = db->buffer_manager.fix_page(page_id, true);
                    auto& page = page_frame.page;
                    
                    uint64_t& value = *reinterpret_cast<uint64_t*>(page->page_data.get());
                    value = segment * 200 + segment_page;
                    db->buffer_manager.unfix_page(page_frame, true);
                }
            }
            //Destroy the buffer manager and create a new one.
            db = std::make_unique<BuzzDB>();
            for (uint16_t segment = 0; segment < 3; ++segment) {
                for (uint64_t segment_page = 0; segment_page < 10; ++segment_page) {
                    uint64_t page_id = segment*200 + segment_page;
                    auto& page_frame = db->buffer_manager.fix_page(page_id, false);
                    auto& page = page_frame.page;
                    
                    uint64_t value = *reinterpret_cast<uint64_t*>(page->page_data.get());
                    db->buffer_manager.unfix_page(page_frame, false);
                    assert(segment * 200 + segment_page == value);
                }
            }
            
            std::cout<<"Passed: Test 2"<<std::endl;
        }
    }
    
    if(execute_all || selected_test == "3") {
        //Test 3: FIFOEvict
        {
            std::cout<<"...Starting Test 3"<<std::endl;

            BuzzDB db;
            for (uint64_t i = 1; i < 11; ++i) {
                auto& page = db.buffer_manager.fix_page(i, false);
                db.buffer_manager.unfix_page(page, false);
            }
            std::vector<uint64_t> expected_fifo{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            assert(expected_fifo == db.buffer_manager.get_fifo_list());
            assert(db.buffer_manager.get_lru_list().empty());
            
            auto& page = db.buffer_manager.fix_page(11, false);
            db.buffer_manager.unfix_page(page, false);

            expected_fifo = std::vector<u_int64_t>{2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
            assert(expected_fifo == db.buffer_manager.get_fifo_list());
            assert(db.buffer_manager.get_lru_list().empty());
            
            std::cout<<"Passed: Test 3"<<std::endl;
        }
    }
    
    if(execute_all || selected_test == "4") {
        //Test 4: BufferFull
        {
            std::cout<<"...Starting Test 4"<<std::endl;

            BuzzDB db;
            std::vector<BufferFrame*> pages;
            pages.reserve(10);
            for (uint64_t i = 1; i < 11; ++i) {
                auto& page = db.buffer_manager.fix_page(i, false);
                pages.push_back(&page);
            }

            bool exception_thrown = false;
            try {
                db.buffer_manager.fix_page(11, false);
            } catch (const buffer_full_error& ex) {
                exception_thrown = true;
            }
            assert(exception_thrown);
            
            for (auto* page : pages) {
                db.buffer_manager.unfix_page(*page, false);
            }
            
            std::cout<<"Passed: Test 4"<<std::endl;
        }
    }
    
    if(execute_all || selected_test == "5") {
        //Test 5: MoveToLRU
        {
            std::cout<<"...Starting Test 5"<<std::endl;

            BuzzDB db;
            
            auto& fifo_page = db.buffer_manager.fix_page(1, false);
            auto* lru_page = &db.buffer_manager.fix_page(2, false);
            
            db.buffer_manager.unfix_page(fifo_page, false);
            db.buffer_manager.unfix_page(*lru_page, false);
            
            assert((std::vector<uint64_t>{1, 2}) == db.buffer_manager.get_fifo_list());
            assert(db.buffer_manager.get_lru_list().empty());
            
            lru_page = &db.buffer_manager.fix_page(2, false);
            db.buffer_manager.unfix_page(*lru_page, false);
            
            assert(std::vector<uint64_t>{1} == db.buffer_manager.get_fifo_list());
            assert(std::vector<uint64_t>{2} == db.buffer_manager.get_lru_list());
            
            std::cout<<"Passed: Test 5"<<std::endl;
        }
    }

    if(execute_all || selected_test == "6") {
        //Test 6: LRURefresh
        {
            std::cout<<"...Starting Test 6"<<std::endl;

            BuzzDB db;
            
            auto* page1 = &db.buffer_manager.fix_page(1, false);
            db.buffer_manager.unfix_page(*page1, false);
            page1 = &db.buffer_manager.fix_page(1, false);
            db.buffer_manager.unfix_page(*page1, false);
            auto* page2 = &db.buffer_manager.fix_page(2, false);
            db.buffer_manager.unfix_page(*page2, false);
            page2 = &db.buffer_manager.fix_page(2, false);
            db.buffer_manager.unfix_page(*page2, false);
            
            assert(db.buffer_manager.get_fifo_list().empty());
            assert((std::vector<uint64_t>{1, 2}) == db.buffer_manager.get_lru_list());
            
            page1 = &db.buffer_manager.fix_page(1, false);
            db.buffer_manager.unfix_page(*page1, false);
            assert(db.buffer_manager.get_fifo_list().empty());
            assert((std::vector<uint64_t>{2, 1}) == db.buffer_manager.get_lru_list());
            
            std::cout<<"Passed: Test 6"<<std::endl;
        }
    }

    if(execute_all || selected_test == "7") {
        //Test 7: MultithreadParallelFix
        {
            std::cout<<"...Starting Test 7"<<std::endl;

            BuzzDB db;
            std::vector<std::thread> threads;
            for (size_t i = 0; i < 4; ++i) {
                threads.emplace_back([i, &db] {
                    auto& page1 = db.buffer_manager.fix_page(i, false);
                    auto& page2 = db.buffer_manager.fix_page(i + 4, false);
                    db.buffer_manager.unfix_page(page1, false);
                    db.buffer_manager.unfix_page(page2, false);
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }

            auto fifo_list = db.buffer_manager.get_fifo_list();
            std::sort(fifo_list.begin(), fifo_list.end());
            std::vector<uint64_t> expected_fifo{0, 1, 2, 3, 4, 5, 6, 7};
            assert(expected_fifo == fifo_list);
            assert(db.buffer_manager.get_lru_list().empty());
            
            std::cout<<"Passed: Test 7"<<std::endl;
        }
    }
    
    if(execute_all || selected_test == "8") {
        //Test 8: MultithreadExclusiveAccess
        {
            std::cout<<"...Starting Test 8"<<std::endl;

            BuzzDB db;
            {
                auto& page_frame = db.buffer_manager.fix_page(0, true);
                auto& page = page_frame.page;
                std::memset(page->page_data.get(), 0, PAGE_SIZE);
                db.buffer_manager.unfix_page(page_frame, true);
            }
            std::vector<std::thread> threads;
            for (size_t i = 0; i < 4; ++i) {
                threads.emplace_back([&db] {
                    for (size_t j = 0; j < 1000; ++j) {
                        auto& page_frame = db.buffer_manager.fix_page(0, true);
                        auto& page = page_frame.page;
                        uint64_t& value = *reinterpret_cast<uint64_t*>(page->page_data.get());
                        ++value;
                        db.buffer_manager.unfix_page(page_frame, true);
                    }
                });
            }

            for (auto& thread : threads) {
                thread.join();
            }
            assert(db.buffer_manager.get_fifo_list().empty());
            assert(std::vector<uint64_t>{0} == db.buffer_manager.get_lru_list());
            
            auto& page_frame = db.buffer_manager.fix_page(0, false);
            auto& page = page_frame.page;
            uint64_t value = *reinterpret_cast<uint64_t*>(page->page_data.get());
            db.buffer_manager.unfix_page(page_frame, false);
            assert(4000 == value);
            
            std::cout<<"Passed: Test 8"<<std::endl;
        }
    }
    
    if(execute_all || selected_test == "9") {
        //Test 9: MultithreadBufferFull
        {
            std::cout<<"...Starting Test 9"<<std::endl;

            BuzzDB db;
            std::atomic<uint64_t> num_buffer_full = 0;
            std::atomic<uint64_t> finished_threads = 0;
            std::vector<std::thread> threads;
            size_t max_threads = 8;

            for (size_t i = 0; i < max_threads; ++i) {
                threads.emplace_back(
                    [i, &db, &num_buffer_full, &finished_threads, &max_threads] {
                        std::vector<BufferFrame*> pages;
                        pages.reserve(8);
                        for (size_t j = 0; j < 8; ++j) {
                            try {
                                pages.push_back(&db.buffer_manager.fix_page(i + j * 8, false));
                            } catch (const buffer_full_error&) {
                                ++num_buffer_full;
                            }
                        }
                        ++finished_threads;
                        // Busy wait until all threads have finished.
                        while (finished_threads.load() < max_threads) {
                        }
                        for (auto* page : pages) {
                            db.buffer_manager.unfix_page(*page, false);
                        }
                    });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            assert(10 == db.buffer_manager.get_fifo_list().size());
            assert(db.buffer_manager.get_lru_list().empty());
            assert(54 == num_buffer_full.load());

            std::cout<<"Passed: Test 9"<<std::endl;
        }
    }
    
    if(execute_all || selected_test == "10") {
        //Test 10: MultithreadManyPages
        {
            std::cout<<"...Starting Test 10"<<std::endl;

            BuzzDB db;
            std::atomic<uint64_t> num_unfixes = 0;
            std::vector<std::thread> threads;

            for (size_t i = 0; i < 4; ++i) {
                threads.emplace_back([i, &db, &num_unfixes] {
                    std::mt19937_64 engine{i};
                    std::uniform_int_distribution<uint64_t> distr(0, 400);
                    for (size_t j = 0; j < 10000; ++j) {
                        PageID next_page = distr(engine);
                        auto& page = db.buffer_manager.fix_page(next_page, false);
                        db.buffer_manager.unfix_page(page, false);
                        num_unfixes++;
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            assert(num_unfixes.load() == 40000);

            std::cout<<"Passed: Test 10"<<std::endl;
        }
    }

    /*
    if(execute_all || selected_test == "11") {
        //Test 11: MultithreadReaderWriter
        {
            std::cout<<"...Starting Test 11"<<std::endl;

            {
                BuzzDB db;
                for (uint16_t segment = 0; segment <= 3; ++segment) {
                    for (uint64_t segment_page = 0; segment_page <= 100; ++segment_page) {
                        uint64_t page_id = segment*200 + segment_page;
                        auto& page_frame = db.buffer_manager.fix_page(page_id, true);
                        auto& page = page_frame.page;

                        std::memset(page->page_data.get(), 0, PAGE_SIZE);
                        db.buffer_manager.unfix_page(page_frame, true);
                    }
                }
                // Let the buffer manager be destroyed here so that the caches are
                // empty before running the actual test.
            }

            BuzzDB db;
            std::atomic<size_t> aborts = 0;
            std::vector<std::thread> threads;
            for (size_t i = 0; i < 4; ++i) {
                threads.emplace_back([i, &db, &aborts] {
                    std::mt19937_64 engine{i};
                    // 5% of queries are scans.
                    std::bernoulli_distribution scan_distr{0.05};
                    // Number of pages accessed by a point query is geometrically
                    // distributed.
                    std::geometric_distribution<size_t> num_pages_distr{0.5};
                    // 60% of point queries are reads.
                    std::bernoulli_distribution reads_distr{0.6};
                    // Out of 20 accesses, 12 are from segment 0, 5 from segment 1,
                    // 2 from segment 2, and 1 from segment 3.
                    std::discrete_distribution<uint16_t> segment_distr{12.0, 5.0, 2.0, 1.0};
                    // Page accesses for point queries are uniformly distributed in
                    // [0, 100].
                    std::uniform_int_distribution<uint64_t> page_distr{0, 100};
                    std::vector<uint64_t> scan_sums(4);
                    for (size_t j = 0; j < 100; ++j) {
                        uint16_t segment = segment_distr(engine);
                        uint64_t segment_shift = segment * 200;
                        if (scan_distr(engine)) {
                            // scan
                            uint64_t scan_sum = 0;
                            for (uint64_t segment_page = 0; segment_page <= 100; ++segment_page) {
                                uint64_t page_id = segment_shift + segment_page;
                                BufferFrame* page_frame;
                                while (true) {
                                    try {
                                        page_frame = &db.buffer_manager.fix_page(page_id, false);
                                        break;
                                    } catch (const buffer_full_error&) {
                                        // Don't abort scan when the buffer is full, retry
                                        // the current page.
                                    }
                                }
                                auto& page = page_frame->page;
                                uint64_t value = *reinterpret_cast<uint64_t*>(page->page_data.get());
                                scan_sum += value;
                                db.buffer_manager.unfix_page(*page_frame, false);
                            }
                            assert(scan_sum >= scan_sums[segment]);
                            scan_sums[segment] = scan_sum;
                        } else {
                            // point query
                            auto num_pages = num_pages_distr(engine) + 1;
                            // For point queries all accesses but the last are always
                            // reads. Only the last is potentially a write. Also,
                            // all pages but the last are held for the entire duration
                            // of the query.
                            std::vector<BufferFrame*> pages;
                            auto unfix_pages = [&] {
                                for (auto it = pages.rbegin(); it != pages.rend(); ++it) {
                                    auto& page = **it;
                                    db.buffer_manager.unfix_page(page, false);
                                }
                                pages.clear();
                            };
                            for (size_t page_number = 0; page_number < num_pages - 1; ++page_number) {
                                uint64_t segment_page = page_distr(engine);
                                uint64_t page_id = segment_shift + segment_page;
                                BufferFrame* page;
                                try {
                                    page = &db.buffer_manager.fix_page(page_id, false);
                                } catch (const buffer_full_error&) {
                                    // Abort query when buffer is full.
                                    ++aborts;
                                    goto abort;
                                }
                                pages.push_back(page);
                            }
                            // Unfix all pages before accessing the last one
                            // (potentially exclusively) to avoid deadlocks.
                            unfix_pages();
                            {
                                uint64_t segment_page = page_distr(engine);
                                uint64_t page_id = segment_shift + segment_page;;
                                if (reads_distr(engine)) {
                                    // read
                                    BufferFrame* page_frame;
                                    try {
                                        page_frame = &db.buffer_manager.fix_page(page_id, false);
                                    } catch (const buffer_full_error&) {
                                        ++aborts;
                                        goto abort;
                                    }
                                    db.buffer_manager.unfix_page(*page_frame, false);
                                } else {
                                    // write
                                    BufferFrame* page_frame;
                                    try {
                                        page_frame = &db.buffer_manager.fix_page(page_id, true);
                                    } catch (const buffer_full_error&) {
                                        ++aborts;
                                        goto abort;
                                    }
                                    auto& page = page_frame->page;
                                    auto& value = *reinterpret_cast<uint64_t*>(page->page_data.get());
                                    ++value;
                                    db.buffer_manager.unfix_page(*page_frame, true);
                                }
                            }
                        abort:
                            unfix_pages();
                    }
                }
            });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            assert(aborts.load() < 20);

            std::cout<<"Passed: Test 11"<<std::endl;
        }
    }
    */
    return 0;
}