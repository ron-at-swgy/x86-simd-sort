#include <cmath>

static constexpr char x[] = "x";
static constexpr char euclidean[] = "euclidean";
static constexpr char taxicab[] = "taxicab";
static constexpr char chebyshev[] = "chebyshev";

template <const char* val>
struct Point3D {
    double x;
    double y;
    double z;
    static constexpr std::string_view name {val};
    Point3D()
    {
        x = (double)rand() / RAND_MAX;
        y = (double)rand() / RAND_MAX;
        z = (double)rand() / RAND_MAX;
    }
    double distance()
    {
        if constexpr (name == "x") {
            return x;
        }
        else if constexpr (name == "euclidean") {
            return std::sqrt(x * x + y * y + z * z);
        }
        else if constexpr (name == "taxicab") {
            return abs(x) + abs(y) + abs(z);
        }
        else if constexpr (name == "chebyshev") {
            return std::max(std::max(x, y), z);
        }
    }
};

template <typename T>
std::vector<T> init_data(const int size)
{
    srand(42);
    std::vector<T> arr;
    for (auto ii = 0; ii < size; ++ii) {
        T temp;
        arr.push_back(temp);
    }
    return arr;
}

template <typename T>
struct less_than_key {
    inline bool operator()(T &p1, T &p2)
    {
        return (p1.distance() < p2.distance());
    }
};

template <typename T>
static void scalarobjsort(benchmark::State &state)
{
    // set up array
    std::vector<T> arr = init_data<T>(state.range(0));
    std::vector<T> arr_bkp = arr;
    // benchmark
    for (auto _ : state) {
        std::sort(arr.begin(), arr.end(), less_than_key<T>());
        state.PauseTiming();
        arr = arr_bkp;
        state.ResumeTiming();
    }
}

template <typename T>
static void simdobjsort(benchmark::State &state)
{
    // set up array
    std::vector<T> arr = init_data<T>(state.range(0));
    std::vector<T> arr_bkp = arr;
    // benchmark
    for (auto _ : state) {
        x86simdsort::object_qsort(arr.data(), arr.size(), [](T p) -> double {
            return p.distance();
        });
        state.PauseTiming();
        if (!std::is_sorted(arr.begin(), arr.end(), less_than_key<T>())) {
            std::cout << "sorting failed \n";
        }
        arr = arr_bkp;
        state.ResumeTiming();
    }
}

#define BENCHMARK_OBJSORT(func, T) \
    BENCHMARK_TEMPLATE(func, T) \
            ->Arg(10e1) \
            ->Arg(10e2) \
            ->Arg(10e3) \
            ->Arg(10e4) \
            ->Arg(10e5) \
            ->Arg(10e6);

BENCHMARK_OBJSORT(simdobjsort, Point3D<x>)
BENCHMARK_OBJSORT(scalarobjsort, Point3D<x>)
BENCHMARK_OBJSORT(simdobjsort, Point3D<taxicab>)
BENCHMARK_OBJSORT(scalarobjsort, Point3D<taxicab>)
BENCHMARK_OBJSORT(simdobjsort, Point3D<euclidean>)
BENCHMARK_OBJSORT(scalarobjsort, Point3D<euclidean>)
BENCHMARK_OBJSORT(simdobjsort, Point3D<chebyshev>)
BENCHMARK_OBJSORT(scalarobjsort, Point3D<chebyshev>)