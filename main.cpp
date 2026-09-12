#include <iostream>
#include <random>

int main() {
    constexpr int kGroupCount = 3;
    constexpr int kNumbersPerGroup = 10;
    constexpr int kMinValue = 1;
    constexpr int kMaxValue = 100;

    std::random_device randomDevice;
    std::mt19937 engine(randomDevice());
    std::uniform_int_distribution<int> distribution(kMinValue, kMaxValue);

    std::cout << "自动生成三组随机数据：\n";
    for (int group = 0; group < kGroupCount; ++group) {
        std::cout << "第 " << group + 1 << " 组：";
        for (int number = 0; number < kNumbersPerGroup; ++number) {
            std::cout << distribution(engine);
            if (number + 1 < kNumbersPerGroup) std::cout << ' ';
        }
        std::cout << '\n';
    }
    return 0;
}
