#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <iostream>

template <typename T>
class RawMemory {
 public:
  RawMemory() = default;

  explicit RawMemory(size_t capacity)
      : buffer_(Allocate(capacity))
      , capacity_(capacity) {}

  RawMemory(const RawMemory&) = delete;
  RawMemory& operator=(const RawMemory& rhs) = delete;
  RawMemory(RawMemory&& other) noexcept
  {
    Deallocate(buffer_);
    buffer_ = std::exchange(other.buffer_, nullptr);
    capacity_ = std::exchange(other.capacity_, 0);
  }
  RawMemory& operator=(RawMemory&& rhs) noexcept {
    if (this->buffer_ != rhs.buffer_) {
      buffer_ = std::move(rhs.buffer_);
      capacity_ = std::move(rhs.capacity_);
      rhs.buffer_ = nullptr;
      rhs.capacity_ = 0;
    }
    return *this;
  }
  ~RawMemory() {
    Deallocate(buffer_);
  }

  T* operator+(size_t offset) noexcept {
    // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
    assert(offset <= capacity_);
    return buffer_ + offset;
  }

  const T* operator+(size_t offset) const noexcept {
    return const_cast<RawMemory&>(*this) + offset;
  }

  const T& operator[](size_t index) const noexcept {
    return const_cast<RawMemory&>(*this)[index];
  }

  T& operator[](size_t index) noexcept {
    assert(index < capacity_);
    return buffer_[index];
  }

  void Swap(RawMemory& other) noexcept {
    std::swap(buffer_, other.buffer_);
    std::swap(capacity_, other.capacity_);
  }

  const T* GetAddress() const noexcept {
    return buffer_;
  }

  T* GetAddress() noexcept {
    return buffer_;
  }

  size_t Capacity() const {
    return capacity_;
  }

 private:
  // Выделяет сырую память под n элементов и возвращает указатель на неё
  static T* Allocate(size_t n) {
    return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
  }

  // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
  static void Deallocate(T* buf) noexcept {
    operator delete(buf);
  }

  T* buffer_ = nullptr;
  size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
  using iterator = T*;
  using const_iterator = const T*;
  iterator begin() noexcept {
    return data_.GetAddress();
  }
  iterator end() noexcept {
    return data_.GetAddress() + size_;
  }
  const_iterator begin() const noexcept {
    return data_.GetAddress();
  }
  const_iterator end() const noexcept {
    return data_.GetAddress() + size_;
  }
  const_iterator cbegin() const noexcept {
    return data_.GetAddress();
  }
  const_iterator cend() const noexcept {
    return data_.GetAddress() + size_;
  }

  Vector() = default;

  explicit Vector(size_t size)
      : data_(size)
      , size_(size)
      {
    std::uninitialized_value_construct_n(data_.GetAddress(), size_);
      }

  Vector(const Vector& other)
      : data_(other.size_)
      , size_(other.size_) {
    std::uninitialized_copy_n(other.data_.GetAddress(), size_,  data_.GetAddress());
  }

  Vector(Vector&& other) noexcept {
    data_ = std::move(other.data_);
    size_ = std::exchange(other.size_, 0);
  }

  ~Vector() {
    std::destroy_n(data_.GetAddress(), size_);
  }

  void Resize(size_t new_size) {
    if (new_size < size_) {
      std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
    } else {
      Reserve(new_size);
      std::uninitialized_default_construct_n(data_.GetAddress() + size_, new_size - size_);
    }
    size_ = new_size;
  }

  void PushBack(const T& value) {
  EmplaceBack(value);
  }

  void PushBack(T&& value) {
    EmplaceBack(std::move(value));
  }

  template<typename S>
  void PushBack(S&& value) {
    if (size_ == Capacity()) {
      RawMemory<T> relocated_v(size_ == 0 ? 1 : size_ * 2);
      new (relocated_v + size_) T(std::forward<S>(value));
      if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
        std::uninitialized_move_n(data_.GetAddress(), size_, relocated_v.GetAddress());
      } else {
        std::uninitialized_copy_n(data_.GetAddress(), size_, relocated_v.GetAddress());
      }
      data_.Swap(relocated_v);
      std::destroy_n(relocated_v.GetAddress(), relocated_v.Capacity());
    } else {
      new (data_ + size_) T(std::forward<S>(value));
    }
    ++size_;
  }

  template <typename... Args>
  iterator Emplace(const_iterator pos, Args&&... args) {
    size_t distance = pos - begin();
    if (size_ == Capacity()) {
      RawMemory<T> relocated_v(size_ == 0 ? 1 : size_ * 2);
      new (relocated_v + distance) T(std::forward<Args>(args)...);
      if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
        std::uninitialized_move_n(data_.GetAddress(), pos - begin(), relocated_v.GetAddress());
        std::uninitialized_move_n(data_.GetAddress() + distance, end() - pos, relocated_v.GetAddress() + distance + 1);
      } else {
        std::uninitialized_copy_n(data_.GetAddress(), pos - begin(), relocated_v.GetAddress());
        std::uninitialized_copy_n(data_.GetAddress() + distance, end() - pos, relocated_v.GetAddress() + distance + 1);
      }
      data_.Swap(relocated_v);
      std::destroy_n(relocated_v.GetAddress(), relocated_v.Capacity());
    } else {
      if (size_ != 0) {
        T tmp_var(std::forward<Args>(args)...);
        new(end()) T(std::forward<T>(*(end() - 1)));
        std::move_backward(begin() + distance, end() - 1, end());
        data_[distance] = std::move(tmp_var);
      } else {
        new (data_.GetAddress()) T(std::forward<Args>(args)...);
      }
    }
    ++size_;
    return begin() + distance;
  }

  template<typename... Args>
  T& EmplaceBack(Args&&... args) {
    if (size_ == Capacity()) {
      RawMemory<T> relocated_v(size_ == 0 ? 1 : size_ * 2);
      new (relocated_v + size_) T(std::forward<Args>(args)...);
      if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
        std::uninitialized_move_n(data_.GetAddress(), size_, relocated_v.GetAddress());
      } else {
        std::uninitialized_copy_n(data_.GetAddress(), size_, relocated_v.GetAddress());
      }
      data_.Swap(relocated_v);
      std::destroy_n(relocated_v.GetAddress(), relocated_v.Capacity());
    } else {
      new (data_ + size_) T(std::forward<Args>(args)...);
    }
    ++size_;
    return data_[size_ - 1];
  }

  iterator Insert(const_iterator pos, const T& value) {
    return Emplace(pos, value);
  }

  iterator Insert(const_iterator pos, T&& value) {
    return Emplace(pos, std::move(value));
  }

  iterator Erase(const_iterator pos) {
    pos->~T();
    size_t distance = pos - begin();
    T* it1 = begin() + distance;
    T* it2 = begin() + distance + 1;
    while (it2 != end()) {
      *it1 = std::move(*it2);
      ++it1;
      ++it2;
    }
    --size_;
    return begin() + distance;
  }

  void PopBack() /* noexcept */ {
    std::destroy_n(data_ + (--size_), 1);
  }

  void Reserve(size_t new_capacity) {
    if (new_capacity <= data_.Capacity()) {
      return;
    }
    RawMemory<T> new_data(new_capacity);
    if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
          std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
          std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
    std::destroy_n(data_.GetAddress(), size_);
    data_.Swap(new_data);
  }

  Vector& operator=(const Vector& rhs) {
    if (this != &rhs) {
      if (rhs.size_ > data_.Capacity()) {
        Vector rhs_copy(rhs);
        Swap(rhs_copy);
      } else {
        size_t i = 0;
        if (size_ > rhs.size_) {
          for (;i < rhs.size_; ++i) {
            *(data_.GetAddress() + i) = *(rhs.data_.GetAddress() + i);
          }
          std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
        }
        else {
          for (; i < size_; ++i) {
            data_[i] = rhs.data_[i];
          }
          if (rhs.size_ > size_) {
            std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
          }
        }
        size_ = rhs.size_;
      }

    }
    return *this;
  }

  Vector& operator=(Vector&& rhs) noexcept {
    if (this->data_.GetAddress() != rhs.data_.GetAddress()) {
      data_ = std::move(rhs.data_);
      size_ = rhs.size_;
      rhs.size_ = 0;
    }
    return *this;
  }

  void Swap(Vector& other) noexcept {
    data_.Swap(other.data_);
    std::swap(size_, other.size_);
  }

  size_t Size() const noexcept {
    return size_;
  }

  size_t Capacity() const noexcept {
    return data_.Capacity();
  }

  const T& operator[](size_t index) const noexcept {
    return const_cast<Vector&>(*this)[index];
  }

  T& operator[](size_t index) noexcept {
    assert(index < size_);
    return data_[index];
  }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};