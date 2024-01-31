#ifndef XSS_PIVOT_SELECTION
#define XSS_PIVOT_SELECTION

#include "xss-network-qsort.hpp"

template <typename type_t>
struct pivot_results{
    bool alreadySorted = false;
    bool only2Values = false;
    type_t pivot = 0;
    
    pivot_results(type_t _pivot){
        pivot = _pivot;
        alreadySorted = false;
    }
    
    pivot_results(type_t _pivot, bool _alreadySorted){
        pivot = _pivot;
        alreadySorted = _alreadySorted;
    }
};

template <typename type_t>
type_t next_value(type_t value){
    // TODO this probably handles non-native float16 wrong
    if constexpr (std::is_floating_point<type_t>::value){
        return std::nextafter(value, std::numeric_limits<type_t>::infinity());
    }else{
        if (value < std::numeric_limits<type_t>::max()){
            return value + 1;
        }else{
            return value;
        }
    }
}

template <typename vtype, typename mm_t>
X86_SIMD_SORT_INLINE void COEX(mm_t &a, mm_t &b);

template <typename vtype, typename type_t>
X86_SIMD_SORT_INLINE type_t get_pivot(type_t *arr,
                                      const arrsize_t left,
                                      const arrsize_t right)
{
    using reg_t = typename vtype::reg_t;
    type_t samples[vtype::numlanes];
    arrsize_t delta = (right - left) / vtype::numlanes;
    for (int i = 0; i < vtype::numlanes; i++) {
        samples[i] = arr[left + i * delta];
    }
    reg_t rand_vec = vtype::loadu(samples);
    reg_t sort = vtype::sort_vec(rand_vec);

    return ((type_t *)&sort)[vtype::numlanes / 2];
}

template <typename vtype, typename type_t>
X86_SIMD_SORT_INLINE type_t get_pivot_blocks(type_t *arr,
                                             const arrsize_t left,
                                             const arrsize_t right)
{

    if (right - left <= 1024) { return get_pivot<vtype>(arr, left, right); }

    using reg_t = typename vtype::reg_t;
    constexpr int numVecs = 5;

    arrsize_t width = (right - vtype::numlanes) - left;
    arrsize_t delta = width / numVecs;

    reg_t vecs[numVecs];
    // Load data
    for (int i = 0; i < numVecs; i++) {
        vecs[i] = vtype::loadu(arr + left + delta * i);
    }

    // Implement sorting network (from https://bertdobbelaere.github.io/sorting_networks.html)
    COEX<vtype>(vecs[0], vecs[3]);
    COEX<vtype>(vecs[1], vecs[4]);

    COEX<vtype>(vecs[0], vecs[2]);
    COEX<vtype>(vecs[1], vecs[3]);

    COEX<vtype>(vecs[0], vecs[1]);
    COEX<vtype>(vecs[2], vecs[4]);

    COEX<vtype>(vecs[1], vecs[2]);
    COEX<vtype>(vecs[3], vecs[4]);

    COEX<vtype>(vecs[2], vecs[3]);

    // Calculate median of the middle vector
    reg_t &vec = vecs[numVecs / 2];
    vec = vtype::sort_vec(vec);

    type_t data[vtype::numlanes];
    vtype::storeu(data, vec);
    return data[vtype::numlanes / 2];
}

template <typename vtype, typename type_t>
X86_SIMD_SORT_INLINE pivot_results<type_t> get_pivot_near_constant(type_t *arr,
                                             type_t commonValue,
                                             const arrsize_t left,
                                             const arrsize_t right);

template <typename vtype, typename type_t>
X86_SIMD_SORT_INLINE pivot_results<type_t> get_pivot_smart(type_t *arr,
                                             const arrsize_t left,
                                             const arrsize_t right)
{
    using reg_t = typename vtype::reg_t;
    constexpr int numVecs = 4;
    
    if (right - left + 1 <= 4 * numVecs * vtype::numlanes){
        return pivot_results<type_t>(get_pivot<vtype>(arr, left, right)); 
    }
    
    constexpr int N = numVecs * vtype::numlanes;

    arrsize_t width = (right - vtype::numlanes) - left;
    arrsize_t delta = width / numVecs;

    reg_t vecs[numVecs];
    for (int i = 0; i < numVecs; i++) {
        vecs[i] = vtype::loadu(arr + left + delta * i);
    }
    
    // Sort the samples
    sort_vectors<vtype, numVecs>(vecs);
    
    type_t samples[N];
    for (int i = 0; i < numVecs; i++){
        vtype::storeu(samples + vtype::numlanes * i, vecs[i]);
    }
    
    type_t smallest = samples[0];
    type_t largest = samples[N - 1];
    type_t median = samples[N / 2];
    
    if (smallest == largest){
        // We have a very unlucky sample, or the array is constant / near constant
        // Run a special function meant to deal with this situation
        return get_pivot_near_constant<vtype, type_t>(arr, median, left, right);
    }else if (median != smallest && median != largest){
        // We have a normal sample; use it's median
        return pivot_results<type_t>(median);
    }else if (median == smallest){
        // If median == smallest, that implies approximately half the array is equal to smallest, unless we were very unlucky with our sample
        // Try just doing the next largest value greater than this seemingly very common value to seperate them out
        return pivot_results<type_t>(next_value<type_t>(median));
    }else if (median == largest){
        // If median == largest, that implies approximately half the array is equal to largest, unless we were very unlucky with our sample
        // Thus, median probably is a fine pivot, since it will move all of this common value into its own partition
        return pivot_results<type_t>(median);
    }else{
        // Should be unreachable
        return pivot_results<type_t>(median);
    }
    
    // Should be unreachable
    return pivot_results<type_t>(median);
}

// Handles the case where we seem to have a near-constant array, since our sample of the array was constant
template <typename vtype, typename type_t>
X86_SIMD_SORT_INLINE pivot_results<type_t> get_pivot_near_constant(type_t *arr,
                                             type_t commonValue,
                                             const arrsize_t left,
                                             const arrsize_t right)
{
    using reg_t = typename vtype::reg_t;
    
    arrsize_t index = left;
    
    type_t value1 = 0;
    type_t value2 = 0;
    
    // First, search for any value not equal to the common value
    // First vectorized
    reg_t commonVec = vtype::set1(commonValue);
    for (; index <= right - vtype::numlanes; index += vtype::numlanes){
        reg_t data = vtype::loadu(arr + index);
        if (!vtype::all_false(vtype::knot_opmask(vtype::eq(data, commonVec)))){
            break;
        }
    }
    
    // Than scalar at the end
    for (; index <= right; index++){
        if (arr[index] != commonValue){
            value1 = arr[index];
            break;
        } 
    }
    
    if (index == right + 1){
        // The array is completely constant
        // Setting the second flag to true skips partitioning, as the array is constant and thus sorted
        return pivot_results<type_t>(commonValue, true);
    }
    
    // Secondly, search for a second value not equal to either of the previous two
    // First vectorized
    reg_t value1Vec = vtype::set1(value1);
    for (; index <= right - vtype::numlanes; index += vtype::numlanes){
        reg_t data = vtype::loadu(arr + index);
        if (!vtype::all_false(vtype::knot_opmask(vtype::eq(data, commonVec))) && !vtype::all_false(vtype::knot_opmask(vtype::eq(data, value1Vec)))){
            break;
        }
    }
    
    // Then scalar
    for (; index <= right; index++){
        if (arr[index] != commonValue && arr[index] != value1){
            value2 = arr[index];
            break;
        } 
    }
    
    if (index == right + 1){
        // The array contains only 2 values
        // We must pick the larger one, else the right partition is empty
        // We can also skip recursing, as it is guaranteed both partitions are constant after partitioning with the larger value
        // TODO this logic now assumes we use greater than or equal to specifically when partitioning, might be worth noting that somewhere
        type_t pivot = std::max(value1, commonValue, comparison_func<vtype>);
        auto result = pivot_results<type_t>(pivot, false);
        result.only2Values = true;
        return result;
    }
    
    // The array has at least 3 distinct values. Use the middle one as the pivot
    type_t median = std::max(std::min(value1,value2, comparison_func<vtype>), std::min(std::max(value1,value2, comparison_func<vtype>),commonValue, comparison_func<vtype>), comparison_func<vtype>);
    return pivot_results<type_t>(median);
}

#endif