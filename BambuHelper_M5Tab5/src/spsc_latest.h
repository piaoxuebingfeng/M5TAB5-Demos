#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#if defined(SPSC_LATEST_TEST_HOOK)
#include <functional>
#endif
#if defined(BOARD_IS_WS_P4_7B)
#include <esp_heap_caps.h>
#endif

// One producer, one consumer, and no waiting in either direction. Three slots
// hold the publication, a short consumer copy, and the producer's next value.
// Two slots are allowed only when the producer retries a failed beginWrite(),
// as for an explicitly requested screenshot.
// publish() only swaps the public index. A consumer already reading the old
// slot finishes its copy; the producer never reclaims that slot for writing.
// This intentionally does not provide a reliable command queue: intermediate
// values may be coalesced, but the final published revision remains readable.
template<typename T, size_t N = 3>
class SpscLatest {
  static_assert(N >= 2, "a publication and a separate writing slot");
  static_assert(std::is_trivially_copyable<T>::value, "plain snapshot data");
  enum : uint8_t { Free, Writing, Ready, Reading };
  struct Slot { T value{}; std::atomic<uint8_t> state{Free}; uint32_t revision = 0; };
  Slot* slots_ = nullptr;
  Slot* ownedSlots_ = nullptr;
  std::atomic<int> published_{-1};
  int writing_ = -1;
  uint32_t nextRevision_ = 1;
  uint32_t consumedRevision_ = 0;
#if defined(SPSC_LATEST_TEST_HOOK)
  std::function<void()> afterClaim_;
  std::function<void()> beforeRetire_;
  std::function<void()> beforeConsume_;
#endif
  struct ExternalStorage {
    Slot slots[N];
    SpscLatest mailbox;
    ExternalStorage() : mailbox(slots) {}
  };
  // The access cell lives with the mailbox, never in the lazily allocated
  // slots. A consumer can therefore acquire the slot pointer even while the
  // producer is publishing the first value on another core.
  struct SlotAccess { std::atomic<Slot*> slots{nullptr}; };
  SlotAccess accessStorage_{};
  SlotAccess* access_ = &accessStorage_;
  explicit SpscLatest(Slot* external) : slots_(external) {
    access_->slots.store(external, std::memory_order_relaxed);
  }
public:
#if defined(BOARD_IS_WS_P4_7B)
  // Global constructors run before initArduino() adds PSRAM to the heap.
  // Only the producer allocates, on its first post-boot publication.
  SpscLatest() = default;
  ~SpscLatest() {
    if (!ownedSlots_) return;
    for (size_t i = 0; i < N; ++i) ownedSlots_[i].~Slot();
    heap_caps_free(ownedSlots_);
  }
#else
  SpscLatest() : slots_(new(std::nothrow) Slot[N]), ownedSlots_(slots_) {
    access_->slots.store(slots_, std::memory_order_relaxed);
  }
  ~SpscLatest() { delete[] ownedSlots_; }
#endif
  SpscLatest(const SpscLatest&) = delete;
  SpscLatest& operator=(const SpscLatest&) = delete;
  static SpscLatest* createExternal() {
#if defined(BOARD_IS_WS_P4_7B)
    void* memory = heap_caps_calloc(1, sizeof(ExternalStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* storage = new (memory) ExternalStorage;
    return &storage->mailbox;
#else
    return new SpscLatest();
#endif
  }
  // Only for an unpublished mailbox before either owner has started, or
  // after both owners have stopped. Never destroy an active mailbox.
  static void destroyExternal(SpscLatest* mailbox) {
    if (!mailbox) return;
#if defined(BOARD_IS_WS_P4_7B)
    static_assert(std::is_standard_layout<ExternalStorage>::value, "external mailbox layout");
    auto* storage = reinterpret_cast<ExternalStorage*>(
      reinterpret_cast<unsigned char*>(mailbox) - offsetof(ExternalStorage, mailbox));
    storage->~ExternalStorage();
    heap_caps_free(storage);
#else
    delete mailbox;
#endif
  }
  T* beginWrite() {
#if defined(BOARD_IS_WS_P4_7B)
    if (!slots_) {
      auto* storage = static_cast<Slot*>(heap_caps_malloc(sizeof(Slot) * N,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!storage) return nullptr;
      for (size_t i = 0; i < N; ++i) new (&storage[i]) Slot;
      slots_ = ownedSlots_ = storage;
      access_->slots.store(storage, std::memory_order_release);
    }
#endif
    if (!slots_) return nullptr;
    if (writing_ >= 0) return &slots_[writing_].value;
    const int published = published_.load(std::memory_order_acquire);
    for (size_t i = 0; i < N; ++i) {
      if (int(i) == published) continue;
      // Free is the usual case. Ready outside the published index is a
      // superseded slot whose reader finished after publish() tried to retire
      // it; it is also safe for this single producer to reclaim.
      uint8_t expected = slots_[i].state.load(std::memory_order_acquire);
      if (expected != Free && expected != Ready) continue;
      if (slots_[i].state.compare_exchange_strong(expected, Writing,
          std::memory_order_acq_rel, std::memory_order_relaxed)) {
        writing_ = int(i);
        return &slots_[i].value;
      }
    }
    return nullptr;
  }
  void publish() {
    if (writing_ < 0) return;
    const int slot = writing_;
    slots_[slot].revision = nextRevision_++;
    if (!nextRevision_) nextRevision_ = 1;
    slots_[slot].state.store(Ready, std::memory_order_release);
    const int old = published_.exchange(slot, std::memory_order_acq_rel);
    writing_ = -1;
    // A consumer can release the current publication before the producer
    // reuses that same slot. Publishing it again must not retire itself.
    if (old < 0 || old == slot) return;
#if defined(SPSC_LATEST_TEST_HOOK)
    if (beforeRetire_) beforeRetire_();
#endif
    // A reader of the previous publication either finishes and frees it, or
    // this CAS retires it immediately. Never wait for, or overwrite, Reading.
    uint8_t expected = Ready;
    slots_[old].state.compare_exchange_strong(expected, Free,
        std::memory_order_acq_rel, std::memory_order_relaxed);
  }
  void cancelWrite() {
    if (writing_ < 0) return;
    slots_[writing_].state.store(Free, std::memory_order_release);
    writing_ = -1;
  }
  // False means the value could not be published. Latest-state producers
  // retry on a later pass; one-shot commands belong in a bounded queue.
  bool update(const T& value) {
    if (T* slot = beginWrite()) { *slot = value; publish(); return true; }
    return false;
  }
private:
  bool read(T& destination, bool onlyNew) {
    // Publication changes require at most a few CAS retries. A busy producer
    // can make this invocation return false; it can never make either side
    // wait. The next invocation still observes the final published revision.
    for (size_t attempt = 0; attempt < N + 1; ++attempt) {
      Slot* slots = access_->slots.load(std::memory_order_acquire);
      if (!slots) return false;
      const int index = published_.load(std::memory_order_acquire);
      // Acquiring the first publication also publishes the producer's lazy
      // slot pointer. Never race its initialization with an empty read.
      if (index < 0) return false;
      uint8_t expected = Ready;
      if (!slots[index].state.compare_exchange_strong(expected, Reading,
          std::memory_order_acq_rel, std::memory_order_relaxed)) continue;
      const uint32_t revision = slots[index].revision;
      const bool deliver = !onlyNew || revision != consumedRevision_;
#if defined(SPSC_LATEST_TEST_HOOK)
      if (deliver && afterClaim_) afterClaim_();
#endif
      // The reader holds the slot while checking its revision. Repeated reads
      // of an unchanged large snapshot must not copy it into the UI again.
      if (deliver) {
        std::memcpy(&destination, &slots[index].value, sizeof(T));
#if defined(SPSC_LATEST_TEST_HOOK)
        if (onlyNew && beforeConsume_) beforeConsume_();
#endif
        if (onlyNew) consumedRevision_ = revision;
      }
      const bool current = published_.load(std::memory_order_acquire) == index;
      slots[index].state.store(current ? Ready : Free, std::memory_order_release);
      return deliver;
    }
    return false;
  }
public:
  bool copyLatest(T& destination) { return read(destination, false); }
  bool readNext(T& destination) { return read(destination, true); }
#if defined(SPSC_LATEST_TEST_HOOK)
  void testHooks(std::function<void()> afterClaim, std::function<void()> beforeRetire,
                 std::function<void()> beforeConsume = nullptr) {
    afterClaim_ = std::move(afterClaim); beforeRetire_ = std::move(beforeRetire);
    beforeConsume_ = std::move(beforeConsume);
  }
#endif
};
