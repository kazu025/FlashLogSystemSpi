#pragma once
#include <stddef.h>
#include <stdint.h>
#include <type_traits>
void dump_hex(const uint8_t* data, size_t len);
bool compare_buffers(const uint8_t* a, const uint8_t* b, size_t len);

/*
 * 可変長移動平均クラス
 * T    : 入力データ型
 * N    : バッファサイズ
 * SumT : 合計値
 */
template <
    typename T,
    size_t N,
    typename SumT = std::conditional_t<
        std::is_floating_point_v<T>,
        double,
        int64_t
    >
>
class MovingAverage {
public:
    static_assert(N > 0, "MovingAverage buffer size N must be greater than 0");
    MovingAverage(){
        reset();
    }

    void reset(){
        index_ = 0;
        count_ = 0;
        sum_ = static_cast<SumT>(0);
        for (size_t i = 0; i < N; i++) {
            buffer_[i] = T{};
        }
    }

    void add(T value){
        if (count_ < N) {
            buffer_[index_] = value;
            sum_ += static_cast<SumT>(value);
            count_++;
        } else {
            sum_ -= static_cast<SumT>(buffer_[index_]);
            buffer_[index_] = value;
            sum_ += static_cast<SumT>(value);
        }

        index_++;
        if (index_ >= N) {
            index_ = 0;
        }
    }

    T average() const {
        if (count_ == 0) {
            return T{};
        }

        return static_cast<T>(sum_ / static_cast<SumT>(count_));
    }

    double averageDouble() const {
        if (count_ == 0) {
            return 0.0;
        }

        return static_cast<double>(sum_) / static_cast<double>(count_);
    }

    SumT sum() const {
        return sum_;
    }

    size_t count() const {
        return count_;
    }

    size_t capacity() const {
        return N;
    }

    bool isFull() const {
        return count_ >= N;
    }

    bool isEmpty() const {
        return count_ == 0;
    }

private:
    T buffer_[N];
    size_t index_;
    size_t count_;
    SumT sum_;
};